// arc.h — IGCL + Level Zero Sysman bindings for a headless Arc tuner.
// Struct layouts ported from Arc-Power (GPL-2.0) src/main/backend/igcl-bindings.js
// and src/main/sysman/sysman-bindings.js.
#pragma once
#include <windows.h>
#include <stdint.h>
#include <stddef.h>

typedef uint32_t ctl_result_t;
typedef int32_t ze_result_t;
typedef void *ze_handle;

typedef struct { uint32_t Data1; uint16_t Data2, Data3; uint8_t Data4[8]; } ctl_application_id_t;
typedef struct { uint32_t Size; uint8_t Version; uint32_t AppVersion, flags, SupportedVersion; ctl_application_id_t ApplicationUID; } ctl_init_args_t;
typedef struct { uint64_t major_version, minor_version, build_number; } ctl_firmware_version_t;
typedef struct { uint8_t bus, device, function; } ctl_adapter_bdf_t;

typedef struct {
  uint32_t Size; uint8_t Version; void *pDeviceID; uint32_t device_id_size; int32_t device_type;
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

typedef struct { uint8_t bSupported, bRelative, bReference; int32_t units; double min, max, step, Default, reference; } ctl_oc_control_info_t;
typedef struct {
  uint32_t Size; uint8_t Version; uint8_t bSupported;
  ctl_oc_control_info_t gpuFrequencyOffset, gpuVoltageOffset, vramFrequencyOffset, vramVoltageOffset;
  ctl_oc_control_info_t powerLimit, temperatureLimit, vramMemSpeedLimit;
  ctl_oc_control_info_t gpuVFCurveVoltageLimit, gpuVFCurveFrequencyLimit;
} ctl_oc_properties_t;

typedef struct { uint32_t Size; uint8_t Version; int32_t speed, units; } ctl_fan_speed_t;
typedef struct { uint32_t Size; uint8_t Version; uint32_t temperature; ctl_fan_speed_t speed; } ctl_fan_temp_speed_t;
typedef struct { uint32_t Size; uint8_t Version; int32_t numPoints; ctl_fan_temp_speed_t table[32]; } ctl_fan_speed_table_t;
typedef struct { uint32_t Size; uint8_t Version; int32_t mode; ctl_fan_speed_t speedFixed; ctl_fan_speed_table_t speedTable; } ctl_fan_config_t;
typedef struct { uint32_t Size; uint8_t Version; uint8_t canControl; uint32_t supportedModes, supportedUnits; int32_t maxRPM, maxPoints; } ctl_fan_properties_t;

typedef struct { uint8_t bSupported; int32_t units, type; double value; } ctl_oc_telemetry_item_t;
typedef struct { uint8_t bSupported; int32_t psuType; ctl_oc_telemetry_item_t energyCounter, voltage; } ctl_psu_info_t;
typedef struct {
  uint32_t Size; uint8_t Version;
  ctl_oc_telemetry_item_t timeStamp, gpuEnergyCounter, gpuVoltage, gpuCurrentClockFrequency, gpuCurrentTemperature;
  ctl_oc_telemetry_item_t globalActivityCounter, renderComputeActivityCounter, mediaActivityCounter;
  uint8_t gpuPowerLimited, gpuTemperatureLimited, gpuCurrentLimited, gpuVoltageLimited, gpuUtilizationLimited;
  ctl_oc_telemetry_item_t vramEnergyCounter, vramVoltage, vramCurrentClockFrequency, vramCurrentEffectiveFrequency;
  ctl_oc_telemetry_item_t vramReadBandwidthCounter, vramWriteBandwidthCounter, vramCurrentTemperature;
  ctl_oc_telemetry_item_t totalCardEnergyCounter;
  ctl_psu_info_t psu[5];
  ctl_oc_telemetry_item_t fanSpeed[5];
  ctl_oc_telemetry_item_t gpuVrTemp, vramVrTemp, saVrTemp, gpuEffectiveClock;
  ctl_oc_telemetry_item_t gpuOverVoltagePercent, gpuPowerPercent, gpuTemperaturePercent;
  ctl_oc_telemetry_item_t vramReadBandwidth, vramWriteBandwidth;
} ctl_power_telemetry_v2_t;

// V1 telemetry: the V2 layout plus the five deprecated vram*Limited bools
typedef struct {
  uint32_t Size; uint8_t Version;
  ctl_oc_telemetry_item_t timeStamp, gpuEnergyCounter, gpuVoltage, gpuCurrentClockFrequency, gpuCurrentTemperature;
  ctl_oc_telemetry_item_t globalActivityCounter, renderComputeActivityCounter, mediaActivityCounter;
  uint8_t gpuPowerLimited, gpuTemperatureLimited, gpuCurrentLimited, gpuVoltageLimited, gpuUtilizationLimited;
  ctl_oc_telemetry_item_t vramEnergyCounter, vramVoltage, vramCurrentClockFrequency, vramCurrentEffectiveFrequency;
  ctl_oc_telemetry_item_t vramReadBandwidthCounter, vramWriteBandwidthCounter, vramCurrentTemperature;
  uint8_t vramPowerLimited, vramTemperatureLimited, vramCurrentLimited, vramVoltageLimited, vramUtilizationLimited;
  ctl_oc_telemetry_item_t totalCardEnergyCounter;
  ctl_psu_info_t psu[5];
  ctl_oc_telemetry_item_t fanSpeed[5];
  ctl_oc_telemetry_item_t gpuVrTemp, vramVrTemp, saVrTemp, gpuEffectiveClock;
  ctl_oc_telemetry_item_t gpuOverVoltagePercent, gpuPowerPercent, gpuTemperaturePercent;
  ctl_oc_telemetry_item_t vramReadBandwidth, vramWriteBandwidth;
} ctl_power_telemetry_t;

typedef struct {
  uint32_t stype; void *pNext; uint8_t isOcSupported;
  double maxFactoryDefaultFrequency, maxFactoryDefaultVoltage, maxOcFrequency;
  double minOcVoltageOffset, maxOcVoltageOffset, maxOcVoltage;
  uint8_t isTjMaxSupported, isIccMaxSupported, isHighVoltModeCapable, isHighVoltModeEnabled;
  uint8_t isExtendedModeSupported, isFixedModeSupported;
} zes_frequency_oc_capabilities_t;

typedef struct { uint8_t enabled; int32_t power, interval; } zes_power_sustained_limit_t;
typedef struct { uint8_t enabled; int32_t power; } zes_power_burst_limit_t;
typedef struct { int32_t powerAC, powerDC; } zes_power_peak_limit_t;

_Static_assert(sizeof(ctl_init_args_t) == 36, "init_args");
_Static_assert(sizeof(ctl_device_adapter_properties_t) == 320, "adapter_props");
_Static_assert(sizeof(ctl_oc_control_info_t) == 48, "oc_info");
_Static_assert(sizeof(ctl_oc_properties_t) == 440, "oc_props");
_Static_assert(sizeof(ctl_fan_speed_t) == 16, "fan_speed");
_Static_assert(sizeof(ctl_fan_config_t) == 936, "fan_config");
_Static_assert(sizeof(ctl_fan_properties_t) == 24, "fan_props");
_Static_assert(sizeof(ctl_power_telemetry_v2_t) == 1016, "telemetry_v2");
_Static_assert(sizeof(ctl_power_telemetry_t) == 1024, "telemetry_v1");
_Static_assert(sizeof(zes_frequency_oc_capabilities_t) == 80, "ze_oc_caps");

typedef struct {
  HMODULE igcl, ze;
  void *api, *dev;        // ctlInit handle, device handle
  void *fan;              // first fan handle (may be NULL)
  void *pwr;              // sysman card power domain (may be NULL)
  void *ze_dev;           // sysman device handle
  ctl_device_adapter_properties_t props;
  ctl_oc_properties_t oc;
  int elevated, waiver_set, have_sysman;
  char dll_path[MAX_PATH];

  // function pointers (IGCL)
  ctl_result_t (*ctlInit)(ctl_init_args_t *, void **);
  ctl_result_t (*ctlClose)(void *);
  ctl_result_t (*ctlEnumerateDevices)(void *, uint32_t *, void **);
  ctl_result_t (*ctlGetDeviceProperties)(void *, ctl_device_adapter_properties_t *);
  ctl_result_t (*ctlOverclockGetProperties)(void *, ctl_oc_properties_t *);
  ctl_result_t (*ctlOverclockWaiverSet)(void *);
  ctl_result_t (*ctlOverclockResetToDefault)(void *);
  ctl_result_t (*ctlOverclockPowerLimitGetV2)(void *, double *);
  ctl_result_t (*ctlOverclockPowerLimitSetV2)(void *, double);
  ctl_result_t (*ctlOverclockGpuFrequencyOffsetGetV2)(void *, double *);
  ctl_result_t (*ctlOverclockGpuFrequencyOffsetSetV2)(void *, double);
  ctl_result_t (*ctlOverclockGpuMaxVoltageOffsetGetV2)(void *, double *);
  ctl_result_t (*ctlOverclockGpuMaxVoltageOffsetSetV2)(void *, double);
  ctl_result_t (*ctlOverclockTemperatureLimitGetV2)(void *, double *);
  ctl_result_t (*ctlOverclockTemperatureLimitSetV2)(void *, double);
  ctl_result_t (*ctlEnumFans)(void *, uint32_t *, void **);
  ctl_result_t (*ctlFanGetProperties)(void *, ctl_fan_properties_t *);
  ctl_result_t (*ctlFanGetConfig)(void *, ctl_fan_config_t *);
  ctl_result_t (*ctlFanGetState)(void *, int32_t, int32_t *);
  ctl_result_t (*ctlFanSetFixedSpeedMode)(void *, ctl_fan_speed_t *);
  ctl_result_t (*ctlFanSetSpeedTableMode)(void *, ctl_fan_speed_t *);
  ctl_result_t (*ctlFanSetDefaultMode)(void *);
  ctl_result_t (*ctlPowerTelemetryGetV2)(void *, ctl_power_telemetry_v2_t *);
  ctl_result_t (*ctlPowerTelemetryGet)(void *, ctl_power_telemetry_t *);
  ctl_result_t (*ctlOverclockGpuFrequencyOffsetGet)(void *, double *);
  ctl_result_t (*ctlOverclockGpuFrequencyOffsetSet)(void *, double);
  ctl_result_t (*ctlOverclockGpuVoltageOffsetGet)(void *, double *);
  ctl_result_t (*ctlOverclockGpuVoltageOffsetSet)(void *, double);
  ctl_result_t (*ctlOverclockPowerLimitGet)(void *, double *);
  ctl_result_t (*ctlOverclockPowerLimitSet)(void *, double);
  ctl_result_t (*ctlOverclockTemperatureLimitGet)(void *, double *);
  ctl_result_t (*ctlOverclockTemperatureLimitSet)(void *, double);
  int props_version, oc_version;

  // extended 2023 runtime (V1 setters: power mW, temp C, up to 115 C)
  HMODULE ext;
  void *ext_api, *ext_dev;
  ctl_result_t (*ext_close)(void *);
  ctl_result_t (*ext_set_temp)(void *, double);
  ctl_result_t (*ext_get_temp)(void *, double *);
  int ext_ready, ext_elevated_ok;

  // function pointers (Sysman)
  ze_result_t (*zesInit)(uint32_t);
  ze_result_t (*zesDriverGet)(uint32_t *, ze_handle *);
  ze_result_t (*zesDeviceGet)(ze_handle, uint32_t *, ze_handle *);
  ze_result_t (*zesDeviceEnumPowerDomains)(ze_handle, uint32_t *, ze_handle *);
  ze_result_t (*zesPowerGetLimits)(ze_handle, zes_power_sustained_limit_t *, zes_power_burst_limit_t *, zes_power_peak_limit_t *);
  ze_result_t (*zesPowerSetLimits)(ze_handle, const zes_power_sustained_limit_t *, const zes_power_burst_limit_t *, const zes_power_peak_limit_t *);
  ze_result_t (*zesDeviceEnumFrequencyDomains)(ze_handle, uint32_t *, ze_handle *);
  ze_result_t (*zesFrequencyOcGetCapabilities)(ze_handle, zes_frequency_oc_capabilities_t *);
  ze_result_t (*zesFrequencyOcGetVoltageTarget)(ze_handle, double *, double *);
  ze_result_t (*zesFrequencyOcSetVoltageTarget)(ze_handle, double, double);
  ze_result_t (*zesFrequencyOcGetFrequencyTarget)(ze_handle, double *);
  ze_result_t (*zesFrequencyOcSetFrequencyTarget)(ze_handle, double);
  ze_result_t (*zesFrequencyOcSetMode)(ze_handle, uint32_t);
  ze_result_t (*zesDeviceSetOverclockWaiver)(ze_handle);
  void *freq;
} ArcDev;

typedef struct {
  char name[100];
  int bus, dev, fn;
  // capability ranges (IGCL reported)
  double pl_min, pl_max, pl_def;
  double core_min, core_max;
  double volt_min, volt_max, volt_def;
  double temp_min, temp_max, temp_def;
  // live values
  double pl_sustained, pl_burst, pl_igcl;
  double core_off, volt_off, temp_limit;
  double gpu_mhz, gpu_temp;
  double vram_mhz, vram_eff_mhz, vram_temp;
  int fan_pct, fan_mode, fan_can_control, fan_available;
  int fan_state, fan_state_units, fan_max_rpm, fan_set_pct;
  int have_sysman, elevated;
} ArcState;

const char *arc_result_name(uint32_t code);
int arc_open(ArcDev *a, char *err, size_t errcap);
void arc_close(ArcDev *a);
int arc_read(ArcDev *a, ArcState *st);
int arc_set_power(ArcDev *a, double watts, char *msg, size_t cap);
int arc_set_core(ArcDev *a, double mhz, char *msg, size_t cap);
int arc_set_temp(ArcDev *a, double celsius, char *msg, size_t cap);
int arc_set_fan(ArcDev *a, int mode, int percent, char *msg, size_t cap);
int arc_set_volt(ArcDev *a, int millivolts, char *msg, size_t cap);
int arc_set_temp_ext(ArcDev *a, double celsius, char *msg, size_t cap);
int arc_ext_available(ArcDev *a);
