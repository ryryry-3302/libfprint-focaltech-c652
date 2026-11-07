// SPDX-License-Identifier: LGPL-2.1-or-later
/* common.h - minimal WinBio adapter definitions (from Microsoft's documented layout). */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>

typedef struct { USHORT Major, Minor; } WB_VER;
typedef struct { WB_VER Version; ULONG Type; SIZE_T Size; GUID AdapterId; void *Fn[64]; } WB_IFACE;

typedef struct {
    HANDLE SensorHandle, EngineHandle, StorageHandle;
    WB_IFACE *SensorInterface, *EngineInterface, *StorageInterface;
    void *SensorContext, *EngineContext, *StorageContext;
    void *FrameworkInterface;
    void *pad[16];
} WB_PIPELINE;

enum { E_ATTACH, E_DETACH, E_CLEAR, E_QPREFFMT, E_QIDXVEC, E_QHASH, E_SETHASH, E_QHINT,
       E_ACCEPT, E_EXPORT, E_VERIFY, E_IDENTIFY, E_CREATE_ENR, E_UPDATE_ENR, E_ENR_STATUS,
       E_ENR_HASH, E_CHECK_DUP, E_COMMIT_ENR, E_DISCARD_ENR, E_CTRL, E_CTRL_PRIV,
       E_POWER, E_RES1, E_PIPE_INIT, E_PIPE_CLEAN, E_ACTIVATE, E_DEACTIVATE, E_QEXTINFO };

/* logging stubs for sensor (S) and storage (T) interfaces: print which slot was called */
#define STUB(P, i) static HRESULT WINAPI P##i(void *a, void *b, void *c, void *d) { \
    printf("  >> %s slot %d called (args %p %p %p %p)\n", #P, i, a, b, c, d); return E_NOTIMPL; }
#define STUBS(P) STUB(P,0) STUB(P,1) STUB(P,2) STUB(P,3) STUB(P,4) STUB(P,5) STUB(P,6) STUB(P,7) \
  STUB(P,8) STUB(P,9) STUB(P,10) STUB(P,11) STUB(P,12) STUB(P,13) STUB(P,14) STUB(P,15) STUB(P,16) \
  STUB(P,17) STUB(P,18) STUB(P,19) STUB(P,20) STUB(P,21) STUB(P,22) STUB(P,23) STUB(P,24) STUB(P,25) \
  STUB(P,26) STUB(P,27) STUB(P,28) STUB(P,29) STUB(P,30) STUB(P,31)
#define FILL(P, iface) do { void *t[] = { P##0,P##1,P##2,P##3,P##4,P##5,P##6,P##7,P##8,P##9,P##10, \
  P##11,P##12,P##13,P##14,P##15,P##16,P##17,P##18,P##19,P##20,P##21,P##22,P##23,P##24,P##25,P##26, \
  P##27,P##28,P##29,P##30,P##31 }; memcpy((iface)->Fn, t, sizeof t); (iface)->Size = sizeof(WB_IFACE); } while (0)

static void hexdump(const char *label, const void *p, size_t n)
{
    const unsigned char *b = p; printf("%s:", label);
    for (size_t i = 0; i < n; i++) printf("%s%02x", i % 32 ? " " : "\n   ", b[i]);
    printf("\n");
}
