// probe_peak.c — can the peak (AC) power limit raise the burst budget?
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

typedef int32_t ze_result_t;
typedef void *ze_handle;
typedef struct { uint8_t enabled; int32_t power, interval; } sus_t;
typedef struct { uint8_t enabled; int32_t power; } burst_t;
typedef struct { int32_t powerAC, powerDC; } peak_t;

int main(int argc, char **argv) {
  int setPk = argc > 1 ? atoi(argv[1]) : -1;      // mW for peak AC, -1 = read only
  HMODULE z = LoadLibraryA("C:\\Windows\\System32\\ze_loader.dll");
  if (!z) { printf("no ze_loader\n"); return 1; }
  ze_result_t (*zesInit)(uint32_t) = (void *)GetProcAddress(z, "zesInit");
  ze_result_t (*zesDriverGet)(uint32_t *, ze_handle *) = (void *)GetProcAddress(z, "zesDriverGet");
  ze_result_t (*zesDeviceGet)(ze_handle, uint32_t *, ze_handle *) = (void *)GetProcAddress(z, "zesDeviceGet");
  ze_result_t (*enumPwr)(ze_handle, uint32_t *, ze_handle *) = (void *)GetProcAddress(z, "zesDeviceEnumPowerDomains");
  ze_result_t (*getL)(ze_handle, sus_t *, burst_t *, peak_t *) = (void *)GetProcAddress(z, "zesPowerGetLimits");
  ze_result_t (*setL)(ze_handle, const sus_t *, const burst_t *, const peak_t *) = (void *)GetProcAddress(z, "zesPowerSetLimits");
  if (!zesInit || zesInit(0) != 0) { printf("zesInit failed\n"); return 1; }
  uint32_t dn = 0; ze_handle drv[4] = {0};
  zesDriverGet(&dn, NULL); if (dn > 4) dn = 4; zesDriverGet(&dn, drv);
  uint32_t devn = 0; ze_handle dev[8] = {0};
  zesDeviceGet(drv[0], &devn, NULL); if (devn > 8) devn = 8; zesDeviceGet(drv[0], &devn, dev);
  ze_handle doms[8] = {0}; uint32_t dc = 8;
  if (enumPwr(dev[0], &dc, doms) != 0 || dc == 0) { printf("no power domains\n"); return 1; }
  ze_handle pwr = NULL;
  for (uint32_t i = 0; i < dc; i++) { sus_t s; burst_t b; peak_t p; memset(&s,0,sizeof s); memset(&b,0,sizeof b); memset(&p,0,sizeof p);
    if (getL(doms[i], &s, &b, &p) == 0) { pwr = doms[i]; break; } }
  if (!pwr) { printf("no usable power domain\n"); return 1; }
  sus_t s; burst_t b; peak_t p; memset(&s,0,sizeof s); memset(&b,0,sizeof b); memset(&p,0,sizeof p);
  getL(pwr, &s, &b, &p);
  printf("before: sustained=%d mW burst=%d mW peakAC=%d peakDC=%d\n", s.power, b.power, p.powerAC, p.powerDC);
  if (setPk > 0) {
    peak_t np = p; np.powerAC = setPk;
    ze_result_t r = setL(pwr, &s, &b, &np);
    printf("set peakAC=%d -> r=0x%x\n", setPk, (unsigned)r);
    Sleep(400);
    sus_t s2 = {0}; burst_t b2 = {0}; peak_t p2 = {0};
    getL(pwr, &s2, &b2, &p2);
    printf("after : sustained=%d mW burst=%d mW peakAC=%d peakDC=%d\n", s2.power, b2.power, p2.powerAC, p2.powerDC);
  }
  return 0;
}
