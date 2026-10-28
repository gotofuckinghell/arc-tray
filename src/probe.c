// Minimal IGCL (IntelControlLib) probe — read-only.
// Loads the real runtime DLL from the DriverStore, enumerates adapters,
// prints identity + overclock capability ranges + live values.
// Layouts ported from Arc-Power src/main/backend/igcl-bindings.js (GPL-2.0).
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stddef.h>

typedef uint32_t ctl_result_t;

typedef struct { uint32_t Data1; uint16_t Data2; uint16_t Data3; uint8_t Data4[8]; } ctl_application_id_t;
typedef struct {
  uint32_t Size; uint8_t Version; uint32_t AppVersion; uint32_t flags;
  uint32_t SupportedVersion; ctl_application_id_t ApplicationUID;
} ctl_init_args_t;

typedef struct { uint64_t major_version, minor_version, build_number; } ctl_firmware_version_t;
typedef struct { uint8_t bus, device, function; } ctl_adapter_bdf_t;

typedef struct {
  uint32_t Size; uint8_t Version;
  void *pDeviceID; uint32_t device_id_size; int32_t device_type;
  uint32_t supported_subfunction_flags; uint64_t driver_version;
  ctl_firmware_version_t firmware_version;
  uint32_t pci_vendor_id, pci_device_id, rev_id;
  uint32_t num_eus_per_sub_slice, num_sub_slices_per_slice, num_slices;
  char name[100];
  uint32_t graphics_adapter_properties, Frequency;
  uint16_t pci_subsys_id, pci_subsys_vendor_id;
  ctl_adapter_bdf_t adapter_bdf;
  uint32_t num_xe_cores;
  uint8_t reserved[108];
} ctl_device_adapter_properties_t;

typedef struct {
  uint8_t bSupported, bRelative, bReference;
  int32_t units; double min, max, step, Default, reference;
} ctl_oc_control_info_t;

typedef struct {
  uint32_t Size; uint8_t Version; uint8_t bSupported;
  ctl_oc_control_info_t gpuFrequencyOffset, gpuVoltageOffset;
  ctl_oc_control_info_t vramFrequencyOffset, vramVoltageOffset;
  ctl_oc_control_info_t powerLimit, temperatureLimit, vramMemSpeedLimit;
  ctl_oc_control_info_t gpuVFCurveVoltageLimit, gpuVFCurveFrequencyLimit;
} ctl_oc_properties_t;

_Static_assert(sizeof(ctl_init_args_t) == 36, "init");
_Static_assert(sizeof(ctl_device_adapter_properties_t) == 320, "props");
_Static_assert(sizeof(ctl_oc_control_info_t) == 48, "ocinfo");
_Static_assert(sizeof(ctl_oc_properties_t) == 440, "ocprops");

typedef ctl_result_t (*fn_init)(ctl_init_args_t *, void **);
typedef ctl_result_t (*fn_close)(void *);
typedef ctl_result_t (*fn_enum)(void *, uint32_t *, void **);
typedef ctl_result_t (*fn_props)(void *, ctl_device_adapter_properties_t *);
typedef ctl_result_t (*fn_ocprops)(void *, ctl_oc_properties_t *);
typedef ctl_result_t (*fn_getd)(void *, double *);
typedef ctl_result_t (*fn_setd)(void *, double);

static const char *RESULT_NAME(uint32_t r) {
  switch (r) {
    case 0: return "SUCCESS";
    case 1: return "ERROR_UNINITIALIZED";
    case 2: return "ERROR_CALL_NOT_IMPLEMENTED";
    case 3: return "ERROR_INVALID_PARAMETER";
    case 4: return "ERROR_OUT_OF_HOST_MEMORY";
    case 0x40000001: return "ERROR_NOT_SUPPORTED";
    case 0x40000002: return "ERROR_DEVICE_NOT_AVAILABLE";
    case 0x40000003: return "ERROR_UNKNOWN";
    case 0x44000001: return "OC_NOT_SUPPORTED";
    default: return "OTHER";
  }
}

// find the runtime DLL in the DriverStore
static int find_dll(char *out, size_t cap) {
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
      snprintf(cand, sizeof cand, "%s\\%s\\IntelControlLib.dll", root, fd.cFileName);
      if (GetFileAttributesA(cand) != INVALID_FILE_ATTRIBUTES) { snprintf(out, cap, "%s", cand); found = 1; break; }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    if (found) return 1;
  }
  return 0;
}

int main(void) {
  char dll[MAX_PATH] = {0};
  if (!find_dll(dll, sizeof dll)) { printf("IntelControlLib.dll not found\n"); return 1; }
  printf("DLL: %s\n", dll);
  HMODULE lib = LoadLibraryA(dll);
  if (!lib) { printf("LoadLibrary failed: %lu\n", GetLastError()); return 1; }

  fn_init ctlInit = (fn_init)GetProcAddress(lib, "ctlInit");
  fn_close ctlClose = (fn_close)GetProcAddress(lib, "ctlClose");
  fn_enum ctlEnumDevices = (fn_enum)GetProcAddress(lib, "ctlEnumerateDevices");
  fn_props ctlProps = (fn_props)GetProcAddress(lib, "ctlGetDeviceProperties");
  fn_ocprops ctlOcProps = (fn_ocprops)GetProcAddress(lib, "ctlOverclockGetProperties");
  fn_getd plGet = (fn_getd)GetProcAddress(lib, "ctlOverclockPowerLimitGetV2");
  fn_getd fqGet = (fn_getd)GetProcAddress(lib, "ctlOverclockGpuFrequencyOffsetGetV2");
  fn_getd voGet = (fn_getd)GetProcAddress(lib, "ctlOverclockGpuMaxVoltageOffsetGetV2");
  fn_getd tlGet = (fn_getd)GetProcAddress(lib, "ctlOverclockTemperatureLimitGetV2");
  fn_getd vlGet = (fn_getd)GetProcAddress(lib, "ctlOverclockVramMemSpeedLimitGetV2");
  if (!ctlInit || !ctlEnumDevices || !ctlProps) { printf("symbols missing\n"); return 1; }

  ctl_init_args_t args; memset(&args, 0, sizeof args);
  args.Size = sizeof args; args.Version = 0; args.AppVersion = 0x00010001;
  args.flags = 1; // CTL_INIT_FLAG_USE_LEVEL_ZERO
  void *api = NULL;
  ctl_result_t r = ctlInit(&args, &api);
  printf("ctlInit -> 0x%08x %s\n", r, RESULT_NAME(r));
  if (r != 0 || !api) return 1;

  uint32_t count = 0;
  r = ctlEnumDevices(api, &count, NULL);
  printf("enumerate(count) -> 0x%08x count=%u\n", r, count);
  if (r != 0 || count == 0) return 1;
  void *handles[8] = {0};
  if (count > 8) count = 8;
  r = ctlEnumDevices(api, &count, handles);
  printf("enumerate(fill) -> 0x%08x\n", r);

  for (uint32_t i = 0; i < count; i++) {
    ctl_device_adapter_properties_t p; memset(&p, 0, sizeof p);
    p.Size = sizeof p; p.Version = 3;
    r = ctlProps(handles[i], &p);
    printf("\n[%u] props -> 0x%08x type=%d name='%s' vendor=0x%04x dev=0x%04x bdf=%02x:%02x.%x eus=%u\n",
           i, r, p.device_type, p.name, p.pci_vendor_id, p.pci_device_id,
           p.adapter_bdf.bus, p.adapter_bdf.device, p.adapter_bdf.function, p.num_xe_cores);

    ctl_oc_properties_t oc; memset(&oc, 0, sizeof oc);
    oc.Size = sizeof oc; oc.Version = 1;
    r = ctlOcProps(handles[i], &oc);
    printf("  ocprops -> 0x%08x supported=%d\n", r, oc.bSupported);
    if (r == 0) {
      printf("  powerLimit  sup=%d units=%d min=%.3f max=%.3f step=%.3f def=%.3f\n",
             oc.powerLimit.bSupported, oc.powerLimit.units, oc.powerLimit.min, oc.powerLimit.max, oc.powerLimit.step, oc.powerLimit.Default);
      printf("  gpuFreqOff  sup=%d units=%d min=%.3f max=%.3f step=%.3f def=%.3f\n",
             oc.gpuFrequencyOffset.bSupported, oc.gpuFrequencyOffset.units, oc.gpuFrequencyOffset.min, oc.gpuFrequencyOffset.max, oc.gpuFrequencyOffset.step, oc.gpuFrequencyOffset.Default);
      printf("  gpuVoltOff  sup=%d units=%d min=%.3f max=%.3f step=%.3f def=%.3f\n",
             oc.gpuVoltageOffset.bSupported, oc.gpuVoltageOffset.units, oc.gpuVoltageOffset.min, oc.gpuVoltageOffset.max, oc.gpuVoltageOffset.step, oc.gpuVoltageOffset.Default);
      printf("  tempLimit   sup=%d units=%d min=%.3f max=%.3f step=%.3f def=%.3f\n",
             oc.temperatureLimit.bSupported, oc.temperatureLimit.units, oc.temperatureLimit.min, oc.temperatureLimit.max, oc.temperatureLimit.step, oc.temperatureLimit.Default);
      printf("  vramFreqLim sup=%d units=%d min=%.3f max=%.3f step=%.3f def=%.3f\n",
             oc.vramMemSpeedLimit.bSupported, oc.vramMemSpeedLimit.units, oc.vramMemSpeedLimit.min, oc.vramMemSpeedLimit.max, oc.vramMemSpeedLimit.step, oc.vramMemSpeedLimit.Default);
    }
    double v = -1;
    if (plGet && plGet(handles[i], &v) == 0) printf("  live PL = %.1f W\n", v);
    v = -1; if (fqGet && fqGet(handles[i], &v) == 0) printf("  live core offset = %.1f MHz\n", v);
    v = -1; if (voGet && voGet(handles[i], &v) == 0) printf("  live volt offset = %.4f V\n", v);
    v = -1; if (tlGet && tlGet(handles[i], &v) == 0) printf("  live temp limit = %.1f C\n", v);
    v = -1; if (vlGet && vlGet(handles[i], &v) == 0) printf("  live vram limit = %.1f GTS\n", v);
  }
  if (ctlClose) ctlClose(api);
  FreeLibrary(lib);
  return 0;
}
