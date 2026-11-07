#include "common.h"
STUBS(sensor) STUBS(storage)
typedef HRESULT (WINAPI *FN1)(WB_PIPELINE *);
typedef HRESULT (WINAPI *FNFMT)(WB_PIPELINE *, USHORT *, GUID *);
typedef HRESULT (WINAPI *FNINFO)(WB_PIPELINE *, void *, SIZE_T);

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    HMODULE m = LoadLibraryA("ftWbioEngineAdapter.dll");
    WB_IFACE *e; ((HRESULT (WINAPI *)(WB_IFACE **))GetProcAddress(m, "WbioQueryEngineInterface"))(&e);
    static WB_IFACE si, ti; FILL(sensor, &si); FILL(storage, &ti);
    static WB_PIPELINE p; p.SensorHandle = p.EngineHandle = p.StorageHandle = INVALID_HANDLE_VALUE;
    p.EngineInterface = e; p.SensorInterface = &si; p.StorageInterface = &ti;

    printf("Attach        -> 0x%08lx  (EngineContext %p)\n", ((FN1)e->Fn[E_ATTACH])(&p), p.EngineContext);
    printf("PipelineInit  -> 0x%08lx\n", ((FN1)e->Fn[E_PIPE_INIT])(&p));
    printf("Activate      -> 0x%08lx\n", ((FN1)e->Fn[E_ACTIVATE])(&p));
    USHORT fmt[2] = {0}; GUID vend = {0};
    HRESULT hr = ((FNFMT)e->Fn[E_QPREFFMT])(&p, fmt, &vend);
    printf("QueryPreferredFormat -> 0x%08lx  standard owner 0x%04x type 0x%04x\n", hr, fmt[0], fmt[1]);
    hexdump("  vendor format GUID", &vend, 16);
    static unsigned char info[512];
    hr = ((FNINFO)e->Fn[E_QEXTINFO])(&p, info, sizeof info);
    printf("QueryExtendedInfo -> 0x%08lx\n", hr); hexdump("  info", info, 96);
    if (p.EngineContext) hexdump("EngineContext (first 128 bytes)", p.EngineContext, 128);
    return 0;
}
