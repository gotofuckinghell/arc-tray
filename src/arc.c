// arc.c — hardware layer: IGCL (IntelControlLib) + Level Zero Sysman.
// Read + apply for power limit, core offset, temperature limit, fan.
#define WIN32_LEAN_AND_MEAN
#include "arc.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")

// The bundled 2023 IGCL runtime is unsigned (verified: no Authenticode signature), and
// it is loaded into an elevated process. Pin its SHA-256 so a swapped file cannot become
// code execution; ARCTRAY_ALLOW_UNSIGNED=1 is the deliberate escape hatch.
#define EXT_DLL_SHA256 "B1D0FAD73F7A3ED2DD417665A45E887D9159CD388F329C4A54E4672B198297EB"

static int file_sha256_hex(const char *path, char out[65]) {
  HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL, NULL);
  if (f == INVALID_HANDLE_VALUE) return 0;
  BCRYPT_ALG_HANDLE alg = NULL; BCRYPT_HASH_HANDLE h = NULL;
  unsigned char digest[32];
  int ok = 0;
  if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, NULL, 0) != 0) goto done;
  if (BCryptCreateHash(alg, &h, NULL, 0, NULL, 0, 0) != 0) goto done;
  unsigned char buf[65536]; DWORD got = 0;
  for (;;) {
    if (!ReadFile(f, buf, sizeof buf, &got, NULL) || got == 0) break;
    if (BCryptHashData(h, buf, got, 0) != 0) goto done;
  }
  if (BCryptFinishHash(h, digest, sizeof digest, 0) != 0) goto done;
  for (int i = 0; i < 32; i++) snprintf(out + i * 2, 3, "%02X", digest[i]);
  out[64] = 0;
  ok = 1;
done:
  if (h) BCryptDestroyHash(h);
  if (alg) BCryptCloseAlgorithmProvider(alg, 0);
  CloseHandle(f);
  return ok;
}

#define CTL_SUCCESS 0

const char *arc_result_name(uint32_t r) {
  switch (r) {
    case 0x0: return "SUCCESS";
    case 0x40000001: return "ERROR_NOT_SUPPORTED";
    case 0x40000002: return "ERROR_DEVICE_NOT_AVAILABLE";
    case 0x40000003: return "ERROR_UNKNOWN";
    case 0x40000004: return "ERROR_INVALID_ARGUMENT";
    case 0x44000001: return "ERROR_OC_NOT_SUPPORTED";
    case 0x44000002: return "ERROR_OC_INSUFFICIENT_PERMISSIONS";
    case 0x44000003: return "ERROR_OC_NOT_AVAILABLE";
    case 0x44000004: return "ERROR_OC_INVALID_SETTING";
    case 0x44000005: return "ERROR_OC_OUT_OF_RANGE";
    case 0x44000006: return "ERROR_OC_WAIVER_NOT_SET";
    case 0x44000007: return "ERROR_OC_WAIVER_ALREADY_SET";
    case 0x44000008: return "ERROR_OC_INVALID_POWER";
    case 0x44000009: return "ERROR_OC_INVALID_FREQUENCY";
    case 0x4400000a: return "ERROR_OC_INVALID_TEMPERATURE";
    case 0x4400000b: return "ERROR_OC_INVALID_VOLTAGE";
    case 0x4400000c: return "ERROR_OC_FAILURE";
    default: return "ERROR_OTHER";
  }
}

static int is_elevated(void);

static int find_igcl_dll(char *out, size_t cap) {
  // explicit override wins (debugging / unusual layouts)
  // An elevated process must not take a DLL path from the environment: the task's
  // env comes from the user profile, so a user-writable DLL would run as admin.
  const char *env = is_elevated() ? NULL : getenv("ARCTRAY_IGCL");
  if (env && *env && GetFileAttributesA(env) != INVALID_FILE_ATTRIBUTES) { snprintf(out, cap, "%s", env); return 1; }
  // the driver may install the runtime next to its store package or into System32 / IGS;
  // System32\ControlLib.dll is intentionally NOT used (UID-whitelisted loader)
  const char *fixed[] = {
      "C:\\Windows\\System32\\IntelControlLib.dll",
      "C:\\Program Files\\Intel\\Intel Graphics Software\\IntelControlLib.dll",
      "C:\\Program Files (x86)\\Intel\\Intel Graphics Software\\IntelControlLib.dll"};
  const char *root = "C:\\Windows\\System32\\DriverStore\\FileRepository";
  const char *prefixes[] = {"*iigd*", "*igfx*", "*igdlh*"};
  for (int k = 0; k < 3; k++) {
    char pattern[MAX_PATH];
    snprintf(pattern, sizeof pattern, "%s\\%s", root, prefixes[k]);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) continue;
    int found = 0;
    do {
      if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
      char cand[MAX_PATH];
      int w = snprintf(cand, sizeof cand, "%s\\%s\\IntelControlLib.dll", root, fd.cFileName);
      if (w < 0 || w >= (int)sizeof cand) continue;   // longer than MAX_PATH cannot be opened
      if (GetFileAttributesA(cand) != INVALID_FILE_ATTRIBUTES) { snprintf(out, cap, "%s", cand); found = 1; break; }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    if (found) return 1;
  }
  for (int i = 0; i < (int)(sizeof fixed / sizeof fixed[0]); i++) {
    if (GetFileAttributesA(fixed[i]) != INVALID_FILE_ATTRIBUTES) { snprintf(out, cap, "%s", fixed[i]); return 1; }
  }
  return 0;
}

static int is_elevated(void) {
  HMODULE sh = LoadLibraryA("shell32.dll");
  if (!sh) return 0;
  BOOL(WINAPI *isAdmin)(void) = (BOOL(WINAPI *)(void))GetProcAddress(sh, "IsUserAnAdmin");
  int r = isAdmin ? isAdmin() != 0 : 0;
  FreeLibrary(sh);
  return r;
}

#define BIND_IGCL(name, type) \
  do { a->name = (type)GetProcAddress(a->igcl, #name); } while (0)

int arc_open(ArcDev *a, char *err, size_t errcap) {
  memset(a, 0, sizeof *a);
  a->elevated = is_elevated();
  if (!find_igcl_dll(a->dll_path, sizeof a->dll_path)) {
    snprintf(err, errcap, "IntelControlLib.dll not found in DriverStore");
    return 0;
  }
  a->igcl = LoadLibraryExA(a->dll_path, NULL,
                          LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
  if (!a->igcl) { snprintf(err, errcap, "LoadLibrary(IntelControlLib) failed: %lu", GetLastError()); return 0; }

  BIND_IGCL(ctlInit, ctl_result_t(*)(ctl_init_args_t *, void **));
  BIND_IGCL(ctlClose, ctl_result_t(*)(void *));
  BIND_IGCL(ctlEnumerateDevices, ctl_result_t(*)(void *, uint32_t *, void **));
  BIND_IGCL(ctlGetDeviceProperties, ctl_result_t(*)(void *, ctl_device_adapter_properties_t *));
  BIND_IGCL(ctlOverclockGetProperties, ctl_result_t(*)(void *, ctl_oc_properties_t *));
  BIND_IGCL(ctlOverclockWaiverSet, ctl_result_t(*)(void *));
  BIND_IGCL(ctlOverclockResetToDefault, ctl_result_t(*)(void *));
  BIND_IGCL(ctlOverclockPowerLimitGetV2, ctl_result_t(*)(void *, double *));
  BIND_IGCL(ctlOverclockPowerLimitSetV2, ctl_result_t(*)(void *, double));
  BIND_IGCL(ctlOverclockGpuFrequencyOffsetGetV2, ctl_result_t(*)(void *, double *));
  BIND_IGCL(ctlOverclockGpuFrequencyOffsetSetV2, ctl_result_t(*)(void *, double));
  BIND_IGCL(ctlOverclockGpuMaxVoltageOffsetGetV2, ctl_result_t(*)(void *, double *));
  BIND_IGCL(ctlOverclockGpuMaxVoltageOffsetSetV2, ctl_result_t(*)(void *, double));
  BIND_IGCL(ctlOverclockTemperatureLimitGetV2, ctl_result_t(*)(void *, double *));
  BIND_IGCL(ctlOverclockTemperatureLimitSetV2, ctl_result_t(*)(void *, double));
  BIND_IGCL(ctlEnumFans, ctl_result_t(*)(void *, uint32_t *, void **));
  BIND_IGCL(ctlFanGetProperties, ctl_result_t(*)(void *, ctl_fan_properties_t *));
  BIND_IGCL(ctlFanGetConfig, ctl_result_t(*)(void *, ctl_fan_config_t *));
  BIND_IGCL(ctlFanGetState, ctl_result_t(*)(void *, int32_t, int32_t *));
  BIND_IGCL(ctlFanSetFixedSpeedMode, ctl_result_t(*)(void *, ctl_fan_speed_t *));
  BIND_IGCL(ctlFanSetSpeedTableMode, ctl_result_t(*)(void *, ctl_fan_speed_t *));
  BIND_IGCL(ctlFanSetDefaultMode, ctl_result_t(*)(void *));
  BIND_IGCL(ctlPowerTelemetryGetV2, ctl_result_t(*)(void *, ctl_power_telemetry_v2_t *));
  BIND_IGCL(ctlPowerTelemetryGet, ctl_result_t(*)(void *, ctl_power_telemetry_t *));
  BIND_IGCL(ctlOverclockGpuFrequencyOffsetGet, ctl_result_t(*)(void *, double *));
  BIND_IGCL(ctlOverclockGpuFrequencyOffsetSet, ctl_result_t(*)(void *, double));
  BIND_IGCL(ctlOverclockGpuVoltageOffsetGet, ctl_result_t(*)(void *, double *));
  BIND_IGCL(ctlOverclockGpuVoltageOffsetSet, ctl_result_t(*)(void *, double));
  BIND_IGCL(ctlOverclockPowerLimitGet, ctl_result_t(*)(void *, double *));
  BIND_IGCL(ctlOverclockPowerLimitSet, ctl_result_t(*)(void *, double));
  BIND_IGCL(ctlOverclockTemperatureLimitGet, ctl_result_t(*)(void *, double *));
  BIND_IGCL(ctlOverclockTemperatureLimitSet, ctl_result_t(*)(void *, double));
  if (!a->ctlInit || !a->ctlEnumerateDevices || !a->ctlGetDeviceProperties) {
    snprintf(err, errcap, "IGCL exports missing");
    return 0;
  }

  ctl_init_args_t args;
  memset(&args, 0, sizeof args);
  args.Size = sizeof args;
  args.Version = 0;
  args.AppVersion = 0x00010001;
  args.flags = 1; // CTL_INIT_FLAG_USE_LEVEL_ZERO
  ctl_result_t r = a->ctlInit(&args, &a->api);
  if (r != CTL_SUCCESS) {
    // the driver may reject a zero UID; retry with the registered Arc tool identity
    args.ApplicationUID.Data1 = 0xe8e10f95;
    args.ApplicationUID.Data2 = 0x1a70;
    args.ApplicationUID.Data3 = 0x4b27;
    uint8_t uid[8] = {0x9c, 0xcf, 0x02, 0x01, 0x02, 0x64, 0xe9, 0xc8};
    memcpy(args.ApplicationUID.Data4, uid, 8);
    r = a->ctlInit(&args, &a->api);
  }
  if (r != CTL_SUCCESS || !a->api) {
    snprintf(err, errcap, "ctlInit failed: 0x%08x %s", r, arc_result_name(r));
    return 0;
  }

  uint32_t count = 0;
  a->ctlEnumerateDevices(a->api, &count, NULL);
  if (count == 0) { snprintf(err, errcap, "no IGCL devices"); return 0; }
  void *handles[8] = {0};
  if (count > 8) count = 8;
  a->ctlEnumerateDevices(a->api, &count, handles);
  // The properties struct version is not frozen across driver branches: an older
  // stack answers ERROR_UNSUPPORTED_VERSION (seen live on the 2023 runtime), so walk
  // 3 -> 2 -> 1 instead of assuming the newest revision.
  int startver = 3;
  const char *envpv = getenv("ARCTRAY_PROPS_VERSION");
  if (envpv) startver = atoi(envpv);
  a->props_version = 0;
  for (uint32_t i = 0; i < count; i++) {
    for (int v = startver; v >= 1; v--) {
      ctl_device_adapter_properties_t p;
      memset(&p, 0, sizeof p);
      p.Size = sizeof p; p.Version = (uint8_t)v;
      if (a->ctlGetDeviceProperties(handles[i], &p) != CTL_SUCCESS) continue;
      if (p.device_type != 1 || p.pci_vendor_id != 0x8086) break;
      a->dev = handles[i];
      a->props = p;
      a->props_version = v;
      break;
    }
    if (a->dev) break;
  }
  if (!a->dev) { snprintf(err, errcap, "no Intel graphics adapter in IGCL list"); return 0; }

  memset(&a->oc, 0, sizeof a->oc);
  a->oc_version = 0;
  for (int v = 1; v >= 0; v--) {
    a->oc.Size = sizeof a->oc; a->oc.Version = (uint8_t)v;
    if (a->ctlOverclockGetProperties && a->ctlOverclockGetProperties(a->dev, &a->oc) == CTL_SUCCESS) { a->oc_version = v; break; }
  }

  // fans
  uint32_t fcount = 0;
  if (a->ctlEnumFans && a->ctlEnumFans(a->dev, &fcount, NULL) == CTL_SUCCESS && fcount > 0) {
    void *fh[4] = {0};
    uint32_t n = fcount > 4 ? 4 : fcount;
    if (a->ctlEnumFans(a->dev, &n, fh) == CTL_SUCCESS) a->fan = fh[0];
  }

  // ---- Level Zero Sysman (standalone zesInit; never zeInit first) ----
  if (getenv("ARCTRAY_NO_SYSMAN")) {
    a->ze = NULL; // debug switch: exercise the IGCL-only fallbacks
  } else {
    a->ze = LoadLibraryA("C:\\Windows\\System32\\ze_loader.dll");
    if (!a->ze) a->ze = LoadLibraryA("C:\\Program Files\\Intel\\Intel Graphics Software\\ze_loader.dll");
  }
  if (a->ze) {
    a->zesInit = (ze_result_t(*)(uint32_t))GetProcAddress(a->ze, "zesInit");
    a->zesDriverGet = (ze_result_t(*)(uint32_t *, ze_handle *))GetProcAddress(a->ze, "zesDriverGet");
    a->zesDeviceGet = (ze_result_t(*)(ze_handle, uint32_t *, ze_handle *))GetProcAddress(a->ze, "zesDeviceGet");
    a->zesDeviceEnumPowerDomains = (ze_result_t(*)(ze_handle, uint32_t *, ze_handle *))GetProcAddress(a->ze, "zesDeviceEnumPowerDomains");
    a->zesPowerGetLimits = (ze_result_t(*)(ze_handle, zes_power_sustained_limit_t *, zes_power_burst_limit_t *, zes_power_peak_limit_t *))GetProcAddress(a->ze, "zesPowerGetLimits");
    a->zesPowerSetLimits = (ze_result_t(*)(ze_handle, const zes_power_sustained_limit_t *, const zes_power_burst_limit_t *, const zes_power_peak_limit_t *))GetProcAddress(a->ze, "zesPowerSetLimits");
    if (a->zesInit && a->zesDriverGet && a->zesDeviceGet && a->zesPowerGetLimits) {
      if (a->zesInit(0) == 0) {
        uint32_t dn = 0;
        ze_handle drv[4] = {0};
        if (a->zesDriverGet(&dn, NULL) == 0 && dn > 0) {
          if (dn > 4) dn = 4;
          a->zesDriverGet(&dn, drv);
          uint32_t devs_count = 0;
          ze_handle devs[8] = {0};
          if (a->zesDeviceGet(drv[0], &devs_count, NULL) == 0 && devs_count > 0) {
            if (devs_count > 8) devs_count = 8;
            a->zesDeviceGet(drv[0], &devs_count, devs);
            for (uint32_t i = 0; i < devs_count && !a->pwr; i++) {
              ze_handle doms[8] = {0};
              uint32_t dc = 8;
              if (a->zesDeviceEnumPowerDomains && a->zesDeviceEnumPowerDomains(devs[i], &dc, doms) == 0) {
                for (uint32_t k = 0; k < dc && k < 8; k++) {
                  zes_power_sustained_limit_t s; memset(&s, 0, sizeof s);
                  zes_power_burst_limit_t b; memset(&b, 0, sizeof b);
                  zes_power_peak_limit_t p; memset(&p, 0, sizeof p);
                  if (a->zesPowerGetLimits(doms[k], &s, &b, &p) == 0) { a->pwr = doms[k]; a->ze_dev = devs[i]; break; }
                }
              }
            }
          }
        }
      }
    }
  }
  // frequency domain: the Alchemist negative-voltage surface
  if (a->ze) {
    a->zesDeviceEnumFrequencyDomains = (ze_result_t(*)(ze_handle, uint32_t *, ze_handle *))GetProcAddress(a->ze, "zesDeviceEnumFrequencyDomains");
    a->zesFrequencyOcGetCapabilities = (ze_result_t(*)(ze_handle, zes_frequency_oc_capabilities_t *))GetProcAddress(a->ze, "zesFrequencyOcGetCapabilities");
    a->zesFrequencyOcGetVoltageTarget = (ze_result_t(*)(ze_handle, double *, double *))GetProcAddress(a->ze, "zesFrequencyOcGetVoltageTarget");
    a->zesFrequencyOcSetVoltageTarget = (ze_result_t(*)(ze_handle, double, double))GetProcAddress(a->ze, "zesFrequencyOcSetVoltageTarget");
    a->zesFrequencyOcGetFrequencyTarget = (ze_result_t(*)(ze_handle, double *))GetProcAddress(a->ze, "zesFrequencyOcGetFrequencyTarget");
    a->zesFrequencyOcSetFrequencyTarget = (ze_result_t(*)(ze_handle, double))GetProcAddress(a->ze, "zesFrequencyOcSetFrequencyTarget");
    a->zesFrequencyOcSetMode = (ze_result_t(*)(ze_handle, uint32_t))GetProcAddress(a->ze, "zesFrequencyOcSetMode");
    a->zesDeviceSetOverclockWaiver = (ze_result_t(*)(ze_handle))GetProcAddress(a->ze, "zesDeviceSetOverclockWaiver");
    if (a->ze_dev && a->zesDeviceEnumFrequencyDomains) {
      ze_handle fd[8] = {0};
      uint32_t fc = 8;
      if (a->zesDeviceEnumFrequencyDomains(a->ze_dev, &fc, fd) == 0 && fc > 0) a->freq = fd[0];
    }
  }
  a->have_sysman = a->pwr != NULL;
  return 1;
}

void arc_close(ArcDev *a) {
  if (a->ext) {
    if (a->ext_close && a->ext_api) a->ext_close(a->ext_api);
    FreeLibrary(a->ext);
    a->ext = NULL;
  }
  if (a->igcl) {
    if (a->ctlClose && a->api) a->ctlClose(a->api);
    FreeLibrary(a->igcl);
  }
  if (a->ze) FreeLibrary(a->ze);
  memset(a, 0, sizeof *a);
}

static int ensure_waiver(ArcDev *a, char *msg, size_t cap) {
  if (a->waiver_set) return 1;
  if (!a->elevated) { snprintf(msg, cap, "admin rights required"); return 0; }
  if (!a->ctlOverclockWaiverSet) { snprintf(msg, cap, "ctlOverclockWaiverSet missing"); return 0; }
  ctl_result_t r = a->ctlOverclockWaiverSet(a->dev);
  if (r != CTL_SUCCESS && r != 0x44000007 /*already set*/) {
    snprintf(msg, cap, "waiver failed: 0x%08x %s", r, arc_result_name(r));
    return 0;
  }
  a->waiver_set = 1;
  return 1;
}

static double sane(double v) { return (isfinite(v) && fabs(v) < 1e9) ? v : NAN; }

int arc_read(ArcDev *a, ArcState *st) {
  memset(st, 0, sizeof *st);
  snprintf(st->name, sizeof st->name, "%s", a->props.name);
  st->bus = a->props.adapter_bdf.bus;
  st->dev = a->props.adapter_bdf.device;
  st->fn = a->props.adapter_bdf.function;
  st->elevated = a->elevated;
  st->have_sysman = a->have_sysman;

  st->pl_min = a->oc.powerLimit.min; st->pl_max = a->oc.powerLimit.max; st->pl_def = a->oc.powerLimit.Default;
  st->core_min = a->oc.gpuFrequencyOffset.min; st->core_max = a->oc.gpuFrequencyOffset.max;
  st->volt_min = a->oc.gpuVoltageOffset.min; st->volt_max = a->oc.gpuVoltageOffset.max; st->volt_def = a->oc.gpuVoltageOffset.Default;
  st->temp_min = a->oc.temperatureLimit.min; st->temp_max = a->oc.temperatureLimit.max; st->temp_def = a->oc.temperatureLimit.Default;

  double v;
  v = NAN;
  if (a->ctlOverclockPowerLimitGetV2 && a->ctlOverclockPowerLimitGetV2(a->dev, &v) == 0) st->pl_igcl = sane(v);
  else if (a->ctlOverclockPowerLimitGet && a->ctlOverclockPowerLimitGet(a->dev, &v) == 0) st->pl_igcl = sane(v) / 1000.0; // V1 reports mW
  v = NAN;
  if (a->ctlOverclockGpuFrequencyOffsetGetV2 && a->ctlOverclockGpuFrequencyOffsetGetV2(a->dev, &v) == 0) st->core_off = sane(v);
  else if (a->ctlOverclockGpuFrequencyOffsetGet && a->ctlOverclockGpuFrequencyOffsetGet(a->dev, &v) == 0) st->core_off = sane(v);
  v = NAN;
  if (a->ctlOverclockGpuMaxVoltageOffsetGetV2 && a->ctlOverclockGpuMaxVoltageOffsetGetV2(a->dev, &v) == 0) st->volt_off = sane(v);
  else if (a->ctlOverclockGpuVoltageOffsetGet && a->ctlOverclockGpuVoltageOffsetGet(a->dev, &v) == 0) st->volt_off = sane(v);
  v = NAN;
  if (a->ctlOverclockTemperatureLimitGetV2 && a->ctlOverclockTemperatureLimitGetV2(a->dev, &v) == 0) st->temp_limit = sane(v);
  else if (a->ctlOverclockTemperatureLimitGet && a->ctlOverclockTemperatureLimitGet(a->dev, &v) == 0) st->temp_limit = sane(v);

  if (a->pwr && a->zesPowerGetLimits) {
    zes_power_sustained_limit_t s; memset(&s, 0, sizeof s);
    zes_power_burst_limit_t b; memset(&b, 0, sizeof b);
    zes_power_peak_limit_t p; memset(&p, 0, sizeof p);
    if (a->zesPowerGetLimits(a->pwr, &s, &b, &p) == 0) {
      st->pl_sustained = s.power / 1000.0;
      st->pl_burst = b.power / 1000.0;
    }
  }

  int tele_ok = 0;
  if (a->ctlPowerTelemetryGetV2) {
    ctl_power_telemetry_v2_t t; memset(&t, 0, sizeof t);
    t.Size = sizeof t; t.Version = 2;
    if (a->ctlPowerTelemetryGetV2(a->dev, &t) == 0) {
      if (t.gpuCurrentClockFrequency.bSupported) st->gpu_mhz = t.gpuCurrentClockFrequency.value;
      if (t.gpuCurrentTemperature.bSupported) st->gpu_temp = t.gpuCurrentTemperature.value;
      if (t.vramCurrentClockFrequency.bSupported) st->vram_mhz = t.vramCurrentClockFrequency.value;
      if (t.vramCurrentEffectiveFrequency.bSupported) st->vram_eff_mhz = t.vramCurrentEffectiveFrequency.value;
      if (t.vramCurrentTemperature.bSupported) st->vram_temp = t.vramCurrentTemperature.value;
      if (t.fanSpeed[0].bSupported) st->fan_pct = (int)(t.fanSpeed[0].value + 0.5);
      tele_ok = 1;
    }
  }
  if (!tele_ok && a->ctlPowerTelemetryGet) { // V1 on older stacks (same field layout up to vramCurrentTemperature)
    ctl_power_telemetry_t t; memset(&t, 0, sizeof t);
    t.Size = sizeof t; t.Version = 1;
    if (a->ctlPowerTelemetryGet(a->dev, &t) == 0) {
      if (t.gpuCurrentClockFrequency.bSupported) st->gpu_mhz = t.gpuCurrentClockFrequency.value;
      if (t.gpuCurrentTemperature.bSupported) st->gpu_temp = t.gpuCurrentTemperature.value;
      if (t.vramCurrentClockFrequency.bSupported) st->vram_mhz = t.vramCurrentClockFrequency.value;
      if (t.vramCurrentEffectiveFrequency.bSupported) st->vram_eff_mhz = t.vramCurrentEffectiveFrequency.value;
      if (t.vramCurrentTemperature.bSupported) st->vram_temp = t.vramCurrentTemperature.value;
      if (t.fanSpeed[0].bSupported) st->fan_pct = (int)(t.fanSpeed[0].value + 0.5);
    }
  }
  if (a->fan && a->ctlFanGetProperties) {
    ctl_fan_properties_t fp; memset(&fp, 0, sizeof fp); fp.Size = sizeof fp; fp.Version = 0;
    if (a->ctlFanGetProperties(a->fan, &fp) == 0) {
      st->fan_can_control = fp.canControl; st->fan_available = 1;
      st->fan_max_rpm = fp.maxRPM;
      st->fan_state_units = (fp.supportedUnits & 2) ? 1 : 0; // PERCENT when advertised
    }
    if (a->ctlFanGetState) {
      int32_t sp = -1;
      if (a->ctlFanGetState(a->fan, st->fan_state_units, &sp) == 0) st->fan_state = sp;
    }
    ctl_fan_config_t fc; memset(&fc, 0, sizeof fc); fc.Size = sizeof fc; fc.Version = 0;
    if (a->ctlFanGetConfig && a->ctlFanGetConfig(a->fan, &fc) == 0) {
      st->fan_mode = fc.mode;
      if (fc.mode == 2 && fc.speedTable.numPoints > 0) st->fan_set_pct = fc.speedTable.table[0].speed.speed;
      if (fc.mode == 1) st->fan_set_pct = fc.speedFixed.speed;
    }
  }
  return 1;
}

// --- power limit via Sysman (sustained + burst pair) ---
static int igcl_set_power(ArcDev *a, double watts, char *msg, size_t cap) {
  // Fallback for driver stacks without Level Zero Sysman: the IGCL control is capped
  // by the advertised range (A770: 115..276 W), so clamp instead of lying.
  double lo = a->oc.powerLimit.min > 0 ? a->oc.powerLimit.min : 115.0;
  double hi = a->oc.powerLimit.max > 0 ? a->oc.powerLimit.max : 276.0;
  double want = watts < lo ? lo : (watts > hi ? hi : watts);
  int v1 = 0;
  ctl_result_t r = 0x40000015;
  if (a->ctlOverclockPowerLimitSetV2 && !getenv("ARCTRAY_V1")) r = a->ctlOverclockPowerLimitSetV2(a->dev, want);
  if ((r != CTL_SUCCESS) && a->ctlOverclockPowerLimitSet) { r = a->ctlOverclockPowerLimitSet(a->dev, want * 1000.0); v1 = (r == CTL_SUCCESS); }
  if (r != CTL_SUCCESS) { snprintf(msg, cap, "PL %.0f W (IGCL): 0x%08x %s", want, r, arc_result_name(r)); return 0; }
  Sleep(400);
  double got = NAN;
  if (a->ctlOverclockPowerLimitGetV2) a->ctlOverclockPowerLimitGetV2(a->dev, &got);
  if (!isfinite(got) && a->ctlOverclockPowerLimitGet) { if (a->ctlOverclockPowerLimitGet(a->dev, &got) == 0) got /= 1000.0; }
  if (!isfinite(got)) { snprintf(msg, cap, "PL %.0f W written, readback unavailable", want); return 1; }
  if (fabs(got - want) > 1.0) { snprintf(msg, cap, "PL readback %.0f W (asked %.0f)", got, want); return 0; }
  snprintf(msg, cap, "PL %.0f W ok (IGCL%s)", got, v1 ? ", V1" : "");
  return 1;
}

int arc_set_power(ArcDev *a, double watts, char *msg, size_t cap) {
  if (!a->elevated) { snprintf(msg, cap, "нужны права администратора"); return 0; }
  if (!a->pwr || !a->zesPowerGetLimits || !a->zesPowerSetLimits) return igcl_set_power(a, watts, msg, cap);
  zes_power_sustained_limit_t s; memset(&s, 0, sizeof s);
  zes_power_burst_limit_t b; memset(&b, 0, sizeof b);
  zes_power_peak_limit_t p; memset(&p, 0, sizeof p);
  if (a->zesPowerGetLimits(a->pwr, &s, &b, &p) != 0) { snprintf(msg, cap, "zesPowerGetLimits failed"); return 0; }
  int32_t mw = (int32_t)lround(watts * 1000.0);
  s.enabled = 1; s.power = mw; if (s.interval <= 0) s.interval = 20800;
  b.enabled = 1; b.power = mw;
  ze_result_t r = a->zesPowerSetLimits(a->pwr, &s, &b, &p);
  if (r != 0) { snprintf(msg, cap, "zesPowerSetLimits: 0x%08x", (uint32_t)r); return 0; }
  Sleep(400);
  zes_power_sustained_limit_t s2 = {0}; zes_power_burst_limit_t b2 = {0}; zes_power_peak_limit_t p2 = {0};
  if (a->zesPowerGetLimits(a->pwr, &s2, &b2, &p2) != 0) { snprintf(msg, cap, "прочитано не подтверждено"); return 0; }
  double got = s2.power / 1000.0;
  if (fabs(got - watts) > 0.6) { snprintf(msg, cap, "драйвер оставил %.0f W (запрошено %.0f)", got, watts); return 0; }
  snprintf(msg, cap, "PL %.0f W ✓", got);
  return 1;
}

int arc_set_core(ArcDev *a, double mhz, char *msg, size_t cap) {
  if (!ensure_waiver(a, msg, cap)) return 0;
  ctl_result_t r = 0x40000015;
  int v1 = 0;
  if (a->ctlOverclockGpuFrequencyOffsetSetV2 && !getenv("ARCTRAY_V1")) r = a->ctlOverclockGpuFrequencyOffsetSetV2(a->dev, mhz);
  if ((r != CTL_SUCCESS) && a->ctlOverclockGpuFrequencyOffsetSet) { r = a->ctlOverclockGpuFrequencyOffsetSet(a->dev, mhz); v1 = (r == CTL_SUCCESS); }
  if (r != CTL_SUCCESS) { snprintf(msg, cap, "set core: 0x%08x %s", r, arc_result_name(r)); return 0; }
  Sleep(400);
  double got = NAN;
  if (a->ctlOverclockGpuFrequencyOffsetGetV2) a->ctlOverclockGpuFrequencyOffsetGetV2(a->dev, &got);
  if (!isfinite(got) && a->ctlOverclockGpuFrequencyOffsetGet) a->ctlOverclockGpuFrequencyOffsetGet(a->dev, &got);
  if (fabs(got - mhz) > 12.0) { snprintf(msg, cap, "readback %.0f MHz (asked %.0f)", got, mhz); return 0; }
  snprintf(msg, cap, "core %+.0f MHz ok%s", got, v1 ? " (V1)" : "");
  return 1;
}

int arc_set_temp(ArcDev *a, double celsius, char *msg, size_t cap) {
  if (!ensure_waiver(a, msg, cap)) return 0;
  ctl_result_t r = 0x40000015;
  int v1 = 0;
  if (a->ctlOverclockTemperatureLimitSetV2 && !getenv("ARCTRAY_V1")) r = a->ctlOverclockTemperatureLimitSetV2(a->dev, celsius);
  if ((r != CTL_SUCCESS) && a->ctlOverclockTemperatureLimitSet) { r = a->ctlOverclockTemperatureLimitSet(a->dev, celsius); v1 = (r == CTL_SUCCESS); }
  if (r != CTL_SUCCESS) { snprintf(msg, cap, "set temp: 0x%08x %s", r, arc_result_name(r)); return 0; }
  Sleep(400);
  double got = NAN;
  if (a->ctlOverclockTemperatureLimitGetV2) a->ctlOverclockTemperatureLimitGetV2(a->dev, &got);
  if (!isfinite(got) && a->ctlOverclockTemperatureLimitGet) a->ctlOverclockTemperatureLimitGet(a->dev, &got);
  if (isfinite(got) && fabs(got - celsius) > 1.5) { snprintf(msg, cap, "readback %.0f C (asked %.0f)", got, celsius); return 0; }
  snprintf(msg, cap, "temp %.0f C ok%s", isfinite(got) ? got : celsius, v1 ? " (V1)" : "");
  return 1;
}

// A770 refuses the dedicated fixed-speed write (ERROR_UNSUPPORTED_FEATURE); the
// working route is the IGS/Acer mechanism: a flat 2-point temperature table with
// the same speed at every point, written through ctlFanSetSpeedTableMode.
static int fan_write_table(ArcDev *a, int percent, int *got_pct) {
  if (!a->ctlFanSetSpeedTableMode) return 0x40000015;
  ctl_fan_speed_table_t tbl;
  memset(&tbl, 0, sizeof tbl);
  tbl.Size = sizeof tbl; tbl.Version = 0; tbl.numPoints = 2;
  const uint32_t temps[2] = {20, 100};
  for (int i = 0; i < 2; i++) {
    tbl.table[i].Size = sizeof(ctl_fan_temp_speed_t);
    tbl.table[i].Version = 0;
    tbl.table[i].temperature = temps[i];
    tbl.table[i].speed.Size = sizeof(ctl_fan_speed_t);
    tbl.table[i].speed.Version = 0;
    tbl.table[i].speed.units = 1; // PERCENT: the Arc table API's encoding
    tbl.table[i].speed.speed = percent;
  }
  ctl_result_t r = a->ctlFanSetSpeedTableMode(a->fan, (ctl_fan_speed_t *)&tbl);
  if (r != CTL_SUCCESS) return (int)r;
  if (a->ctlFanGetConfig) {
    ctl_fan_config_t cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.Size = sizeof cfg; cfg.Version = 0;
    if (a->ctlFanGetConfig(a->fan, &cfg) == CTL_SUCCESS) {
      if (got_pct) *got_pct = cfg.speedTable.table[0].speed.speed;
      if (cfg.mode != 2 || cfg.speedTable.numPoints < 2) return 0x40000003;
    }
  }
  return CTL_SUCCESS;
}

int arc_set_fan(ArcDev *a, int mode, int percent, char *msg, size_t cap) {
  if (!ensure_waiver(a, msg, cap)) return 0;
  if (!a->fan) { snprintf(msg, cap, "fan not found"); return 0; }
  if (mode == 0) {
    if (!a->ctlFanSetDefaultMode) { snprintf(msg, cap, "no ctlFanSetDefaultMode"); return 0; }
    ctl_result_t r = a->ctlFanSetDefaultMode(a->fan);
    for (int tries = 0; r != CTL_SUCCESS && tries < 3; tries++) { Sleep(150); r = a->ctlFanSetDefaultMode(a->fan); }
    if (r != CTL_SUCCESS) { snprintf(msg, cap, "fan auto: 0x%08x %s", r, arc_result_name(r)); return 0; }
    snprintf(msg, cap, "fan auto ok");
    return 1;
  }
  int got = -1;
  ctl_result_t r = 0x4000000a;
  if (a->ctlFanSetFixedSpeedMode) {
    ctl_fan_speed_t sp;
    memset(&sp, 0, sizeof sp);
    sp.Size = sizeof sp; sp.Version = 0; sp.speed = percent; sp.units = 1; // PERCENT
    r = a->ctlFanSetFixedSpeedMode(a->fan, &sp);
  }
  int via_table = 0;
  if (r == 0x4000000a || r == 0x40000007 || r == 0x40000015) { // unsupported / not available / not implemented
    r = (ctl_result_t)fan_write_table(a, percent, &got);
    via_table = 1;
  }
  if (r != CTL_SUCCESS) { snprintf(msg, cap, "fan %d%%: 0x%08x %s", percent, r, arc_result_name(r)); return 0; }
  if (got >= 0 && abs(got - percent) > 1) { snprintf(msg, cap, "fan readback %d%% (asked %d)", got, percent); return 0; }
  snprintf(msg, cap, "fan %d%% ok%s", got >= 0 ? got : percent, via_table ? " (table)" : "");
  return 1;
}

// ---------- extended 2023 runtime: temperature limit up to 115 C ----------
// The DriverStore runtime clamps the temperature limit to 90 C. The bundled
// 2023 ArcTool runtime exposes the V1 setters (power in mW, temperature in C)
// and accepts up to EXTENDED_TL_MAX_C = 115 C.
static int find_ext_dll(ArcDev *a, char *out, size_t cap) {
  const char *env = is_elevated() ? NULL : getenv("ARCTRAY_IGCL2023");
  if (env && *env && GetFileAttributesA(env) != INVALID_FILE_ATTRIBUTES) { snprintf(out, cap, "%s", env); return 1; }
  char exe[MAX_PATH];
  GetModuleFileNameA(NULL, exe, MAX_PATH);
  char *slash = strrchr(exe, '\\');
  if (slash) *slash = 0;
  const char *rel[] = {"\\IntelControlLib-2023.dll",
                       "\\vendor\\IntelControlLib-2023.dll",
                       "\\..\\arc-power\\src\\main\\backend\\igcl2023\\IntelControlLib.dll",
                       "\\..\\..\\arc-power\\src\\main\\backend\\igcl2023\\IntelControlLib.dll"};
  for (int i = 0; i < (int)(sizeof rel / sizeof rel[0]); i++) {
    char cand[MAX_PATH];
    snprintf(cand, sizeof cand, "%s%s", exe, rel[i]);
    if (GetFileAttributesA(cand) != INVALID_FILE_ATTRIBUTES) { snprintf(out, cap, "%s", cand); return 1; }
  }
  const char *fixed[] = {
      "C:\\Program Files\\Arc Power\\resources\\app.asar.unpacked\\src\\main\\backend\\igcl2023\\IntelControlLib.dll",
      "C:\\Program Files\\Arc Power\\resources\\app\\src\\main\\backend\\igcl2023\\IntelControlLib.dll"};
  for (int i = 0; i < (int)(sizeof fixed / sizeof fixed[0]); i++) {
    if (GetFileAttributesA(fixed[i]) != INVALID_FILE_ATTRIBUTES) { snprintf(out, cap, "%s", fixed[i]); return 1; }
  }
  (void)a;
  return 0;
}

int arc_ext_available(ArcDev *a) {
  if (a->ext_ready) return a->ext_dev != NULL;
  a->ext_ready = 1;
  char dll[MAX_PATH];
  if (!find_ext_dll(a, dll, sizeof dll)) return 0;
  char hash[65] = {0};
  const char *allow = getenv("ARCTRAY_ALLOW_UNSIGNED");
  int pinned = file_sha256_hex(dll, hash) && _stricmp(hash, EXT_DLL_SHA256) == 0;
  if (!pinned && !(allow && *allow && *allow != '0')) {
    fprintf(stderr, "refusing unsigned 2023 runtime %s (sha256 %s)\n", dll, hash[0] ? hash : "unreadable");
    return 0;
  }
  HMODULE h = LoadLibraryExA(dll, NULL,
                             LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
  if (!h) return 0;
  ctl_result_t (*init)(ctl_init_args_t *, void **) = (ctl_result_t(*)(ctl_init_args_t *, void **))GetProcAddress(h, "ctlInit");
  ctl_result_t (*enumd)(void *, uint32_t *, void **) = (ctl_result_t(*)(void *, uint32_t *, void **))GetProcAddress(h, "ctlEnumerateDevices");
  ctl_result_t (*props)(void *, ctl_device_adapter_properties_t *) = (ctl_result_t(*)(void *, ctl_device_adapter_properties_t *))GetProcAddress(h, "ctlGetDeviceProperties");
  ctl_result_t (*waiver)(void *) = (ctl_result_t(*)(void *))GetProcAddress(h, "ctlOverclockWaiverSet");
  ctl_result_t (*sett)(void *, double) = (ctl_result_t(*)(void *, double))GetProcAddress(h, "ctlOverclockTemperatureLimitSet");
  ctl_result_t (*gett)(void *, double *) = (ctl_result_t(*)(void *, double *))GetProcAddress(h, "ctlOverclockTemperatureLimitGet");
  ctl_result_t (*cls)(void *) = (ctl_result_t(*)(void *))GetProcAddress(h, "ctlClose");
  if (!init || !enumd || !props || !sett) { FreeLibrary(h); return 0; }
  ctl_init_args_t args;
  memset(&args, 0, sizeof args);
  args.Size = sizeof args; args.Version = 0; args.AppVersion = 0x00010000; args.flags = 1;
  void *api = NULL;
  ctl_result_t r = init(&args, &api);
  if (r != CTL_SUCCESS) {
    args.ApplicationUID.Data1 = 0xe8e10f95;
    args.ApplicationUID.Data2 = 0x1a70;
    args.ApplicationUID.Data3 = 0x4b27;
    uint8_t uid[8] = {0x9c, 0xcf, 0x02, 0x01, 0x02, 0x64, 0xe9, 0xc8};
    memcpy(args.ApplicationUID.Data4, uid, 8);
    r = init(&args, &api);
  }
  if (r != CTL_SUCCESS || !api) { FreeLibrary(h); return 0; }
  uint32_t count = 0;
  enumd(api, &count, NULL);
  if (!count) { if (cls) cls(api); FreeLibrary(h); return 0; }
  void *handles[8] = {0};
  if (count > 8) count = 8;
  enumd(api, &count, handles);
  void *dev = NULL, *fallback = NULL;
  for (uint32_t i = 0; i < count; i++) {
    ctl_device_adapter_properties_t p;
    memset(&p, 0, sizeof p); p.Size = sizeof p; p.Version = 3;
    // The 2023 runtime rejects the modern properties version (ERROR_UNSUPPORTED_VERSION);
    // IGCL only enumerates Intel adapters, so the first handle is the right fallback.
    if (props(handles[i], &p) != CTL_SUCCESS) { if (!fallback) fallback = handles[i]; continue; }
    if (p.pci_vendor_id != 0x8086) continue;
    if (!fallback) fallback = handles[i];
    if (p.adapter_bdf.bus == a->props.adapter_bdf.bus &&
        p.adapter_bdf.device == a->props.adapter_bdf.device &&
        p.adapter_bdf.function == a->props.adapter_bdf.function) { dev = handles[i]; break; }
  }
  if (!dev) dev = fallback;
  if (!dev) { if (cls) cls(api); FreeLibrary(h); return 0; }
  if (waiver) { ctl_result_t wr = waiver(dev); if (wr != CTL_SUCCESS && wr != 0x44000007) { if (cls) cls(api); FreeLibrary(h); return 0; } }
  a->ext = h; a->ext_api = api; a->ext_dev = dev;
  a->ext_set_temp = sett; a->ext_get_temp = gett; a->ext_close = cls;
  return 1;
}

int arc_set_temp_ext(ArcDev *a, double celsius, char *msg, size_t cap) {
  if (!a->elevated) { snprintf(msg, cap, "admin rights required"); return 0; }
  if (!arc_ext_available(a)) { snprintf(msg, cap, "extended 2023 IGCL runtime not found"); return 0; }
  ctl_result_t r = a->ext_set_temp(a->ext_dev, celsius);
  if (r != CTL_SUCCESS) { snprintf(msg, cap, "ext temp: 0x%08x %s", r, arc_result_name(r)); return 0; }
  Sleep(400);
  double got = NAN;
  if (a->ctlOverclockTemperatureLimitGetV2) a->ctlOverclockTemperatureLimitGetV2(a->dev, &got);
  if (!isfinite(got) && a->ext_get_temp) a->ext_get_temp(a->ext_dev, &got);
  if (isfinite(got) && fabs(got - celsius) > 1.5) { snprintf(msg, cap, "temp readback %.0f C (asked %.0f)", got, celsius); return 0; }
  snprintf(msg, cap, "temp %.0f C ok (ext)", isfinite(got) ? got : celsius);
  return 1;
}

// ---------- voltage offset (IGCL positive, Sysman negative/undervolt) ----------
static double ze_scale_of(ArcDev *a) {
  if (!a->freq || !a->zesFrequencyOcGetCapabilities) return 1000.0; // A770 ABI: mV
  zes_frequency_oc_capabilities_t caps;
  memset(&caps, 0, sizeof caps);
  caps.stype = 0x1c;
  if (a->zesFrequencyOcGetCapabilities(a->freq, &caps) != 0) return 1000.0;
  double probe[4] = {caps.maxFactoryDefaultVoltage, caps.maxOcVoltage, caps.minOcVoltageOffset, caps.maxOcVoltageOffset};
  for (int i = 0; i < 4; i++) if (isfinite(probe[i]) && fabs(probe[i]) > 10.0) return 1000.0;
  return 1.0;
}

int arc_set_volt(ArcDev *a, int mv, char *msg, size_t cap) {
  if (!a->elevated) { snprintf(msg, cap, "admin rights required"); return 0; }
  if (!ensure_waiver(a, msg, cap)) return 0;
  const char *envs = getenv("ARCTRAY_VOLT_SYSMAN");
  int forced = envs && *envs && *envs != '0';

  if (!forced && mv >= 0) { // positive offset lives on the IGCL surface
    if (!a->ctlOverclockGpuMaxVoltageOffsetSetV2 && !a->ctlOverclockGpuVoltageOffsetSet) {
      snprintf(msg, cap, "no voltage-offset setter");
      return 0;
    }
    // Clearing this control (0 mV) resets EVERY IGCL overclock control on the A770,
    // so remember the live PL / core offset and put them back afterwards.
    double keepPL = 0, keepCore = 0, keepTemp = 0;
    if (mv == 0) {
      if (a->ctlOverclockTemperatureLimitGetV2) a->ctlOverclockTemperatureLimitGetV2(a->dev, &keepTemp);
      if (a->pwr && a->zesPowerGetLimits) {
        zes_power_sustained_limit_t s; memset(&s, 0, sizeof s);
        zes_power_burst_limit_t b; memset(&b, 0, sizeof b);
        zes_power_peak_limit_t p; memset(&p, 0, sizeof p);
        if (a->zesPowerGetLimits(a->pwr, &s, &b, &p) == 0) keepPL = s.power / 1000.0;
      }
      if (a->ctlOverclockGpuFrequencyOffsetGetV2) a->ctlOverclockGpuFrequencyOffsetGetV2(a->dev, &keepCore);
    }
    double want = mv / 1000.0;
    ctl_result_t r = 0x40000015;
    int v1 = 0;
    if (a->ctlOverclockGpuMaxVoltageOffsetSetV2 && !getenv("ARCTRAY_V1")) r = a->ctlOverclockGpuMaxVoltageOffsetSetV2(a->dev, want);
    if ((r != CTL_SUCCESS) && a->ctlOverclockGpuVoltageOffsetSet) { r = a->ctlOverclockGpuVoltageOffsetSet(a->dev, want); v1 = (r == CTL_SUCCESS); }
    if (r != CTL_SUCCESS) { snprintf(msg, cap, "volt %+d mV: 0x%08x %s", mv, r, arc_result_name(r)); return 0; }
    Sleep(350);
    char extra[128] = {0};
    if (mv == 0) {
      char m2[96];
      if (keepPL > 0) { if (arc_set_power(a, keepPL, m2, sizeof m2)) strncat_s(extra, sizeof extra, " PL restored", _TRUNCATE); }
      if (keepCore > 0) { if (arc_set_core(a, keepCore, m2, sizeof m2)) strncat_s(extra, sizeof extra, " core restored", _TRUNCATE); }
      if (keepTemp > 0) { if (arc_set_temp(a, keepTemp, m2, sizeof m2)) strncat_s(extra, sizeof extra, " temp restored", _TRUNCATE); }
    }
    double got = NAN;
    if (a->ctlOverclockGpuMaxVoltageOffsetGetV2) a->ctlOverclockGpuMaxVoltageOffsetGetV2(a->dev, &got);
    if (!isfinite(got) && a->ctlOverclockGpuVoltageOffsetGet) a->ctlOverclockGpuVoltageOffsetGet(a->dev, &got);
    if (isfinite(got) && fabs(got - want) > 0.006) { snprintf(msg, cap, "volt readback %+.0f mV (asked %+d)", got * 1000, mv); return 0; }
    snprintf(msg, cap, "volt %+d mV ok%s%s", (int)lround((isfinite(got) ? got : want) * 1000), extra, v1 ? " (V1)" : "");
    return 1;
  }

  // Negative offsets need the Sysman V/F target pair. Verified on the A770:
  // writing mode=2 + freqTarget + voltageTarget rewrites the operating point, so
  // the IGCL core offset is re-based (+75 -> +224 MHz) and a phantom base offset
  // appears (-31 mV). Not shipped until the V/F mapping is done properly.
  if (mv < 0 && !forced) {
    snprintf(msg, cap, "undervolt needs the Sysman V/F pair; rerun with --sysman to try it");
    return 0;
  }
  // The Sysman path rewrites the whole operating point, so remember what the IGCL
  // controls held and put them back after the write.
  double snapPL = 0, snapCore = 0, snapTemp = 0;
  if (forced) {
    if (a->pwr && a->zesPowerGetLimits) {
      zes_power_sustained_limit_t s; memset(&s, 0, sizeof s);
      zes_power_burst_limit_t b; memset(&b, 0, sizeof b);
      zes_power_peak_limit_t p; memset(&p, 0, sizeof p);
      if (a->zesPowerGetLimits(a->pwr, &s, &b, &p) == 0) snapPL = s.power / 1000.0;
    }
    if (a->ctlOverclockGpuFrequencyOffsetGetV2) a->ctlOverclockGpuFrequencyOffsetGetV2(a->dev, &snapCore);
    if (a->ctlOverclockTemperatureLimitGetV2) a->ctlOverclockTemperatureLimitGetV2(a->dev, &snapTemp);
  }
  // negative offset: Level Zero Sysman legacy frequency OC surface
  if (!a->freq || !a->zesFrequencyOcGetVoltageTarget || !a->zesFrequencyOcSetVoltageTarget ||
      !a->zesFrequencyOcSetFrequencyTarget || !a->zesFrequencyOcGetFrequencyTarget || !a->zesFrequencyOcSetMode) {
    snprintf(msg, cap, "sysman frequency domain unavailable");
    return 0;
  }
  if (mv < -200) mv = -200;
  if (mv > 500) mv = 500; // experimental ceiling; the driver clamps what it wants
  double scale = ze_scale_of(a);
  double rawTgt = 0, rawOff = 0;
  ze_result_t r = a->zesFrequencyOcGetVoltageTarget(a->freq, &rawTgt, &rawOff);
  if (r != 0) { snprintf(msg, cap, "zesFrequencyOcGetVoltageTarget: 0x%08x", (uint32_t)r); return 0; }
  double targetV = fabs(rawTgt) > 10.0 ? rawTgt / 1000.0 : rawTgt;
  double freqTgt = 0;
  if (a->zesFrequencyOcGetFrequencyTarget(a->freq, &freqTgt) != 0) freqTgt = 0;
  if (!(targetV > 0) || !(freqTgt > 0)) {
    // After an IGCL clear the driver reports no target; fall back to the factory
    // defaults the capability block advertises (Alchemist: mV + MHz).
    zes_frequency_oc_capabilities_t caps;
    memset(&caps, 0, sizeof caps);
    caps.stype = 0x1c;
    if (a->zesFrequencyOcGetCapabilities && a->zesFrequencyOcGetCapabilities(a->freq, &caps) == 0) {
      if (!(targetV > 0) && caps.maxFactoryDefaultVoltage > 0) {
        targetV = caps.maxFactoryDefaultVoltage > 10.0 ? caps.maxFactoryDefaultVoltage / 1000.0 : caps.maxFactoryDefaultVoltage;
      }
      if (!(freqTgt > 0) && caps.maxFactoryDefaultFrequency > 0) freqTgt = caps.maxFactoryDefaultFrequency;
    }
  }
  if (!(targetV > 0)) { snprintf(msg, cap, "no voltage target from the driver"); return 0; }
  if (!(freqTgt > 0)) { snprintf(msg, cap, "no core-frequency target from the driver"); return 0; }
  double wantOffV = mv / 1000.0;
  double nativeTgt = scale == 1000.0 ? targetV * 1000.0 : targetV;
  double nativeOff = scale == 1000.0 ? round(wantOffV * 1000.0) : wantOffV;
  if (a->zesDeviceSetOverclockWaiver) a->zesDeviceSetOverclockWaiver(a->ze_dev); // informational
  r = a->zesFrequencyOcSetMode(a->freq, 2);
  if (r != 0) { snprintf(msg, cap, "zesFrequencyOcSetMode(2): 0x%08x", (uint32_t)r); return 0; }
  r = a->zesFrequencyOcSetFrequencyTarget(a->freq, freqTgt);
  if (r != 0) { snprintf(msg, cap, "zesFrequencyOcSetFrequencyTarget: 0x%08x", (uint32_t)r); return 0; }
  r = a->zesFrequencyOcSetVoltageTarget(a->freq, nativeTgt, nativeOff);
  if (r != 0) { snprintf(msg, cap, "zesFrequencyOcSetVoltageTarget: 0x%08x", (uint32_t)r); return 0; }
  Sleep(350);
  double chkTgt = 0, chkOff = 0;
  if (a->zesFrequencyOcGetVoltageTarget(a->freq, &chkTgt, &chkOff) == 0) {
    double offV = fabs(chkOff) > 10.0 ? chkOff / 1000.0 : chkOff;
    if (fabs(offV - wantOffV) > 0.0015) { snprintf(msg, cap, "volt readback %+.0f mV (asked %+d)", offV * 1000, mv); return 0; }
    if (forced) {
      char m2[96], extra[96] = {0};
      if (snapPL > 0 && arc_set_power(a, snapPL, m2, sizeof m2)) strncat_s(extra, sizeof extra, " PL", _TRUNCATE);
      if (snapCore != 0 && arc_set_core(a, snapCore, m2, sizeof m2)) strncat_s(extra, sizeof extra, " core", _TRUNCATE);
      if (snapTemp > 0 && arc_set_temp(a, snapTemp, m2, sizeof m2)) strncat_s(extra, sizeof extra, " temp", _TRUNCATE);
      if (extra[0]) { snprintf(msg, cap, "volt %+.0f mV ok (sysman), restored:%s", offV * 1000, extra); return 1; }
    }
    snprintf(msg, cap, "volt %+.0f mV ok (sysman)", offV * 1000);
    return 1;
  }
  snprintf(msg, cap, "volt %+d mV written, readback unavailable", mv);
  return 1;
}
