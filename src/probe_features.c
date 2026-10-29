// probe_features.c — read-only sweep of every ctl_3d_feature_t id, including the ones the
// driver does not advertise in ctlGetSupported3DCapabilities. Prints the raw result code,
// so "absent from caps" can be distinguished from "present but unreadable".
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "../src/arc.h"

typedef struct {
  uint32_t Size; uint8_t Version; int32_t FeatureType; void *ApplicationName;
  int8_t ApplicationNameLength; uint8_t bSet; int32_t ValueType;
  uint8_t Enable; uint8_t Pad[3]; uint32_t Value;
  int32_t CustomValueSize; void *pCustomValue;
} getset_t;

static const char *NAME[21] = {
  "frame-pacing", "endurance-gaming", "frame-limit", "anisotropic", "cmaa",
  "texture-filtering", "adaptive-tessellation", "sharpening", "msaa", "flip-mode",
  "adaptive-sync-plus", "app-profiles", "app-profile-details", "atomics-64",
  "vrr-windowed", "scope", "low-latency", "frame-gen", "prebuilt-shaders",
  "live-state", "frame-gen-control"
};

typedef ctl_result_t (*fn_gets)(void *, void *);

int main(void) {
  ArcDev a; char err[256];
  if (!arc_open(&a, err, sizeof err)) { printf("open failed: %s\n", err); return 1; }
  fn_gets gets = (fn_gets)GetProcAddress(a.igcl, "ctlGetSet3DFeature");
  if (!gets) { printf("no ctlGetSet3DFeature\n"); return 1; }
  static char empty[1] = {0};
  printf("sizeof(getset_t)=%zu\n", sizeof(getset_t));
  for (int id = 0; id <= 20; id++) {
    if (id == 11 || id == 12) {              /* CUSTOM payload: the driver dereferences
                                                pCustomValue, a null one crashes IGCL */
      printf("id %2d %-22s skipped (CUSTOM payload)\n", id, NAME[id]);
      continue;
    }
    getset_t g; memset(&g, 0, sizeof g);
    g.Size = sizeof g; g.Version = 0; g.FeatureType = id;
    g.ApplicationName = empty; g.ApplicationNameLength = 1;
    g.bSet = 0; g.ValueType = 0;
    ctl_result_t r = gets(a.dev, &g);
    printf("id %2d %-22s r=0x%08x", id, NAME[id], r);
    if (r == 0) printf("  enable=%d value=%u type=%d", g.Enable, g.Value, g.ValueType);
    printf("\n");
  }
  arc_close(&a);
  return 0;
}
