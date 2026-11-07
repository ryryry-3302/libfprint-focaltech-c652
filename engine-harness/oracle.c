/*
 * oracle.c - run the vendor WinBio engine adapter as a black-box reference
 * matcher on captured 64x80 images (run under Wine).
 *
 *   wine oracle.exe <manifest> <mode 0-3>
 *
 * Manifest lines:   E <path.pgm>          enrolment image (used until enrolment completes)
 *                   P <label> <path.pgm>  probe to verify against the enrolled template
 *
 * Interfaces used (from static analysis of the adapter, see docs/ENGINE.md):
 *   - IOCTL 0x442018 (sensor size) answered with {width, height} bytes
 *   - AcceptSampleData takes a WINBIO_BIR with an ANSI-381 8-bit image
 *   - Create/Update/CommitEnrollment via the WinBio engine interface;
 *     the template is captured by a stub storage AddRecord
 *   - verify mirrors VerifyFeatureSet: algo slots 0x30, 0x18, 0x40
 */
#include "common.h"

#define W 64
#define H 80

/* ---------- DeviceIoControl hook (IAT patch) ---------- */
static BOOL WINAPI fake_ioctl(HANDLE h, DWORD code, LPVOID in, DWORD inlen, LPVOID out,
                              DWORD outlen, LPDWORD ret, LPOVERLAPPED ov)
{
    (void)h; (void)in; (void)inlen; (void)ov;
    if (out && outlen) memset(out, 0, outlen);
    if (code == 0x442018 && out && outlen >= 2) {          /* IOCTL_BIOMETRIC_SENSOR_SIZE */
        ((BYTE *)out)[0] = W; ((BYTE *)out)[1] = H;
    }
    if (ret) *ret = outlen;
    fprintf(stderr, "  [ioctl 0x%06lx out %lu]\n", code, outlen);
    return TRUE;
}

static void hook_iat(HMODULE m, const char *dll, const char *fn, void *repl)
{
    BYTE *base = (BYTE *)m;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    IMAGE_DATA_DIRECTORY d = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    for (IMAGE_IMPORT_DESCRIPTOR *imp = (void *)(base + d.VirtualAddress); imp->Name; imp++) {
        if (_stricmp((char *)base + imp->Name, dll)) continue;
        IMAGE_THUNK_DATA *names = (void *)(base + imp->OriginalFirstThunk);
        IMAGE_THUNK_DATA *iat = (void *)(base + imp->FirstThunk);
        for (; names->u1.AddressOfData; names++, iat++) {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            IMAGE_IMPORT_BY_NAME *ibn = (void *)(base + names->u1.AddressOfData);
            if (strcmp((char *)ibn->Name, fn)) continue;
            DWORD old; VirtualProtect(&iat->u1.Function, 8, PAGE_READWRITE, &old);
            iat->u1.Function = (ULONGLONG)repl;
            VirtualProtect(&iat->u1.Function, 8, old, &old);
            return;
        }
    }
    fprintf(stderr, "hook %s!%s not found\n", dll, fn); exit(1);
}

/* ---------- stub storage adapter ---------- */
STUBS(sensor) STUBS(storage)
static BYTE *g_tpl; static SIZE_T g_tpl_len;
static HRESULT WINAPI st_add_record(void *pipe, BYTE *rec)            /* slot 9 */
{
    (void)pipe;
    BYTE *tpl = *(BYTE **)(rec + 0x20); SIZE_T len = *(SIZE_T *)(rec + 0x28);
    g_tpl = malloc(len); memcpy(g_tpl, tpl, len); g_tpl_len = len;
    return S_OK;
}
static HRESULT WINAPI st_query_subject(void *a, void *b, void *c)    /* slot 11 */
{ (void)a; (void)b; (void)c; return 0x8009801F; }                    /* no results */

/* ---------- image input ---------- */
static int load_pgm(const char *path, unsigned short *px)
{
    FILE *f = fopen(path, "rb"); if (!f) return -1;
    int w, h, mx;
    if (fscanf(f, "P5 %d %d %d", &w, &h, &mx) != 3 || w != W || h != H) { fclose(f); return -1; }
    fgetc(f);
    for (int i = 0; i < W * H; i++) { int a = fgetc(f), b = fgetc(f); px[i] = (a << 8) | b; }
    fclose(f); return 0;
}

/* 16-bit sensor values -> 8-bit.
 * mode 0: min/max stretch      mode 1: inverted stretch
 * mode 2: fixed scale (0..400)  mode 3: column/row offset removal, then stretch */
static int cmp_us(const void *a, const void *b) { return *(const int *)a - *(const int *)b; }
static void to8(const unsigned short *px, BYTE *out, int mode)
{
    int v[W * H];
    for (int i = 0; i < W * H; i++) v[i] = px[i];
    if (mode == 2) {
        for (int i = 0; i < W * H; i++) { int x = v[i] * 255 / 400; out[i] = (BYTE)(x > 255 ? 255 : x); }
        return;
    }
    if (mode == 3) {
        int col[H], row[W];
        for (int x = 0; x < W; x++) { for (int y = 0; y < H; y++) col[y] = v[y * W + x];
            qsort(col, H, sizeof *col, cmp_us); for (int y = 0; y < H; y++) v[y * W + x] -= col[H / 2]; }
        for (int y = 0; y < H; y++) { for (int x = 0; x < W; x++) row[x] = v[y * W + x];
            qsort(row, W, sizeof *row, cmp_us); for (int x = 0; x < W; x++) v[y * W + x] -= row[W / 2]; }
    }
    int lo = 1 << 30, hi = -(1 << 30);
    for (int i = 0; i < W * H; i++) { if (v[i] < lo) lo = v[i]; if (v[i] > hi) hi = v[i]; }
    for (int i = 0; i < W * H; i++) {
        int x = hi > lo ? (v[i] - lo) * 255 / (hi - lo) : 0;
        out[i] = (BYTE)(mode == 1 ? 255 - x : x);
    }
}

/* WINBIO_BIR: header block (0x30 bytes), standard block = ANSI-381 record */
static BYTE *make_bir(const BYTE *img8, SIZE_T *size)
{
    const SIZE_T hdr_off = 0x20, hdr_len = 0x30, std_off = hdr_off + hdr_len,
                 std_len = 0x38 + W * H;
    BYTE *b = calloc(1, std_off + std_len);
    *(ULONG *)(b + 0x00) = hdr_len;  *(ULONG *)(b + 0x04) = hdr_off;   /* HeaderBlock   */
    *(ULONG *)(b + 0x08) = std_len;  *(ULONG *)(b + 0x0c) = std_off;   /* StandardBlock */
    BYTE *h = b + hdr_off;
    *(USHORT *)(h + 0x28) = 0x001B; *(USHORT *)(h + 0x2a) = 0x0401;   /* ANSI 381 */
    BYTE *s = b + std_off;
    *(ULONG64 *)(s + 0x00) = std_len;
    *(USHORT *)(s + 0x1c) = 500; *(USHORT *)(s + 0x1e) = 500;         /* resolution (dpi) */
    s[0x20] = 1; s[0x21] = 1; s[0x22] = 8; s[0x23] = 0;              /* 8-bit, uncompressed */
    *(ULONG *)(s + 0x28) = 0x38 - 0x26 + W * H;
    *(USHORT *)(s + 0x2c) = W; *(USHORT *)(s + 0x2e) = H;
    memcpy(s + 0x38, img8, W * H);
    *size = std_off + std_len; return b;
}

typedef HRESULT (WINAPI *F1)(WB_PIPELINE *);
typedef HRESULT (WINAPI *F_ACCEPT)(WB_PIPELINE *, BYTE *, SIZE_T, BYTE, DWORD *);
typedef HRESULT (WINAPI *F_UPDATE)(WB_PIPELINE *, DWORD *);
typedef HRESULT (WINAPI *F_COMMIT)(WB_PIPELINE *, void *, BYTE, BYTE *, SIZE_T);
typedef int (*A_1)(void *, int);
typedef int (*A_IMPORT)(void *, int, BYTE *, ULONG);
typedef int (*A_IDENT)(void *, void *, int, BYTE *);

int main(int argc, char **argv)
{
    if (argc != 3) { fprintf(stderr, "usage: oracle <manifest> <mode 0-3>\n"); return 1; }
    int mode = atoi(argv[2]);
    HMODULE m = LoadLibraryA("vendor\\ftWbioEngineAdapter.dll");
    if (!m) { fprintf(stderr, "cannot load vendor\\ftWbioEngineAdapter.dll\n"); return 1; }
    hook_iat(m, "KERNEL32.dll", "DeviceIoControl", fake_ioctl);
    WB_IFACE *e; ((HRESULT (WINAPI *)(WB_IFACE **))GetProcAddress(m, "WbioQueryEngineInterface"))(&e);

    static WB_IFACE si, ti; FILL(sensor, &si); FILL(storage, &ti);
    ti.Fn[9] = st_add_record; ti.Fn[11] = st_query_subject;
    static WB_PIPELINE p; p.SensorHandle = p.StorageHandle = (HANDLE)0x1234; p.EngineHandle = INVALID_HANDLE_VALUE;
    p.EngineInterface = e; p.SensorInterface = &si; p.StorageInterface = &ti;
    HRESULT hr;
    if ((hr = ((F1)e->Fn[E_ATTACH])(&p))) { printf("Attach failed 0x%08lx\n", hr); return 1; }
    ((F1)e->Fn[E_PIPE_INIT])(&p); ((F1)e->Fn[E_ACTIVATE])(&p);

    void **ctx = (void **)p.EngineContext;
    void *algo = ctx[0]; void **vt = (void **)algo;   /* algo is a struct of function pointers */ void *img = ctx[2];
    printf("engine ready: sensor %dx%d, enrol samples needed %d\n",
           ((BYTE *)ctx)[0x1c], ((BYTE *)ctx)[0x1d], *(int *)ctx[1]);

    FILE *mf = fopen(argv[1], "r"); if (!mf) { perror(argv[1]); return 1; }
    char line[600], label[100], path[500];
    unsigned short px[W * H]; BYTE img8[W * H];
    int enr_done = 0, committed = 0, n_enr = 0;
    ((F1)e->Fn[E_CREATE_ENR])(&p);
    while (fgets(line, sizeof line, mf)) {
        int is_enr = line[0] == 'E';
        if (is_enr ? sscanf(line, "E %499s", path) != 1 : sscanf(line, "P %99s %499s", label, path) != 2) continue;
        if (is_enr && enr_done) continue;
        if (!is_enr && !committed) {
            /* finish enrolment: commit -> template captured by AddRecord stub */
            BYTE ident[80] = {0}; *(ULONG *)ident = 3; *(ULONG *)(ident + 4) = 12;
            hr = ((F_COMMIT)e->Fn[E_COMMIT_ENR])(&p, ident, 0xf5, NULL, 0);
            printf("commit -> 0x%08lx, template %llu bytes (from %d images)\n", hr,
                   (unsigned long long)g_tpl_len, n_enr);
            if (!g_tpl) return 1;
            committed = 1;
        }
        if (load_pgm(path, px)) { printf("bad image %s\n", path); continue; }
        to8(px, img8, mode);
        SIZE_T bsz; BYTE *bir = make_bir(img8, &bsz); DWORD reject = 0;
        hr = ((F_ACCEPT)e->Fn[E_ACCEPT])(&p, bir, bsz, is_enr ? 4 : 1, &reject);
        free(bir);
        if (is_enr) {
            if (hr) { printf("E %s accept 0x%08lx reject %lu\n", path, hr, reject); continue; }
            hr = ((F_UPDATE)e->Fn[E_UPDATE_ENR])(&p, &reject);
            n_enr++;
            printf("E %s update 0x%08lx reject %lu\n", path, hr, reject);
            if (hr == 0) enr_done = 1;               /* engine has enough samples */
            continue;
        }
        if (hr) { printf("P %s %s accept 0x%08lx reject %lu -> REJECTED(quality)\n", label, path, hr, reject); continue; }
        ((A_1)vt[0x30 / 8])(algo, 1);
        ((A_IMPORT)vt[0x18 / 8])(algo, 1, g_tpl, (ULONG)g_tpl_len);
        BYTE res[0x20] = {0};
        int r = ((A_IDENT)vt[0x40 / 8])(algo, img, 1, res);
        int *ri = (int *)res;
        printf("P %s %s ident %d -> %s  res %d %d %d %d %d %d %d %d\n", label, path, r,
               ri[5] > 0 ? "MATCH" : "no", ri[0], ri[1], ri[2], ri[3], ri[4], ri[5], ri[6], ri[7]);
    }
    return 0;
}
