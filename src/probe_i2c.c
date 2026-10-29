// probe_i2c.c — read-only look at the IGCL I2C surface (CTA child driver must be up).
// Enumerates pin pairs and, if any, performs a single DDC read of EDID address 0x50.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "../src/arc.h"
#pragma GCC diagnostic ignored "-Wcast-function-type"

typedef struct {
  uint32_t Size; uint8_t Version;
  uint32_t DataSize, Address;
  int32_t OpType;      // 1 = read, 2 = write
  uint32_t Offset, Flags;
  uint8_t Data[128];
  uint32_t ReservedFields[4];
} i2c_pinpair_args_t;

_Static_assert(sizeof(i2c_pinpair_args_t) == 172, "pinpair args size");

int main(void) {
  ArcDev a; char err[256];
  if (!arc_open(&a, err, sizeof err)) { printf("open failed: %s\n", err); return 1; }
  ctl_result_t (*enumPP)(void *, uint32_t *, void **) =
      (ctl_result_t(*)(void *, uint32_t *, void **))GetProcAddress(a.igcl, "ctlEnumerateI2CPinPairs");
  ctl_result_t (*accPP)(void *, void *) =
      (ctl_result_t(*)(void *, void *))GetProcAddress(a.igcl, "ctlI2CAccessOnPinPair");
  printf("symbols: enumerate=%p accessOnPinPair=%p\n", (void *)enumPP, (void *)accPP);
  if (!enumPP) { arc_close(&a); return 0; }

  uint32_t count = 0;
  ctl_result_t r = enumPP(a.dev, &count, NULL);
  printf("ctlEnumerateI2CPinPairs(count): r=0x%08x count=%u\n", r, count);
  if (r != 0 || count == 0) { arc_close(&a); return 0; }

  void *pairs[16] = {0};
  uint32_t n = count > 16 ? 16 : count;
  r = enumPP(a.dev, &n, pairs);
  printf("fill: r=0x%08x n=%u\n", r, n);

  if (!accPP) { printf("no ctlI2CAccessOnPinPair\n"); arc_close(&a); return 0; }
  struct { uint32_t addr, flags, size; const char *label; } variants[] = {
    {0x50, 0x000, 8, "0x50 no-flags 8B"},
    {0x50, 0x002, 8, "0x50 1-byte-index 8B"},
    {0xA0, 0x002, 8, "0xA0 1-byte-index 8B"},
    {0x50, 0x004, 1, "0x50 2-byte-index 1B"},
  };
  for (uint32_t i = 0; i < n && i < 2; i++) {
    for (int v = 0; v < 4; v++) {
      i2c_pinpair_args_t args;
      memset(&args, 0, sizeof args);
      args.Size = sizeof args; args.Version = 0;
      args.DataSize = variants[v].size; args.Address = variants[v].addr;
      args.OpType = 1; args.Offset = 0; args.Flags = variants[v].flags;
      ctl_result_t ar = accPP(pairs[i], &args);
      printf("pair %u %-22s: r=0x%08x size=%u data=", i, variants[v].label, ar, args.DataSize);
      for (uint32_t b = 0; b < args.DataSize && b < 8; b++) printf("%02x", args.Data[b]);
      printf("\n");
    }
  }

  // and the display-output flavour of the same call
  ctl_result_t (*enumOut)(void *, uint32_t *, void **) =
      (ctl_result_t(*)(void *, uint32_t *, void **))GetProcAddress(a.igcl, "ctlEnumerateDisplayOutputs");
  if (enumOut) {
    uint32_t oc = 0;
    ctl_result_t or_ = enumOut(a.dev, &oc, NULL);
    printf("display outputs: r=0x%08x count=%u\n", or_, oc);
    if (or_ == 0 && oc > 0) {
      void *outs[32] = {0};
      uint32_t on = oc > 32 ? 32 : oc;
      if (enumOut(a.dev, &on, outs) == 0) {
        ctl_result_t (*i2cOut)(void *, void *) =
            (ctl_result_t(*)(void *, void *))GetProcAddress(a.igcl, "ctlI2CAccess");
        struct { uint32_t Size; uint8_t Version; uint32_t DataSize, Address; int32_t OpType;
                 uint32_t Offset, Flags; uint64_t RAD; uint8_t Data[128]; } args;
        if (i2cOut) {
          for (uint32_t i = 0; i < on && i < 3; i++) {
            memset(&args, 0, sizeof args);
            args.Size = sizeof args; args.Version = 0;
            args.DataSize = 8; args.Address = 0x50; args.OpType = 1; args.Offset = 0; args.Flags = 0;
            ctl_result_t ar = i2cOut(outs[i], &args);
            printf("ctlI2CAccess(out %u, EDID 0x50): r=0x%08x size=%u data=%02x%02x%02x%02x\n",
                   i, ar, args.DataSize, args.Data[0], args.Data[1], args.Data[2], args.Data[3]);
          }
        }
      }
    }
  }
  arc_close(&a);
  return 0;
}
