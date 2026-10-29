// probe_vf.c — is the custom V/F curve surface usable on this adapter?
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "../src/arc.h"

// GetProcAddress casts are the whole point of a probe; the project's own arc.c
// carries the same warnings, silence them locally so a probe build stays clean.
#pragma GCC diagnostic ignored "-Wcast-function-type"

typedef struct { uint32_t Voltage, Frequency; } vf_point_t;

int main(void) {
  ArcDev a;
  char err[256];
  if (!arc_open(&a, err, sizeof err)) { printf("open failed: %s\n", err); return 1; }
  printf("props version used: %d, oc version used: %d\n", a.props_version, a.oc_version);
  printf("VF limits: voltage sup=%d %.3f..%.3f  frequency sup=%d %.1f..%.1f\n",
         a.oc.gpuVFCurveVoltageLimit.bSupported, a.oc.gpuVFCurveVoltageLimit.min, a.oc.gpuVFCurveVoltageLimit.max,
         a.oc.gpuVFCurveFrequencyLimit.bSupported, a.oc.gpuVFCurveFrequencyLimit.min, a.oc.gpuVFCurveFrequencyLimit.max);

  ctl_result_t (*readVF)(void *, int32_t, int32_t, uint32_t *, vf_point_t *) =
      (ctl_result_t(*)(void *, int32_t, int32_t, uint32_t *, vf_point_t *))GetProcAddress(a.igcl, "ctlOverclockReadVFCurve");
  ctl_result_t (*writeVF)(void *, uint32_t, vf_point_t *) =
      (ctl_result_t(*)(void *, uint32_t, vf_point_t *))GetProcAddress(a.igcl, "ctlOverclockWriteCustomVFCurve");
  printf("symbols: read=%p write=%p\n", (void *)readVF, (void *)writeVF);
  if (!readVF) { arc_close(&a); return 0; }

  const char *tname[2] = {"STOCK", "LIVE"};
  const char *dname[3] = {"SIMPLIFIED", "MEDIUM", "ELABORATE"};
  for (int type = 0; type <= 1; type++) {
    for (int det = 0; det <= 2; det++) {
      uint32_t count = 0;
      ctl_result_t r = readVF(a.dev, type, det, &count, NULL);
      printf("readVF %-5s %-10s count-probe: r=0x%08x count=%u\n", tname[type], dname[det], r, count);
      if (r == 0 && count > 0 && count < 128) {
        vf_point_t pts[128];
        memset(pts, 0, sizeof pts);
        r = readVF(a.dev, type, det, &count, pts);
        printf("  fill: r=0x%08x\n", r);
        for (uint32_t i = 0; i < count && i < 6; i++) printf("    [%u] %u mV @ %u MHz\n", i, pts[i].Voltage, pts[i].Frequency);
        break;
      }
    }
  }
  arc_close(&a);
  return 0;
}
