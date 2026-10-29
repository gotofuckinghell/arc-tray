// tweaks.c — the registry table and its read/write helpers.
// Everything is a plain HKLM/HKCU value, written only while the tray runs elevated.
#include "tweaks.h"
#include <stdio.h>
#include <string.h>

#define HKLM_SYS L"HKLM\\SYSTEM\\CurrentControlSet"
#define MMCSS L"HKLM\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile"

static const TweakDef TWEAKS[] = {
  {
    L"hags", L"Hardware-accelerated GPU scheduling", L"lower latency, reboot",
    L"HwSchMode=2 lets the GPU scheduler own VRAM instead of the CPU driver thread. "
    L"Lower input latency on some systems, stutter on others. Needs a reboot to take effect.",
    1, 1, {{ L"HKLM\\SYSTEM\\CurrentControlSet\\Control\\GraphicsDrivers", L"HwSchMode", 0, 2, 1, NULL, NULL }}
  },
  {
    L"mpo", L"Multiplane Overlay (MPO) workaround", L"fix stutter / flicker / black screens",
    L"Legacy MPO workaround (MPOHack=1 + DWM OverlayTestMode=5). Fixes stutter, flicker and "
    L"black screens on some multi-monitor / windowed setups. Experimental; a reboot helps.",
    0, 3, {
      { L"HKLM\\SOFTWARE\\Microsoft\\DirectX\\UserGpuPreferences", L"MPOHack", 0, 1, 0, NULL, NULL },
      { L"HKCU\\SOFTWARE\\Microsoft\\DirectX\\UserGpuPreferences", L"MPOHack", 0, 1, 0, NULL, NULL },
      { L"HKLM\\SOFTWARE\\Microsoft\\Windows\\Dwm", L"OverlayTestMode", 0, 5, 0, NULL, NULL }
    }
  },
  {
    L"gamedvr", L"Game DVR / background recording", L"stop background recording",
    L"Turns Game Bar background recording off machine-wide (GameDVR_Enabled=0 + AllowGameDVR=0). "
    L"Costs a few percent of FPS and adds capture overhead when left on.",
    0, 2, {
      { L"HKCU\\System\\GameConfigStore", L"GameDVR_Enabled", 0, 0, 1, NULL, NULL },
      { L"HKLM\\SOFTWARE\\Policies\\Microsoft\\Windows\\GameDVR", L"AllowGameDVR", 0, 0, 1, NULL, NULL }
    }
  },
  {
    L"fso", L"Fullscreen optimizations (global default)", L"true exclusive fullscreen",
    L"GameDVR_FSEBehaviorMode=2 makes games use true exclusive fullscreen instead of the "
    L"fullscreen-optimization flip path. Lower latency in some titles, worse alt-tab in others.",
    0, 1, {{ L"HKCU\\System\\GameConfigStore", L"GameDVR_FSEBehaviorMode", 0, 2, 0, NULL, NULL }}
  },
  {
    L"mmcss-games", L"MMCSS \"Games\" task priority", L"high priority while a game runs",
    L"Raises the multimedia scheduler class used while a game runs: GPU Priority=8, "
    L"Priority=6, Scheduling Category=High, SFIO Priority=High. Applies to newly started games.",
    0, 4, {
      { MMCSS L"\\Tasks\\Games", L"GPU Priority", 0, 8, 1, NULL, NULL },
      { MMCSS L"\\Tasks\\Games", L"Priority", 0, 6, 2, NULL, NULL },
      { MMCSS L"\\Tasks\\Games", L"Scheduling Category", 1, 0, 0, L"High", L"Medium" },
      { MMCSS L"\\Tasks\\Games", L"SFIO Priority", 1, 0, 0, L"High", L"Normal" }
    }
  },
  {
    L"system-responsiveness", L"MMCSS system responsiveness (0%)", L"no 20% CPU reserve",
    L"SystemResponsiveness=0 stops the multimedia service from reserving 20% of the CPU for "
    L"itself, so games get the full budget. Can make background audio glitchy on weak CPUs.",
    0, 1, {{ MMCSS, L"SystemResponsiveness", 0, 0, 20, NULL, NULL }}
  },
  {
    L"net-throttling", L"Network throttling limit off", L"no multimedia network throttling",
    L"NetworkThrottlingIndex=0xFFFFFFFF disables the 10-packets-per-ms multimedia throttle "
    L"that Windows applies while audio/video plays — removes a known network latency spike.",
    0, 1, {{ MMCSS, L"NetworkThrottlingIndex", 0, 0xFFFFFFFF, 10, NULL, NULL }}
  },
  {
    L"timer-resolution", L"Global high-resolution timers", L"0.5 ms timers, reboot",
    L"GlobalTimerResolutionRequests=1 lets a process request 0.5 ms timers globally instead of "
    L"being rounded up to the 15.6 ms tick. Needs a reboot; costs a little idle power.",
    1, 1, {{ HKLM_SYS L"\\Control\\Session Manager\\kernel", L"GlobalTimerResolutionRequests", 0, 1, 0, NULL, NULL }}
  },
  {
    L"win32-priority", L"Win32PrioritySeparation (foreground boost)", L"foreground quantum boost, reboot",
    L"Win32PrioritySeparation=0x26 gives the foreground thread quantum more weight than the "
    L"Windows default (0x2). Classic responsiveness tweak; a reboot is recommended.",
    1, 1, {{ HKLM_SYS L"\\Control\\PriorityControl", L"Win32PrioritySeparation", 0, 0x26, 2, NULL, NULL }}
  },
};

int tweaks_count(void) { return (int)(sizeof TWEAKS / sizeof TWEAKS[0]); }
const TweakDef *tweak_at(int i) { return (i >= 0 && i < tweaks_count()) ? &TWEAKS[i] : NULL; }

static int split_hive(const wchar_t *path, HKEY *root, const wchar_t **sub) {
  if (wcsncmp(path, L"HKCU\\", 5) == 0) { *root = HKEY_CURRENT_USER; *sub = path + 5; return 1; }
  if (wcsncmp(path, L"HKLM\\", 5) == 0) { *root = HKEY_LOCAL_MACHINE; *sub = path + 5; return 1; }
  *root = HKEY_LOCAL_MACHINE; *sub = path; return 1;
}

static LONG read_step(const TweakStep *s, DWORD *dw, wchar_t *sz, DWORD szcap) {
  HKEY root; const wchar_t *sub;
  split_hive(s->path, &root, &sub);
  HKEY key = NULL;
  LONG r = RegOpenKeyExW(root, sub, 0, KEY_QUERY_VALUE, &key);
  if (r != ERROR_SUCCESS) return r;
  DWORD type = 0, size = 0;
  if (s->is_string) {
    size = szcap * sizeof(wchar_t);
    r = RegQueryValueExW(key, s->value, NULL, &type, (BYTE *)sz, &size);
  } else {
    DWORD v = 0; size = sizeof v;
    r = RegQueryValueExW(key, s->value, NULL, &type, (BYTE *)&v, &size);
    if (r == ERROR_SUCCESS) *dw = v;
  }
  RegCloseKey(key);
  return r;
}

int tweak_state(const TweakDef *t) {
  int on = 0, off = 0;
  for (int i = 0; i < t->nsteps; i++) {
    const TweakStep *s = &t->steps[i];
    if (s->is_string) {
      wchar_t buf[128] = {0};
      LONG rc = read_step(s, NULL, buf, 128);
      if (rc == ERROR_FILE_NOT_FOUND) { off++; continue; } // absent = default
      if (rc != ERROR_SUCCESS) return -1;
      if (_wcsicmp(buf, s->on_sz) == 0) on++;
      else if (_wcsicmp(buf, s->off_sz) == 0) off++;
      else return -1;
    } else {
      DWORD v = 0;
      LONG rc = read_step(s, &v, NULL, 0);
      if (rc == ERROR_FILE_NOT_FOUND) { off++; continue; } // absent = Windows default = not applied
      if (rc != ERROR_SUCCESS) return -1;
      if (v == s->on_dw) on++;
      else if (v == s->off_dw) off++;
      else return -1;
    }
  }
  if (on == t->nsteps) return 1;
  if (off == t->nsteps) return 0;
  return -1; // mixed
}

static int write_step(const TweakStep *s, int enable, wchar_t *err, size_t errcap) {
  const wchar_t *path = s->path;
  HKEY root = HKEY_LOCAL_MACHINE;
  if (wcsncmp(path, L"HKCU\\", 5) == 0) { root = HKEY_CURRENT_USER; path += 5; }
  else if (wcsncmp(path, L"HKLM\\", 5) == 0) { root = HKEY_LOCAL_MACHINE; path += 5; }

  HKEY key = NULL;
  LONG r = RegCreateKeyExW(root, path, 0, NULL, 0, KEY_SET_VALUE, NULL, &key, NULL);
  if (r != ERROR_SUCCESS) { swprintf(err, errcap, L"open failed (%ld): %s", r, path); return 0; }
  if (s->is_string) {
    const wchar_t *val = enable ? s->on_sz : s->off_sz;
    r = RegSetValueExW(key, s->value, 0, REG_SZ, (const BYTE *)val,
                       (DWORD)((wcslen(val) + 1) * sizeof(wchar_t)));
  } else {
    DWORD v = enable ? s->on_dw : s->off_dw;
    r = RegSetValueExW(key, s->value, 0, REG_DWORD, (const BYTE *)&v, sizeof v);
  }
  RegCloseKey(key);
  if (r != ERROR_SUCCESS) { swprintf(err, errcap, L"write failed (%ld): %s\\%s", r, path, s->value); return 0; }
  return 1;
}

void tweak_debug(const TweakDef *t) {
  for (int i = 0; i < t->nsteps; i++) {
    const TweakStep *s = &t->steps[i];
    if (s->is_string) {
      wchar_t buf[128] = {0};
      LONG r = read_step(s, NULL, buf, 128);
      printf("    step %d: rc=%ld value=%ls\n", i, r, r == 0 ? buf : L"-");
    } else {
      DWORD v = 0;
      LONG r = read_step(s, &v, NULL, 0);
      printf("    step %d: rc=%ld value=%lu (on=%lu off=%lu)\n", i, r, r == 0 ? v : 0, s->on_dw, s->off_dw);
    }
  }
}

int tweak_apply(const TweakDef *t, int enable, wchar_t *err, size_t errcap) {
  for (int i = 0; i < t->nsteps; i++)
    if (!write_step(&t->steps[i], enable, err, errcap)) return 0;
  return 1;
}

int tweak_revert(const TweakDef *t, wchar_t *err, size_t errcap) {
  for (int i = 0; i < t->nsteps; i++) {
    const TweakStep *s = &t->steps[i];
    const wchar_t *path = s->path;
    HKEY root = HKEY_LOCAL_MACHINE;
    if (wcsncmp(path, L"HKCU\\", 5) == 0) { root = HKEY_CURRENT_USER; path += 5; }
    else if (wcsncmp(path, L"HKLM\\", 5) == 0) { root = HKEY_LOCAL_MACHINE; path += 5; }
    HKEY key = NULL;
    if (RegOpenKeyExW(root, path, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) continue; // nothing to delete
    LONG r = RegDeleteValueW(key, s->value);
    RegCloseKey(key);
    if (r != ERROR_SUCCESS && r != ERROR_FILE_NOT_FOUND) {
      swprintf(err, errcap, L"delete failed (%ld): %s\\%s", r, path, s->value);
      return 0;
    }
  }
  return 1;
}
