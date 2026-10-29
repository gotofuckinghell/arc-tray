// probe_vram.c — read-only: does this driver expose the VRAM memory speed limit and the
// custom V/F curve (both are "Version > 0" fields of ctl_oc_properties_t)?
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "../src/arc.h"

typedef ctl_result_t (*fn_vram_get)(void *, double *);

int main(void) {
  ArcDev a; char err[256];
  if (!arc_open(&a, err, sizeof err)) { printf("open failed: %s\n", err); return 1; }

  // re-read the OC properties at version 1: vramMemSpeedLimit and the VF-curve limits
  // only exist there (version 0 is the shorter layout).
  ctl_oc_properties_t p; memset(&p, 0, sizeof p);
  p.Size = sizeof p; p.Version = 1;
  ctl_result_t r = a.ctlOverclockGetProperties(a.dev, &p);
  printf("oc props v1: r=0x%08x  supported=%d\n", r, p.bSupported);
  if (r == 0) {
    struct { const char *n; ctl_oc_control_info_t *i; } f[] = {
      {"vramMemSpeedLimit", &p.vramMemSpeedLimit},
      {"gpuVFCurveVoltageLimit", &p.gpuVFCurveVoltageLimit},
      {"gpuVFCurveFrequencyLimit", &p.gpuVFCurveFrequencyLimit},
      {"powerLimit", &p.powerLimit},
      {"gpuFrequencyOffset", &p.gpuFrequencyOffset},
      {"gpuVoltageOffset", &p.gpuVoltageOffset},
    };
    for (unsigned i = 0; i < sizeof f / sizeof f[0]; i++)
      printf("  %-24s supported=%d units=%d min=%g max=%g step=%g default=%g rel=%d\n",
             f[i].n, f[i].i->bSupported, f[i].i->units, f[i].i->min, f[i].i->max,
             f[i].i->step, f[i].i->Default, f[i].i->bRelative);
  } else {
    printf("  (version 1 rejected: %s)\n", arc_result_name(r));
  }

  void *sym = (void *)GetProcAddress(a.igcl, "ctlOverclockVramMemSpeedLimitGetV2");
  fn_vram_get vram_get = NULL;
  if (sym) memcpy(&vram_get, &sym, sizeof vram_get);
  printf("ctlOverclockVramMemSpeedLimitGetV2 resolved: %s\n", vram_get ? "yes" : "no");
  if (vram_get) {
    double v = 0;
    ctl_result_t gr = vram_get(a.dev, &v);
    printf("vram speed get: r=0x%08x (%s) value=%g\n", gr, arc_result_name(gr), v);
  }

  typedef ctl_result_t (*fn_vf)(void *, int32_t, int32_t, uint32_t *, void *);
  sym = (void *)GetProcAddress(a.igcl, "ctlOverclockReadVFCurve");
  printf("ctlOverclockReadVFCurve resolved: %s\n", sym ? "yes" : "no");
  if (sym) {
    fn_vf vf = NULL;
    memcpy(&vf, &sym, sizeof vf);
    static const struct { int type; int detail; const char *label; } q[] = {
      {0, 0, "stock/simplified"}, {1, 0, "live/simplified"},
      {1, 1, "live/medium"},      {1, 2, "live/elaborate"},
    };
    for (unsigned i = 0; i < sizeof q / sizeof q[0]; i++) {
      uint32_t n = 0;                       // 0 = "tell me how many points you have"
      ctl_result_t vr = vf(a.dev, q[i].type, q[i].detail, &n, NULL);
      printf("  vf %-16s r=0x%08x (%s) points=%u\n", q[i].label, vr, arc_result_name(vr), n);
    }
  }

  arc_close(&a);
  return 0;
}
