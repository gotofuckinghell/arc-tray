#!/usr/bin/env bash
# Canonical post-change check for ArcTray. Read-only: it builds and asserts behaviour that
# must never regress, without writing a single driver or registry value.
#
#   bash scripts/verify.sh            # build + invariants
#   bash scripts/verify.sh --live     # also read the card (needs the driver, works unelevated)
set -uo pipefail
cd "$(dirname "$0")/.." || exit 1
E=./arctray.exe
LIVE=0
[ "${1:-}" = "--live" ] && LIVE=1
fail=0
ok()   { printf '  ok   %s\n' "$1"; }
bad()  { printf '  FAIL %s\n' "$1"; fail=1; }
have() { "$@" >/dev/null 2>&1; }

echo "== build =="
have bash build.sh || { echo "  FAIL build.sh"; exit 1; }
[ -x "$E" ] && ok "arctray.exe built" || bad "arctray.exe missing"

echo "== version macros agree =="
v=$("$E" --about | head -1 | awk '{print $2}')
b=$(sed -n 's/.*ARCTRAY_VERSION_BASE \([0-9]*\).*/\1/p' src/version.h | head -1)
q=$(sed -n 's/.*ARCTRAY_VERSION_QUARTER \([0-9]*\).*/\1/p' src/version.h | head -1)
bs=$(sed -n 's/.*ARCTRAY_VERSION_BASE_STR "\([0-9]*\)".*/\1/p' src/version.h | head -1)
qs=$(sed -n 's/.*ARCTRAY_VERSION_QUARTER_STR "\([0-9]*\)".*/\1/p' src/version.h | head -1)
bw=$(sed -n 's/.*ARCTRAY_VERSION_BASE_WSTR L"\([0-9]*\)".*/\1/p' src/version.h | head -1)
qw=$(sed -n 's/.*ARCTRAY_VERSION_QUARTER_WSTR L"\([0-9]*\)".*/\1/p' src/version.h | head -1)
pad=$(printf '%03d' "$b"); quarter=$((q * 25)); [ "$q" -eq 0 ] && quarter=""
[ "$v" = "0.0.$pad$quarter" ] && ok "--about matches version.h ($v)" || bad "--about=$v, expected 0.0.$pad$quarter"
[ "$bs" = "$pad" ] && [ "$bw" = "$pad" ] && ok "BASE_STR/BASE_WSTR = $pad" || bad "base strings != $pad"
[ "$qs" = "$quarter" ] && [ "$qw" = "$quarter" ] && ok "QUARTER_STR/QUARTER_WSTR = ${quarter:-empty}" || bad "quarter strings != ${quarter:-empty}"
grep -q 'APP_VERSION ARCTRAY_VERSION_WSTR' src/tray.c && ok "tray uses the wide macro" || bad "tray macro outdated"

echo "== menu and tables still build =="
[ "$("$E" --menudump | grep -c '\[')" -gt 100 ] && ok "menu tree" || bad "menu tree"
[ "$("$E" --tweaks | grep -cE '^[a-z0-9-]+ +')" -ge 14 ] && ok "tweaks table >= 14" || bad "tweaks table"
[ "$("$E" --gfx | head -1 | awk '{print $2}')" = "9" ] && ok "9 3D features readable" || bad "gfx feature count"

if [ "$LIVE" = 1 ]; then
  echo "== live card (read-only) =="
  pl=$("$E" --state | sed -n 's/^PL *: sustained=\([0-9]*\) W.*/\1/p')
  [ -n "$pl" ] && ok "PL=${pl} W" || bad "--state cannot read PL"
fi

[ "$fail" -eq 0 ] && echo "PASS" || echo "FAIL"
exit "$fail"
