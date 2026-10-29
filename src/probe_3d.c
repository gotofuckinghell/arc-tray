// probe_3d.c — which non-OC (3D / display) controls the installed driver exposes
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "../src/arc.h"

// GetProcAddress casts are inherent to a probe (same as probe_vf.c)
#pragma GCC diagnostic ignored "-Wcast-function-type"

typedef struct { uint32_t Size; uint8_t Version; uint32_t NumSupportedFeatures; void *pFeatureDetails; } caps_t;
typedef struct {
  int32_t FeatureType, ValueType; uint8_t Value[24];
  int32_t CustomValueSize; void *pCustomValue;
  uint8_t PerAppSupport; int64_t ConflictingFeatures; int16_t Misc, R1, R2, R3;
} detail_t;
typedef struct {
  uint32_t Size; uint8_t Version; int32_t FeatureType; void *ApplicationName;
  int8_t ApplicationNameLength; uint8_t bSet; int32_t ValueType; uint8_t Value[8];
  int32_t CustomValueSize; void *pCustomValue;
} getset_t;

static const char *F(int t) {
  switch (t) {
    case 0: return "FRAME_PACING"; case 1: return "ENDURANCE_GAMING"; case 2: return "FRAME_LIMIT";
    case 3: return "ANISOTROPIC"; case 4: return "CMAA"; case 5: return "TEXTURE_FILTERING_QUALITY";
    case 6: return "ADAPTIVE_TESSELLATION"; case 7: return "SHARPENING_FILTER"; case 8: return "MSAA";
    case 9: return "GAMING_FLIP_MODES"; case 10: return "ADAPTIVE_SYNC_PLUS"; case 11: return "APP_PROFILES";
    case 12: return "APP_PROFILE_DETAILS"; case 13: return "EMULATED_64BIT_ATOMICS"; case 14: return "VRR_WINDOWED_BLT";
    case 15: return "GLOBAL_OR_PER_APP"; case 16: return "LOW_LATENCY"; case 17: return "FRAME_GENERATION";
    case 18: return "PREBUILT_SHADER_DOWNLOAD"; case 19: return "LIVE_STATE"; default: return "?";
  }
}
static const char *VT(int t) { return t == 0 ? "BOOL" : t == 1 ? "FLOAT" : t == 2 ? "INT32" : t == 3 ? "UINT32" : t == 4 ? "UINT64" : t == 5 ? "CUSTOM" : "?"; }

int main(void) {
  ArcDev a;
  char err[256];
  if (!arc_open(&a, err, sizeof err)) { printf("open failed: %s\n", err); return 1; }
  ctl_result_t (*caps)(void *, void *) = (ctl_result_t(*)(void *, void *))GetProcAddress(a.igcl, "ctlGetSupported3DCapabilities");
  ctl_result_t (*gets)(void *, void *) = (ctl_result_t(*)(void *, void *))GetProcAddress(a.igcl, "ctlGetSet3DFeature");
  if (!caps || !gets) { printf("3D feature API missing\n"); arc_close(&a); return 1; }

  caps_t c;
  memset(&c, 0, sizeof c); c.Size = sizeof c; c.Version = 0;
  ctl_result_t r = caps(a.dev, &c);
  printf("ctlGetSupported3DCapabilities: r=0x%08x count=%u ptr=%p\n", r, c.NumSupportedFeatures, c.pFeatureDetails);
  // two-pass: the first call only reports the count, the second fills our array
  static detail_t buf[64];
  memset(buf, 0, sizeof buf);
  caps_t c2;
  memset(&c2, 0, sizeof c2);
  c2.Size = sizeof c2; c2.Version = 0; c2.NumSupportedFeatures = c.NumSupportedFeatures; // exact count
  c2.pFeatureDetails = buf;
  r = caps(a.dev, &c2);
  printf("fill: r=0x%08x count=%u\n", r, c2.NumSupportedFeatures);
  if (r != 0 || c2.NumSupportedFeatures == 0) { arc_close(&a); return 0; }
  detail_t *d = buf;
  for (uint32_t i = 0; i < c.NumSupportedFeatures && i < 64; i++) {
    getset_t g;
    memset(&g, 0, sizeof g);
    g.Size = sizeof g; g.Version = 0; g.FeatureType = d[i].FeatureType; g.ValueType = d[i].ValueType;
    static char empty[1] = {0};
    ctl_result_t gr = 0xffffffff; unsigned val = 0;
    // scope variants: ""+len0, NULL+len0, ""+len1
    for (int variant = 0; variant < 3 && gr != 0; variant++) {
      memset(&g, 0, sizeof g);
      g.Size = sizeof g; g.Version = 0; g.FeatureType = d[i].FeatureType; g.ValueType = d[i].ValueType;
      if (variant == 0) { g.ApplicationName = empty; g.ApplicationNameLength = 0; }
      else if (variant == 1) { g.ApplicationName = NULL; g.ApplicationNameLength = 0; }
      else { g.ApplicationName = empty; g.ApplicationNameLength = 1; }
      g.bSet = 0;
      gr = gets(a.dev, &g);
      if (gr == 0) memcpy(&val, g.Value, 4);
    } // Enable/bool or scalar share the same first word
    printf("[%2d] %-24s type=%-6s perApp=%u get=0x%08x value=%u\n",
           d[i].FeatureType, F(d[i].FeatureType), VT(d[i].ValueType), d[i].PerAppSupport, gr, val);
  }
  arc_close(&a);
  return 0;
}
