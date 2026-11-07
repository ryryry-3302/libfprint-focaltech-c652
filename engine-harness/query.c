// SPDX-License-Identifier: LGPL-2.1-or-later
/* query.c - load ftWbioEngineAdapter.dll and dump its WINBIO_ENGINE_INTERFACE. */
#include <windows.h>
#include <stdio.h>

typedef struct { USHORT Major, Minor; } VER;
typedef struct {
    VER     Version;
    ULONG   Type;
    SIZE_T  Size;
    GUID    AdapterId;
    void   *Fn[64];          /* function pointers follow; count derived from Size */
} ENGINE_IFACE;
typedef HRESULT (WINAPI *QUERY_FN)(ENGINE_IFACE **);

static const char *names[] = {
 "Attach","Detach","ClearContext","QueryPreferredFormat","QueryIndexVectorSize",
 "QueryHashAlgorithms","SetHashAlgorithm","QuerySampleHint","AcceptSampleData",
 "ExportEngineData","VerifyFeatureSet","IdentifyFeatureSet","CreateEnrollment",
 "UpdateEnrollment","GetEnrollmentStatus","GetEnrollmentHash","CheckForDuplicate",
 "CommitEnrollment","DiscardEnrollment","ControlUnit","ControlUnitPrivileged",
 "NotifyPowerChange","Reserved_1",
 "PipelineInit","PipelineCleanup","Activate","Deactivate","QueryExtendedInfo",
 "IdentifyAll","SetEnrollmentSelector","SetEnrollmentParameters",
 "QueryExtendedEnrollmentStatus","RefreshCache","SelectCalibrationFormat",
 "QueryCalibrationData","SetAccountPolicy","CreateKey","IdentifyFeatureSetSecure",
 "AcceptPrivateSensorTypeInfo","CreateEnrollmentAuthenticated",
 "IdentifyFeatureSetAuthenticated" };

int main(void)
{
    HMODULE m = LoadLibraryA("ftWbioEngineAdapter.dll");
    if (!m) { printf("LoadLibrary failed: %lu\n", GetLastError()); return 1; }
    QUERY_FN q = (QUERY_FN)GetProcAddress(m, "WbioQueryEngineInterface");
    ENGINE_IFACE *e = NULL;
    HRESULT hr = q(&e);
    printf("WbioQueryEngineInterface hr=0x%08lx  base=%p\n", hr, (void *)m);
    if (FAILED(hr) || !e) return 1;
    printf("version %u.%u  type %lu  size %llu\n", e->Version.Major, e->Version.Minor,
           e->Type, (unsigned long long)e->Size);
    int n = (int)((e->Size - offsetof(ENGINE_IFACE, Fn)) / sizeof(void *));
    printf("function slots: %d\n", n);
    for (int i = 0; i < n && i < 64; i++)
        printf("  [%2d] %-32s %s rva=0x%llx\n", i, i < (int)(sizeof names / sizeof *names) ? names[i] : "?",
               e->Fn[i] ? "" : "(null)", e->Fn[i] ? (unsigned long long)((char *)e->Fn[i] - (char *)m) : 0ULL);
    return 0;
}
