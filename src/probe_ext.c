// probe_ext.c — check the bundled 2023 IGCL runtime on the live A770.
#define WIN32_LEAN_AND_MEAN
#include "arc.h"
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    const char *dll = argc > 1 ? argv[1] : "IntelControlLib-2023.dll";
    HMODULE h = LoadLibraryA(dll);
    printf("LoadLibrary: %p\n", (void *)h);
    if (!h) { printf("err=%lu\n", GetLastError()); return 1; }
    ctl_result_t (*init)(ctl_init_args_t *, void **) = (ctl_result_t(*)(ctl_init_args_t *, void **))GetProcAddress(h, "ctlInit");
    ctl_result_t (*enumd)(void *, uint32_t *, void **) = (ctl_result_t(*)(void *, uint32_t *, void **))GetProcAddress(h, "ctlEnumerateDevices");
    ctl_result_t (*props)(void *, ctl_device_adapter_properties_t *) = (ctl_result_t(*)(void *, ctl_device_adapter_properties_t *))GetProcAddress(h, "ctlGetDeviceProperties");
    ctl_result_t (*waiver)(void *) = (ctl_result_t(*)(void *))GetProcAddress(h, "ctlOverclockWaiverSet");
    ctl_result_t (*sett)(void *, double) = (ctl_result_t(*)(void *, double))GetProcAddress(h, "ctlOverclockTemperatureLimitSet");
    ctl_result_t (*gett)(void *, double *) = (ctl_result_t(*)(void *, double *))GetProcAddress(h, "ctlOverclockTemperatureLimitGet");
    printf("sym init=%p enum=%p props=%p waiver=%p set=%p get=%p\n", (void *)init, (void *)enumd, (void *)props, (void *)waiver, (void *)sett, (void *)gett);

    ctl_init_args_t args;
    memset(&args, 0, sizeof args);
    args.Size = sizeof args; args.Version = 0; args.AppVersion = 0x00010000; args.flags = 1; // LEVEL_ZERO
    void *api = NULL;
    ctl_result_t r = init(&args, &api);
    printf("ctlInit(flags=1,zero uid): 0x%08x api=%p\n", r, api);
    if (r != 0) {
        args.ApplicationUID.Data1 = 0xe8e10f95;
        args.ApplicationUID.Data2 = 0x1a70;
        args.ApplicationUID.Data3 = 0x4b27;
        unsigned char uid[8] = {0x9c, 0xcf, 0x02, 0x01, 0x02, 0x64, 0xe9, 0xc8};
        memcpy(args.ApplicationUID.Data4, uid, 8);
        r = init(&args, &api);
        printf("ctlInit(arc uid): 0x%08x api=%p\n", r, api);
    }
    if (r != 0 || !api) return 1;

    uint32_t n = 0;
    r = enumd(api, &n, NULL);
    printf("enum(count): 0x%08x n=%u\n", r, n);
    if (!n) return 1;
    void *handles[8] = {0};
    if (n > 8) n = 8;
    enumd(api, &n, handles);
    for (uint32_t i = 0; i < n; i++) {
        ctl_device_adapter_properties_t p;
        memset(&p, 0, sizeof p); p.Size = sizeof p; p.Version = 3;
        r = props(handles[i], &p);
        printf("[%u] props 0x%08x type=%d '%s' ven=%04x bdf=%02x:%02x.%x\n", i, r, p.device_type, p.name,
               p.pci_vendor_id, p.adapter_bdf.bus, p.adapter_bdf.device, p.adapter_bdf.function);
        if (waiver) printf("    waiver: 0x%08x\n", waiver(handles[i]));
        double v = -1;
        if (gett) printf("    temp get: 0x%08x value=%.1f\n", gett(handles[i], &v), v);
        v = -1;
        if (sett) printf("    temp set 95: 0x%08x\n", sett(handles[i], 95.0));
        Sleep(300);
        v = -1;
        if (gett) printf("    temp get: 0x%08x value=%.1f\n", gett(handles[i], &v), v);
    }
    return 0;
}
