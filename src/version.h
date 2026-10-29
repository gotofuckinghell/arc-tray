// version.h — single source of truth for the version.
//
// Scheme (pre-1.0, by the user's request):
//   0.0.<base><quarters>
//   * base      — three digits, +1 for every added option/feature
//   * quarters  — two digits, appended only when non-zero:
//                 0 bugs = "", 1 = "25", 2 = "50", 3 = "75"
//                 (one fixed bug adds 0.25, so four bugs make one feature step)
//   Examples: 0.0.004 (four features), 0.0.00425 (+1 bug), 0.0.00450 (+2 bugs).
//   The git tag is "v" + the same string. 1.0.0 is reserved for a frozen interface.
#pragma once

#define ARCTRAY_VERSION_MAJOR 0
#define ARCTRAY_VERSION_MINOR 0
#define ARCTRAY_VERSION_BASE 1     // +1 per new option
#define ARCTRAY_VERSION_QUARTER 1  // +1 per fixed bug (= +0.25)

// Literal strings on purpose: the preprocessor cannot zero-pad or evaluate arithmetic
// inside #stringify. Keep BASE_STR / QUARTER_STR in sync with the numbers above.
#define ARCTRAY_VERSION_BASE_STR "001"
#define ARCTRAY_VERSION_QUARTER_STR "25"   // "", "25", "50", "75"

#define ARCTRAY_VERSION_STR \
  "0.0." ARCTRAY_VERSION_BASE_STR ARCTRAY_VERSION_QUARTER_STR

#define ARCTRAY_VERSION_BASE_WSTR L"001"
#define ARCTRAY_VERSION_QUARTER_WSTR L"25"
#define ARCTRAY_VERSION_WSTR \
  L"0.0." ARCTRAY_VERSION_BASE_WSTR ARCTRAY_VERSION_QUARTER_WSTR
