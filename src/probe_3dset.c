// probe_3dset.c — can we write a 3D feature (FPS limit) and read it back?
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "../src/arc.h"
#pragma GCC diagnostic ignored "-Wcast-function-type"

typedef struct { uint32_t Size; uint8_t Version; int32_t FeatureType; void *ApplicationName;
                 int8_t ApplicationNameLength; uint8_t bSet; int32_t ValueType; uint8_t Value[8];
                 int32_t CustomValueSize; void *pCustomValue; } getset_t;

static ctl_result_t call(ctl_result_t (*fn)(void *, void *), void *dev, int feature, int vtype,
                         int set, uint32_t scalar, uint32_t *out) {
  getset_t g; memset(&g, 0, sizeof g);
  static char empty[1] = {0};
  g.Size = sizeof g; g.Version = 0; g.FeatureType = feature; g.ValueType = vtype;
  g.ApplicationName = empty; g.ApplicationNameLength = 1; g.bSet = set ? 1 : 0;
  if (set) memcpy(g.Value + 4, &scalar, 4);   // scalar lives at offset 36 (Value+4)
  ctl_result_t r = fn(dev, &g);
  if (out) { uint32_t v = 0; memcpy(&v, g.Value + 4, 4); *out = v; }
  return r;
}

int main(void) {
  ArcDev a; char err[256];
  if (!arc_open(&a, err, sizeof err)) { printf("open failed: %s\n", err); return 1; }
  ctl_result_t (*gets)(void *, void *) = (ctl_result_t(*)(void *, void *))GetProcAddress(a.igcl, "ctlGetSet3DFeature");
  if (!gets) return 1;

  uint32_t v = 0;
  printf("FRAME_LIMIT read before: r=0x%08x value=%u\n", call(gets, a.dev, 2, 2, 0, 0, &v), v);

  ctl_result_t wr = call(gets, a.dev, 2, 2, 1, 60, NULL);
  printf("FRAME_LIMIT set 60    : r=0x%08x\n", wr);
  Sleep(300);
  v = 0;
  printf("FRAME_LIMIT read after : r=0x%08x value=%u\n", call(gets, a.dev, 2, 2, 0, 0, &v), v);

  // restore: 0 = off
  ctl_result_t back = call(gets, a.dev, 2, 2, 1, 0, NULL);
  printf("FRAME_LIMIT restore 0  : r=0x%08x\n", back);
  Sleep(300);
  v = 0;
  printf("FRAME_LIMIT read final : r=0x%08x value=%u\n", call(gets, a.dev, 2, 2, 0, 0, &v), v);

  // enum feature: low latency (16), enum values unknown -> just read
  v = 0;
  printf("LOW_LATENCY read       : r=0x%08x value=%u\n", call(gets, a.dev, 16, 4, 0, 0, &v), v);
  arc_close(&a);
  return 0;
}
