// tweaks.h — reversible latency / responsiveness registry tweaks
#pragma once
#include <windows.h>

#define TWEAK_MAX_STEPS 4

typedef struct {
  const wchar_t *path;    // hive-qualified key
  const wchar_t *value;   // value name
  int is_string;          // 0 = REG_DWORD, 1 = REG_SZ
  DWORD on_dw, off_dw;
  const wchar_t *on_sz, *off_sz;
} TweakStep;

typedef struct {
  const wchar_t *id;
  const wchar_t *name;
  const wchar_t *short_desc; // one line, shown in the menu next to the name
  const wchar_t *desc;       // full text for the balloon / help dialog
  int needs_reboot;
  int nsteps;
  TweakStep steps[TWEAK_MAX_STEPS];
} TweakDef;

int tweaks_count(void);
const TweakDef *tweak_at(int index);
// 1 = all steps at "on", 0 = all at "off", -1 = mixed / missing / unreadable
int tweak_state(const TweakDef *t);
// writes every step to the requested state; returns 1 on success
int tweak_apply(const TweakDef *t, int enable, wchar_t *err, size_t errcap);
// deletes the values this app manages (back to Windows defaults)
int tweak_revert(const TweakDef *t, wchar_t *err, size_t errcap);
// diagnostic: print every step's read result
void tweak_debug(const TweakDef *t);
