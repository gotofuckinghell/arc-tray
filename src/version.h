// version.h — single source of truth for the version.
// Pre-1.0 on purpose: the menu/CLI/config surface is still moving, so the project stays at
// 0.0.N (patch = any change, feature or fix) until the interface is frozen. The git tag is
// always "v<MAJOR>.<MINOR>.<PATCH>". The earlier 1.x tags stay in history; the numbering
// restarted at 0.0.1 on 2026-10-29.
#pragma once

#define ARCTRAY_VERSION_MAJOR 0
#define ARCTRAY_VERSION_MINOR 0
#define ARCTRAY_VERSION_PATCH 1

#define ARCTRAY_STR_(x) #x
#define ARCTRAY_STR(x) ARCTRAY_STR_(x)
#define ARCTRAY_VERSION_STR \
  ARCTRAY_STR(ARCTRAY_VERSION_MAJOR) "." ARCTRAY_STR(ARCTRAY_VERSION_MINOR) "." ARCTRAY_STR(ARCTRAY_VERSION_PATCH)
