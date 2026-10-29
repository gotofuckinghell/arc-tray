// probe_gfx.c — read all supported features, then round-trip low-latency and frame-limit
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include "../src/arc.h"
#include "../src/gfx.h"

int main(void) {
  ArcDev a; char err[256];
  if (!arc_open(&a, err, sizeof err)) { printf("open failed: %s\n", err); return 1; }
  GfxFeature f[64];
  int n = gfx_caps(&a, f, 64);
  printf("features: %d\n", n);
  for (int i = 0; i < n; i++) {
    int en = -1; unsigned v = 0;
    int ok = gfx_get(&a, f[i].id, &en, &v);
    printf("  %-18s id=%2d type=%d perApp=%d  get=%s enable=%d value=%u\n",
           f[i].name, f[i].id, f[i].value_type, f[i].per_app, ok ? "ok" : "fail", en, v);
  }
  printf("\n-- round-trip low-latency (16) --\n");
  int en; unsigned v;
  gfx_get(&a, 16, &en, &v); printf("before: enable=%d value=%u\n", en, v);
  printf("set on : %s\n", gfx_set(&a, 16, 1, 1) ? "ok" : "fail");
  Sleep(250); gfx_get(&a, 16, &en, &v); printf("after : enable=%d value=%u\n", en, v);
  printf("set off: %s\n", gfx_set(&a, 16, 1, 0) ? "ok" : "fail");
  Sleep(250); gfx_get(&a, 16, &en, &v); printf("final : enable=%d value=%u\n", en, v);

  printf("\n-- round-trip frame-limit (2) --\n");
  gfx_get(&a, 2, &en, &v); printf("before: enable=%d value=%u\n", en, v);
  printf("set 60 : %s\n", gfx_set(&a, 2, 1, 60) ? "ok" : "fail");
  Sleep(250); gfx_get(&a, 2, &en, &v); printf("after : enable=%d value=%u\n", en, v);
  printf("set 0  : %s\n", gfx_set(&a, 2, 1, 0) ? "ok" : "fail");
  Sleep(250); gfx_get(&a, 2, &en, &v); printf("final : enable=%d value=%u\n", en, v);

  arc_close(&a);
  return 0;
}
