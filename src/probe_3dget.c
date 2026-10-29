// probe_3dget.c — find the request shape that reads a 3D feature value
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

int main(void) {
  ArcDev a; char err[256];
  if (!arc_open(&a, err, sizeof err)) { printf("open failed: %s\n", err); return 1; }
  ctl_result_t (*gets)(void *, void *) = (ctl_result_t(*)(void *, void *))GetProcAddress(a.igcl, "ctlGetSet3DFeature");
  if (!gets) { printf("no ctlGetSet3DFeature\n"); return 1; }

  static char empty[1] = {0};
  const int features[] = {15, 16, 9, 17, 2};
  for (int f = 0; f < 5; f++) {
    for (int ver = 0; ver <= 2; ver++) {
      for (int name = 0; name < 2; name++) {
        getset_t g; memset(&g, 0, sizeof g);
        g.Size = sizeof g; g.Version = (uint8_t)ver; g.FeatureType = features[f];
        g.ValueType = (features[f] == 2) ? 2 : 4;   // FRAME_LIMIT=INT32, others=UINT64
        g.ApplicationName = name ? empty : NULL;
        g.ApplicationNameLength = 0; g.bSet = 0;
        ctl_result_t r = gets(a.dev, &g);
        unsigned v = 0; memcpy(&v, g.Value, 4);
        printf("feat %2d ver %d name %-5s -> r=0x%08x value=%u\n",
               features[f], ver, name ? "\"\"" : "NULL", r, r == 0 ? v : 0);
      }
    }
  }
  arc_close(&a);
  return 0;
}
