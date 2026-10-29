// gfx.c — the non-OC 3D feature surface (FPS limit, VSync/flip, low latency, frame gen…)
#include "gfx.h"
#include <stdio.h>
#include <string.h>

typedef struct {
  uint32_t Size; uint8_t Version; uint32_t NumSupportedFeatures; void *pFeatureDetails;
} caps_t;

typedef struct {
  int32_t FeatureType, ValueType; uint8_t Info[24];
  int32_t CustomValueSize; void *pCustomValue;
  uint8_t PerAppSupport; int64_t ConflictingFeatures; int16_t Misc, R1, R2, R3;
} detail_t;

typedef struct {
  uint32_t Size; uint8_t Version; int32_t FeatureType; void *ApplicationName;
  int8_t ApplicationNameLength; uint8_t bSet; int32_t ValueType;
  uint8_t Enable;        // union member: bool Enable
  uint8_t Pad[3];
  uint32_t Value;        // union member: int/uint/enum Value
  int32_t CustomValueSize; void *pCustomValue;
} getset_t;

_Static_assert(sizeof(getset_t) == 56, "getset layout");
_Static_assert(sizeof(detail_t) == 72, "details layout");

typedef ctl_result_t (*fn_caps)(void *, void *);
typedef ctl_result_t (*fn_gets)(void *, void *);

static const struct { int id; const char *name; } NAMES[] = {
  {2, "frame-limit"}, {4, "cmaa"}, {9, "flip-mode"}, {11, "app-profiles"},
  {13, "atomics-64"}, {14, "vrr-windowed"}, {15, "scope"}, {16, "low-latency"},
  {17, "frame-gen"}, {19, "live-state"}, {20, "frame-gen-control"},
};

const char *gfx_name(int feature) {
  for (unsigned i = 0; i < sizeof NAMES / sizeof NAMES[0]; i++)
    if (NAMES[i].id == feature) return NAMES[i].name;
  return "?";
}

int gfx_id_by_name(const char *name) {
  for (unsigned i = 0; i < sizeof NAMES / sizeof NAMES[0]; i++)
    if (_stricmp(NAMES[i].name, name) == 0) return NAMES[i].id;
  return -1;
}

static fn_caps caps_fn(ArcDev *a) { return (fn_caps)GetProcAddress(a->igcl, "ctlGetSupported3DCapabilities"); }
static fn_gets gets_fn(ArcDev *a) { return (fn_gets)GetProcAddress(a->igcl, "ctlGetSet3DFeature"); }

int gfx_caps(ArcDev *a, GfxFeature *out, int max) {
  fn_caps caps = caps_fn(a);
  if (!caps) return 0;
  caps_t c; memset(&c, 0, sizeof c); c.Size = sizeof c; c.Version = 0;
  if (caps(a->dev, &c) != 0 || c.NumSupportedFeatures == 0) return 0;

  static detail_t buf[64];
  memset(buf, 0, sizeof buf);
  caps_t fill; memset(&fill, 0, sizeof fill);
  fill.Size = sizeof fill; fill.Version = 0;
  // The driver fills as many detail slots as it reports; a future stack with more
  // than 64 features must not be allowed to write past buf.
  const uint32_t room = (uint32_t)(sizeof buf / sizeof buf[0]);
  const uint32_t want = c.NumSupportedFeatures > room ? room : c.NumSupportedFeatures;
  fill.NumSupportedFeatures = want;
  fill.pFeatureDetails = buf;
  if (caps(a->dev, &fill) != 0) return 0;
  if (fill.NumSupportedFeatures < want) return 0;   // driver refused the smaller request

  int n = 0;
  for (uint32_t i = 0; i < fill.NumSupportedFeatures && n < max; i++) {
    out[n].id = buf[i].FeatureType;
    out[n].value_type = buf[i].ValueType;
    out[n].per_app = buf[i].PerAppSupport;
    out[n].name = gfx_name(buf[i].FeatureType);
    n++;
  }
  return n;
}

static int find_type(ArcDev *a, int feature, int *vtype, int *per_app) {
  GfxFeature f[64];
  int n = gfx_caps(a, f, 64);
  for (int i = 0; i < n; i++)
    if (f[i].id == feature) {
      if (vtype) *vtype = f[i].value_type;
      if (per_app) *per_app = f[i].per_app;
      return 1;
    }
  return 0;
}

// The Value union differs per type: ENUM is a single uint32 (EnableType) at the
// union start, while INT/UINT/FLOAT are {bool Enable; value} — the header owns that.
static void union_put(getset_t *g, int vtype, int enabled, unsigned value) {
  uint8_t *u = &g->Enable;
  if (vtype == 4) { uint32_t t = value; memcpy(u, &t, 4); return; }
  if (vtype == 0) { g->Enable = enabled ? 1 : 0; return; }
  g->Enable = enabled ? 1 : 0;
  g->Value = value;
}

static void union_take(const getset_t *g, int vtype, int *enabled, unsigned *value) {
  const uint8_t *u = &g->Enable;
  if (vtype == 4) {                       // ENUM: one uint32 at the union start
    uint32_t t = 0; memcpy(&t, u, 4);
    if (value) *value = t;
    if (enabled) *enabled = t ? 1 : 0;
    return;
  }
  if (enabled) *enabled = g->Enable ? 1 : 0;
  if (!value) return;
  if (vtype == 0) { *value = 0; return; } // BOOL has no Value member at all
  if (vtype == 1) {                       // FLOAT: {bool Enable; float Value;}
    float f = 0; memcpy(&f, &g->Value, sizeof f);
    *value = (f > 0) ? (unsigned)(f + 0.5f) : 0;
    return;
  }
  *value = g->Value;                      // INT32 / UINT32
}

int gfx_get(ArcDev *a, int feature, int *enabled, unsigned *value) {
  fn_gets gets = gets_fn(a);
  int vtype = 0;
  if (!gets || !find_type(a, feature, &vtype, NULL)) return 0;
  getset_t g; memset(&g, 0, sizeof g);
  static char empty[2] = {0};   /* room for the NUL even with Length = 1 */
  g.Size = sizeof g; g.Version = 0; g.FeatureType = feature;
  g.ApplicationName = empty; g.ApplicationNameLength = 1; g.bSet = 0; g.ValueType = vtype;
  if (gets(a->dev, &g) != 0) return 0;
  union_take(&g, vtype, enabled, value);
  return 1;
}

int gfx_set(ArcDev *a, int feature, int enabled, unsigned value) {
  fn_gets gets = gets_fn(a);
  int vtype = 0;
  if (!gets || !find_type(a, feature, &vtype, NULL)) return 0;
  getset_t g; memset(&g, 0, sizeof g);
  static char empty[2] = {0};   /* room for the NUL even with Length = 1 */
  g.Size = sizeof g; g.Version = 0; g.FeatureType = feature;
  g.ApplicationName = empty; g.ApplicationNameLength = 1; g.bSet = 1; g.ValueType = vtype;
  union_put(&g, vtype, enabled, value);
  return gets(a->dev, &g) == 0;
}
