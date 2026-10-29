// gfx.h — non-OC graphics features (IGCL ctlGetSet3DFeature)
#pragma once
#include "arc.h"

typedef struct {
  int id;             // ctl_3d_feature_t
  int value_type;     // ctl_property_value_type_t
  int per_app;
  const char *name;   // stable CLI name
} GfxFeature;

int gfx_caps(ArcDev *a, GfxFeature *out, int max);           // returns count
int gfx_get(ArcDev *a, int feature, int *enabled, unsigned *value);
int gfx_set(ArcDev *a, int feature, int enabled, unsigned value);
const char *gfx_name(int feature);                            // "frame-limit", ...
int gfx_id_by_name(const char *name);                         // -1 when unknown
