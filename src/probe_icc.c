// probe_icc.c — what the driver exposes on the Sysman overclock domains
// (does it have an ICC max / current limit control, and is it writable?)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

typedef int32_t ze_result_t;
typedef void *ze_handle;

typedef struct { uint32_t stype; void *pNext; int32_t domainType; uint32_t AvailableControls, VFProgramType, NumberOfVFPoints; } oc_props_t;
typedef struct { double MinValue, MaxValue, StepValue, RefValue, DefaultValue; } ctrl_prop_t;
typedef struct {
  uint32_t stype; void *pNext; uint8_t isOcSupported;
  double maxFactoryDefaultFrequency, maxFactoryDefaultVoltage, maxOcFrequency;
  double minOcVoltageOffset, maxOcVoltageOffset, maxOcVoltage;
  uint8_t isTjMaxSupported, isIccMaxSupported, isHighVoltModeCapable, isHighVoltModeEnabled;
  uint8_t isExtendedModeSupported, isFixedModeSupported;
} freq_oc_caps_t;

static const char *DOMAIN_NAME(uint32_t b) {
  switch (b) {
    case 1: return "CARD"; case 2: return "PACKAGE"; case 4: return "GPU_ALL";
    case 8: return "GPU_RENDER_COMPUTE"; case 16: return "GPU_RENDER"; case 32: return "GPU_COMPUTE";
    case 64: return "GPU_MEDIA"; case 128: return "VRAM"; case 256: return "ADM"; default: return "?";
  }
}
static const char *CTRL_NAME(int c) {
  switch (c) {
    case 1: return "VF"; case 2: return "FREQ_OFFSET"; case 4: return "VMAX_OFFSET"; case 8: return "FREQ";
    case 16: return "VOLT_LIMIT"; case 32: return "POWER_SUSTAINED"; case 64: return "POWER_BURST";
    case 128: return "POWER_PEAK"; case 256: return "ICCMAX_LIMIT"; case 512: return "TEMP_LIMIT";
    case 1024: return "ITD_DISABLE"; case 2048: return "ACM_DISABLE"; default: return "?";
  }
}

int main(void) {
  HMODULE z = LoadLibraryA("C:\\Windows\\System32\\ze_loader.dll");
  if (!z) { printf("ze_loader missing\n"); return 1; }
  ze_result_t (*zesInit)(uint32_t) = (void *)GetProcAddress(z, "zesInit");
  ze_result_t (*zesDriverGet)(uint32_t *, ze_handle *) = (void *)GetProcAddress(z, "zesDriverGet");
  ze_result_t (*zesDeviceGet)(ze_handle, uint32_t *, ze_handle *) = (void *)GetProcAddress(z, "zesDeviceGet");
  ze_result_t (*domMask)(ze_handle, uint32_t *) = (void *)GetProcAddress(z, "zesDeviceGetOverclockDomains");
  ze_result_t (*domEnum)(ze_handle, uint32_t *, ze_handle *) = (void *)GetProcAddress(z, "zesDeviceEnumOverclockDomains");
  ze_result_t (*domProps)(ze_handle, oc_props_t *) = (void *)GetProcAddress(z, "zesOverclockGetDomainProperties");
  ze_result_t (*ctrlMask)(ze_handle, int32_t, uint32_t *) = (void *)GetProcAddress(z, "zesDeviceGetOverclockControls");
  ze_result_t (*ctrlCur)(ze_handle, int32_t, double *) = (void *)GetProcAddress(z, "zesOverclockGetControlCurrentValue");
  ze_result_t (*ctrlProp)(ze_handle, int32_t, ctrl_prop_t *) = (void *)GetProcAddress(z, "zesOverclockGetDomainControlProperties");
  ze_result_t (*freqCaps)(ze_handle, freq_oc_caps_t *) = (void *)GetProcAddress(z, "zesFrequencyOcGetCapabilities");

  if (!zesInit || zesInit(0) != 0) { printf("zesInit failed\n"); return 1; }
  uint32_t dn = 0;
  ze_handle drv[4] = {0};
  if (zesDriverGet(&dn, NULL) != 0 || dn == 0) { printf("no drivers\n"); return 1; }
  if (dn > 4) dn = 4;
  zesDriverGet(&dn, drv);
  uint32_t devn = 0;
  ze_handle dev[8] = {0};
  zesDeviceGet(drv[0], &devn, NULL);
  if (devn == 0) { printf("no devices\n"); return 1; }
  if (devn > 8) devn = 8;
  zesDeviceGet(drv[0], &devn, dev);

  freq_oc_caps_t caps; memset(&caps, 0, sizeof caps); caps.stype = 0x1c;
  if (freqCaps && freqCaps(dev[0], &caps) == 0)
    printf("freq OC caps: oc=%u ibu=%u iccMaxSupported=%u\n", caps.isOcSupported, caps.isHighVoltModeCapable, caps.isIccMaxSupported);

  uint32_t mask = 0;
  ze_result_t r = domMask ? domMask(dev[0], &mask) : -1;
  printf("zesDeviceGetOverclockDomains: r=0x%x mask=0x%x\n", (unsigned)r, mask);
  for (int b = 0; b < 32; b++) if (mask & (1u << b)) printf("  domain bit %d = %s\n", b, DOMAIN_NAME(1u << b));

  uint32_t ocn = 0;
  ze_handle doms[8] = {0};
  r = domEnum ? domEnum(dev[0], &ocn, NULL) : -1;
  printf("zesDeviceEnumOverclockDomains: r=0x%x count=%u\n", (unsigned)r, ocn);
  if (r != 0 || ocn == 0) return 0;
  if (ocn > 8) ocn = 8;
  domEnum(dev[0], &ocn, doms);
  for (uint32_t i = 0; i < ocn; i++) {
    oc_props_t p; memset(&p, 0, sizeof p); p.stype = 0x0b /*ZES_STRUCTURE_TYPE_OVERCLOCK_PROPERTIES*/;
    r = domProps ? domProps(doms[i], &p) : -1;
    printf("\n[%u] domain r=0x%x type=%d (%s) controls=0x%x vfProg=%u vfPoints=%u\n",
           i, (unsigned)r, p.domainType, DOMAIN_NAME((uint32_t)p.domainType), p.AvailableControls, p.VFProgramType, p.NumberOfVFPoints);
    for (int c = 1; c <= 2048; c <<= 1) {
      if (!(p.AvailableControls & (uint32_t)c)) continue;
      double cur = -1; ctrl_prop_t cp; memset(&cp, 0, sizeof cp);
      ze_result_t rc = ctrlCur ? ctrlCur(doms[i], c, &cur) : -1;
      ze_result_t rp = ctrlProp ? ctrlProp(doms[i], c, &cp) : -1;
      printf("   %-16s cur=%12.3f  r=0x%x   min=%.3f max=%.3f step=%.3f def=%.3f (r=0x%x)\n",
             CTRL_NAME(c), cur, (unsigned)rc, cp.MinValue, cp.MaxValue, cp.StepValue, cp.DefaultValue, (unsigned)rp);
    }
  }
  return 0;
}
