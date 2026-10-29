// version.h — single source of truth for the version (SemVer 2.0.0)
// Bump rules: MAJOR = incompatible layout/behaviour change, MINOR = new feature,
// PATCH = fixes only. The git tag is always "v<MAJOR>.<MINOR>.<PATCH>".
#pragma once

#define ARCTRAY_VERSION_MAJOR 1
#define ARCTRAY_VERSION_MINOR 9
#define ARCTRAY_VERSION_PATCH 0

#define ARCTRAY_STR_(x) #x
#define ARCTRAY_STR(x) ARCTRAY_STR_(x)
#define ARCTRAY_VERSION_STR \
  ARCTRAY_STR(ARCTRAY_VERSION_MAJOR) "." ARCTRAY_STR(ARCTRAY_VERSION_MINOR) "." ARCTRAY_STR(ARCTRAY_VERSION_PATCH)
