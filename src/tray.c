// tray.c — headless Arc tuner: Win32 tray menu + CLI. No Electron, no GUI window.
#define WIN32_LEAN_AND_MEAN
#include "arc.h"
#include "tweaks.h"
#include "gfx.h"
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <shellapi.h>
#include <math.h>
#include <stdarg.h>

#define WM_TRAY (WM_APP + 1)
#define ID_TRAY 1

typedef struct { char name[48]; int pl, core, volt_mv, temp, fan; } Profile; // fan: -1 auto, else percent

static ArcDev g_arc;
static ArcState g_state;
static Profile g_prof[16];
static int g_profc = 0;
static char g_ini[MAX_PATH];

static void logf_(const char *fmt, ...) {
  char path[MAX_PATH];
  GetModuleFileNameA(NULL, path, MAX_PATH);
  char *slash = strrchr(path, '\\');
  if (!slash) return;
  *slash = 0;
  strncat_s(path, MAX_PATH, "\\arctray.log", _TRUNCATE);
  // never append through a junction/symlink: an elevated process writing to a
  // redirected path could corrupt an arbitrary file
  DWORD attr = GetFileAttributesA(path);
  if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_REPARSE_POINT)) return;
  int truncate = 0;
  WIN32_FILE_ATTRIBUTE_DATA fa;
  if (GetFileAttributesExA(path, GetFileExInfoStandard, &fa) && fa.nFileSizeLow > 65536) truncate = 1;
  FILE *f = NULL;
  if (fopen_s(&f, path, truncate ? "w" : "a") == 0 && f) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
  }
}

static void ini_path(char *out, size_t cap) {
  // portable first: arctray.ini next to the exe when that folder is writable
  char exe[MAX_PATH];
  GetModuleFileNameA(NULL, exe, MAX_PATH);
  char *slash = strrchr(exe, '\\');
  if (slash) {
    *slash = 0;
    char cand[MAX_PATH];
    snprintf(cand, sizeof cand, "%s\\arctray.ini", exe);
    if (GetFileAttributesA(cand) != INVALID_FILE_ATTRIBUTES) { snprintf(out, cap, "%s", cand); return; }
    FILE *probe = fopen(cand, "ab");
    if (probe) { fclose(probe); snprintf(out, cap, "%s", cand); return; }
  }
  char *appdata = getenv("APPDATA");
  if (!appdata) appdata = ".";
  snprintf(out, cap, "%s\\ArcTray", appdata);
  CreateDirectoryA(out, NULL);
  size_t n = strlen(out);
  if (n + 12 >= cap) return;                 // "<dir>\arctray.ini" must fit
  snprintf(out + n, cap - n, "\\arctray.ini");
}

static void write_default_ini(const char *path) {
  FILE *f = fopen(path, "w");
  if (!f) return;
  fputs("; ArcTray - headless Intel Arc tuner\n"
        "; profile=Name,PowerW,CoreOffsetMHz,VoltOffsetmV,TempC,Fan(auto|30..100)\n"
        "profile=Gaming,375,0,0,90,auto\n"
        "profile=Quiet,150,0,0,85,auto\n"
        "profile=Stock,230,0,0,90,auto\n", f);
  fclose(f);
}

static void load_ini(void) {
  ini_path(g_ini, sizeof g_ini);
  FILE *f = fopen(g_ini, "r");
  if (f) {
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); f = NULL; }
  }
  if (!f) { write_default_ini(g_ini); f = fopen(g_ini, "r"); }
  g_profc = 0;
  if (!f) return;
  char line[256];
  while (fgets(line, sizeof line, f) && g_profc < 16) {
    char *s = line;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == ';' || *s == '#' || *s == '\n' || *s == 0) continue;
    if (strncmp(s, "profile=", 8) != 0) continue;
    char buf[200];
    snprintf(buf, sizeof buf, "%s", s + 8);
    char *nl = strpbrk(buf, "\r\n");
    if (nl) *nl = 0;
    char *parts[6] = {0};
    int n = 0;
    char *save = NULL;
    for (char *tok = strtok_s(buf, ",", &save); tok && n < 6; tok = strtok_s(NULL, ",", &save)) parts[n++] = tok;
    if (n < 6) continue;
    Profile *p = &g_prof[g_profc];
    memset(p, 0, sizeof *p);
    snprintf(p->name, sizeof p->name, "%s", parts[0]);
    p->pl = atoi(parts[1]);
    p->core = atoi(parts[2]);
    p->volt_mv = atoi(parts[3]);
    p->temp = atoi(parts[4]);
    if (_stricmp(parts[5], "auto") == 0) p->fan = -1;
    else p->fan = atoi(parts[5]);
    g_profc++;
  }
  fclose(f);
}

static const int PL_STEPS[] = {115, 150, 190, 230, 252, 276, 300, 325, 350, 375, 398, 420};
static const int CORE_STEPS[] = {0, 25, 50, 75, 100, 125, 150, 175, 200, 225, 250, 275, 300};
static const int TEMP_STEPS[] = {60, 65, 70, 75, 80, 85, 90, 95, 100, 105, 110, 115};
static const int FAN_STEPS[] = {30, 40, 50, 60, 70, 80, 90, 100};
static const int GFX_LL[] = {0, 1, 2};
static const unsigned GFX_FLIP[] = {1, 2, 4, 8, 16, 32};
static const int GFX_FPS[] = {0, 30, 60, 90, 120, 144, 165, 240};

static const int VOLT_STEPS[] = {0, 25, 50, 75, 100, 125, 150, 175, 200, 225, 250, 288};

static int mark_at(const int *arr, int n, double v, double tol) {
  int best = -1;
  double bd = 1e9;
  for (int i = 0; i < n; i++) {
    double d = fabs(v - arr[i]);
    if (d < bd) { bd = d; best = i; }
  }
  return (best >= 0 && bd <= tol) ? best : -1;
}

static void fmt_state(wchar_t *out, size_t cap) {
  double pl = g_state.pl_sustained > 0 ? g_state.pl_sustained : g_state.pl_igcl;
  swprintf(out, cap, L"Arc A770 — %.0f W · %.0f MHz · %.0f\x00B0""C%s",
           pl, g_state.gpu_mhz, g_state.gpu_temp,
           g_state.elevated ? L"" : L"  (no admin)");
}

// ---- tray icon lifecycle -------------------------------------------------
// Shell_NotifyIcon fails with E_FAIL (0x80004005) when the logon task starts
// before Explorer has created its tray window, and the icon never appears.
// Keep the NOTIFYICONDATA in one place, retry on a timer, and re-add it when
// Explorer broadcasts TaskbarCreated (explorer.exe restart).
static NOTIFYICONDATAW g_nid;
static int g_tray_added = 0;
static int g_tray_attempts = 0;
static UINT g_taskbar_created = 0;
#define TIMER_TRAY_RETRY 8

static int tray_add_once(void) {
  if (g_tray_added) return 1;
  if (Shell_NotifyIconW(NIM_ADD, &g_nid)) { g_tray_added = 1; return 1; }
  return 0;
}

static void balloon(HWND hwnd, const wchar_t *title, const wchar_t *text, DWORD flags) {
  NOTIFYICONDATAW nid;
  nid = g_nid;
  nid.hWnd = hwnd;
  nid.uID = ID_TRAY;
  nid.uFlags = NIF_INFO;
  wcsncpy_s(nid.szInfoTitle, 64, title, _TRUNCATE);
  wcsncpy_s(nid.szInfo, 256, text, _TRUNCATE);
  nid.dwInfoFlags = flags;
  Shell_NotifyIconW(NIM_MODIFY, &nid);
}

static void show_result(HWND hwnd, int ok, const char *msg) {
  wchar_t w[256];
  MultiByteToWideChar(CP_UTF8, 0, msg, -1, w, 256);
  balloon(hwnd, ok ? L"ArcTray" : L"ArcTray — failed", w, ok ? NIIF_INFO : NIIF_WARNING);
  arc_read(&g_arc, &g_state);
}

static void apply_profile(HWND hwnd, const Profile *p) {
  char msg[256], all[512] = {0};
  int ok = 1;
  if (p->pl > 0) {
    if (arc_set_power(&g_arc, p->pl, msg, sizeof msg)) { strncat_s(all, sizeof all, msg, _TRUNCATE); strncat_s(all, sizeof all, "  ", _TRUNCATE); }
    else { ok = 0; strncat_s(all, sizeof all, msg, _TRUNCATE); }
  }
  if (p->core != 0 || ok) {
    if (arc_set_core(&g_arc, p->core, msg, sizeof msg)) { strncat_s(all, sizeof all, msg, _TRUNCATE); strncat_s(all, sizeof all, "  ", _TRUNCATE); }
    else { ok = 0; strncat_s(all, sizeof all, msg, _TRUNCATE); }
  }
  if (p->volt_mv != 0) {
    if (arc_set_volt(&g_arc, p->volt_mv, msg, sizeof msg)) { strncat_s(all, sizeof all, msg, _TRUNCATE); strncat_s(all, sizeof all, "  ", _TRUNCATE); }
    else { ok = 0; strncat_s(all, sizeof all, msg, _TRUNCATE); }
  }
  if (p->temp > 0) {
    if ((p->temp > 90 ? arc_set_temp_ext(&g_arc, p->temp, msg, sizeof msg) : arc_set_temp(&g_arc, p->temp, msg, sizeof msg))) { strncat_s(all, sizeof all, msg, _TRUNCATE); strncat_s(all, sizeof all, "  ", _TRUNCATE); }
    else { ok = 0; strncat_s(all, sizeof all, msg, _TRUNCATE); }
  }
  if (p->fan == -1 || p->fan > 0) {
    if (arc_set_fan(&g_arc, p->fan == -1 ? 0 : 1, p->fan == -1 ? 0 : p->fan, msg, sizeof msg)) {
      strncat_s(all, sizeof all, msg, _TRUNCATE);
    } else { ok = 0; strncat_s(all, sizeof all, msg, _TRUNCATE); }
  }
  show_result(hwnd, ok, all);
}

#include "version.h"
#define APP_VERSION L"" ARCTRAY_STR(ARCTRAY_VERSION_MAJOR) L"." ARCTRAY_STR(ARCTRAY_VERSION_MINOR) L"." ARCTRAY_STR(ARCTRAY_VERSION_PATCH)
#define URL_GITHUB L"https://github.com/gotofuckinghell"
#define URL_GITLAB L"https://gitlab.com/grubyrams"
#define URL_TELEGRAM L"https://t.me/hwretard"
#define TASK_NAME L"ArcTray Elevated"
static int g_task_state = -1;
static DWORD g_task_checked = 0;

static int run_wait(const wchar_t *exe, wchar_t *cmd, DWORD *code) {
  wchar_t full[MAX_PATH];
  if (wcschr(exe, L'\\') == NULL) {
    wchar_t sysdir[MAX_PATH];
    UINT n = GetSystemDirectoryW(sysdir, MAX_PATH);
    if (n > 0 && n < MAX_PATH) swprintf(full, MAX_PATH, L"%s\\%s", sysdir, exe);
    else wcscpy_s(full, MAX_PATH, exe);
    exe = full;
  }
  STARTUPINFOW si;
  PROCESS_INFORMATION pi;
  memset(&si, 0, sizeof si);
  memset(&pi, 0, sizeof pi);
  si.cb = sizeof si;
  si.dwFlags = STARTF_USESHOWWINDOW;
  si.wShowWindow = SW_HIDE;
  if (!CreateProcessW(exe, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) return 0;
  WaitForSingleObject(pi.hProcess, 15000);
  DWORD ec = (DWORD)-1;
  GetExitCodeProcess(pi.hProcess, &ec);
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  if (code) *code = ec;
  return 1;
}

// The logon task is the honest source of truth: it exists -> autostart is on.
static int autostart_enabled(void) {
  DWORD now = GetTickCount();
  if (g_task_state >= 0 && now - g_task_checked < 20000) return g_task_state;
  wchar_t cmd[512];
  swprintf(cmd, 512, L"schtasks.exe /Query /TN \"%s\"", TASK_NAME);
  DWORD ec = 1;
  run_wait(L"schtasks.exe", cmd, &ec);
  g_task_state = (ec == 0);
  g_task_checked = now;
  return g_task_state;
}

static int autostart_set(int enable) {
  wchar_t exe[MAX_PATH];
  wchar_t cmd[1024];
  GetModuleFileNameW(NULL, exe, MAX_PATH);
  if (enable)
    swprintf(cmd, 1024, L"schtasks.exe /Create /TN \"%s\" /TR \"%s\" /SC ONLOGON /RL HIGHEST /F", TASK_NAME, exe);
  else
    swprintf(cmd, 1024, L"schtasks.exe /Delete /TN \"%s\" /F", TASK_NAME);
  logf_("autostart: %ls", cmd);
  DWORD ec = 1;
  int started = run_wait(L"schtasks.exe", cmd, &ec);
  logf_("autostart result: started=%d ec=%lu lasterr=%lu", started, ec, GetLastError());
  int ok = started && ec == 0;
  g_task_state = -1;
  g_task_checked = 0;
  return ok;
}

static int g_open = 0;
static HWND g_hwnd = NULL;
#define IDLE_TIMER 7

static int ensure_open(void) {
  if (g_open) return 1;
  char err[256];
  if (!arc_open(&g_arc, err, sizeof err)) { return 0; }
  g_open = 1;
  return 1;
}

static void release_hw(void) {
  if (!g_open) return;
  logf_("release start");
  arc_close(&g_arc);
  logf_("release done");
  g_open = 0;
}

// ---- graphics snapshot (--gfx-save / --gfx-restore) -------------------------
static int gfx_snap_path(char *out, size_t n, const char *file) {
  if (file && *file) { snprintf(out, n, "%s", file); return 1; }
  const char *base = getenv("APPDATA");
  if (!base || !*base) return 0;
  char dir[MAX_PATH];
  int w = snprintf(dir, sizeof dir, "%s\\ArcTray", base);
  if (w < 0 || w >= (int)sizeof dir) return 0;
  DWORD attr = GetFileAttributesA(dir);
  if (attr == INVALID_FILE_ATTRIBUTES) {
    if (!CreateDirectoryA(dir, NULL)) return 0;
  } else if (attr & FILE_ATTRIBUTE_REPARSE_POINT) {
    return 0;                     // never write a config through a junction/symlink
  }
  w = snprintf(out, n, "%s\\gfx-snapshot.ini", dir);
  return w > 0 && w < (int)n;
}

static int gfx_save(const char *path) {
  GfxFeature f[64];
  int n = gfx_caps(&g_arc, f, 64);
  if (n <= 0) return 0;
  FILE *fp = fopen(path, "w");
  if (!fp) return 0;
  fprintf(fp, "# ArcTray graphics snapshot\n# name id enable value\n");
  int saved = 0;
  for (int i = 0; i < n; i++) {
    int en = -1; unsigned v = 0;
    if (!gfx_get(&g_arc, f[i].id, &en, &v)) continue;
    fprintf(fp, "%s %d %d %u\n", f[i].name, f[i].id, en, v);
    saved++;
  }
  fclose(fp);
  printf("saved %d features to %s\n", saved, path);
  return saved > 0;
}

static int gfx_restore(const char *path) {
  FILE *fp = fopen(path, "r");
  if (!fp) { fprintf(stderr, "cannot open %s\n", path); return 0; }
  char line[256];
  int done = 0, bad = 0;
  while (fgets(line, sizeof line, fp)) {
    if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
    char name[64]; int id = -1, en = 1; unsigned v = 0;
    if (sscanf(line, "%63s %d %d %u", name, &id, &en, &v) != 4 || id < 0) { bad++; continue; }
    int ok = gfx_set(&g_arc, id, en, v);
    int ren = -1; unsigned rv = 0;
    int rd = gfx_get(&g_arc, id, &ren, &rv);
    int match = rd && rv == v;
    printf("  %-14s -> %u : %s%s\n", name, v,
           ok ? "written" : "refused", match ? ", read back ok" : ", MISMATCH");
    if (ok && match) done++; else bad++;
  }
  fclose(fp);
  printf("restored %d, problems %d (%s)\n", done, bad, path);
  return bad == 0 && done > 0;
}

// ---- graphics change watcher (--gfx-watch[=seconds]) ------------------------
static void watch_stamp(char *out, size_t n) {
  time_t now = time(NULL);
  struct tm lt;
  localtime_s(&lt, &now);
  strftime(out, n, "%Y-%m-%d %H:%M:%S", &lt);
}

static int gfx_watch_path(char *out, size_t n) {
  const char *base = getenv("APPDATA");
  if (!base || !*base) return 0;
  char dir[MAX_PATH];
  int w = snprintf(dir, sizeof dir, "%s\\ArcTray", base);
  if (w < 0 || w >= (int)sizeof dir) return 0;
  DWORD attr = GetFileAttributesA(dir);
  if (attr == INVALID_FILE_ATTRIBUTES) {
    if (!CreateDirectoryA(dir, NULL)) return 0;
  } else if (attr & FILE_ATTRIBUTE_REPARSE_POINT) {
    return 0;
  }
  w = snprintf(out, n, "%s\\gfx-watch.log", dir);
  if (w < 0 || w >= (int)n) return 0;
  WIN32_FILE_ATTRIBUTE_DATA fa;
  if (GetFileAttributesExA(out, GetFileExInfoStandard, &fa) && fa.nFileSizeLow > (1u << 20)) {
    char old[MAX_PATH];
    if (snprintf(old, sizeof old, "%s.1", out) > 0 && (int)strlen(old) < (int)sizeof old)
      MoveFileExA(out, old, MOVEFILE_REPLACE_EXISTING);
  }
  return 1;
}

typedef struct { int id; int valid; int en; unsigned v; } WatchPrev;

static int gfx_watch_loop(int interval_s) {
  char logp[MAX_PATH], ts[32];
  if (!gfx_watch_path(logp, sizeof logp)) return 2;
  FILE *log = fopen(logp, "a");
  if (!log) { fprintf(stderr, "cannot open %s\n", logp); return 2; }

  GfxFeature f[64];
  int n = gfx_caps(&g_arc, f, 64);
  if (n <= 0) { fprintf(stderr, "no gfx features\n"); fclose(log); return 1; }
  WatchPrev prev[64];
  memset(prev, 0, sizeof prev);

  watch_stamp(ts, sizeof ts);
  fprintf(log, "%s watch start: %d features, interval %d s\n", ts, n, interval_s);
  for (int i = 0; i < n; i++) {
    prev[i].id = f[i].id;
    prev[i].valid = gfx_get(&g_arc, f[i].id, &prev[i].en, &prev[i].v);
    fprintf(log, "%s   baseline %-14s enable=%d value=%u%s\n", ts, f[i].name,
            prev[i].en, prev[i].v, prev[i].valid ? "" : "  (unreadable)");
  }
  fflush(log);
  printf("watching %d features every %d s -> %s\n", n, interval_s, logp);

  unsigned beats = 0;
  for (;;) {
    Sleep((DWORD)interval_s * 1000);
    beats++;
    for (int i = 0; i < n; i++) {
      int en = -1; unsigned v = 0;
      int ok = gfx_get(&g_arc, f[i].id, &en, &v);
      if (!ok) {
        if (prev[i].valid) {
          watch_stamp(ts, sizeof ts);
          fprintf(log, "%s %-14s now unreadable (was enable=%d value=%u)\n", ts, f[i].name, prev[i].en, prev[i].v);
          prev[i].valid = 0;
          fflush(log);
        }
        continue;
      }
      if (!prev[i].valid) {
        watch_stamp(ts, sizeof ts);
        fprintf(log, "%s %-14s became readable enable=%d value=%u\n", ts, f[i].name, en, v);
      } else if (en != prev[i].en || v != prev[i].v) {
        watch_stamp(ts, sizeof ts);
        fprintf(log, "%s %-14s %d/%u -> %d/%u\n", ts, f[i].name, prev[i].en, prev[i].v, en, v);
      } else {
        continue;
      }
      prev[i].en = en; prev[i].v = v; prev[i].valid = 1;
      fflush(log);
    }
    if (beats % 120 == 0) {           // ~1 h at 30 s
      watch_stamp(ts, sizeof ts);
      fprintf(log, "%s heartbeat (%u polls)\n", ts, beats);
      fflush(log);
    }
  }
  return 0;
}

static HMENU build_menu(void) {
  if (!ensure_open()) {
    HMENU root = CreatePopupMenu();
    AppendMenuW(root, MF_STRING | MF_GRAYED, 0, L"ArcTray — GPU/IGCL unavailable");
    AppendMenuW(root, MF_STRING, 9003, L"Retry");
    AppendMenuW(root, MF_SEPARATOR, 0, NULL);
    AppendMenuW(root, MF_STRING, 9004, L"Exit");
    return root;
  }
  if (g_hwnd) SetTimer(g_hwnd, IDLE_TIMER, 20000, NULL);
  arc_read(&g_arc, &g_state);
  HMENU root = CreatePopupMenu();
  wchar_t head[128];
  fmt_state(head, 128);
  AppendMenuW(root, MF_STRING | MF_GRAYED, 0, head);
  AppendMenuW(root, MF_SEPARATOR, 0, NULL);

  if (g_profc > 0) {
    HMENU prof = CreatePopupMenu();
    for (int i = 0; i < g_profc; i++) {
      wchar_t label[192];
      swprintf(label, 192, L"%S  —  %d W, %+d MHz, %d\x00B0""C", g_prof[i].name, g_prof[i].pl, g_prof[i].core, g_prof[i].temp);
      AppendMenuW(prof, MF_STRING, 1000 + i, label);
    }
    AppendMenuW(root, MF_POPUP, (UINT_PTR)prof, L"Profiles");
  }

  const int nPL = (int)(sizeof PL_STEPS / sizeof PL_STEPS[0]);
  const int nCORE = (int)(sizeof CORE_STEPS / sizeof CORE_STEPS[0]);
  const int nTEMP = (int)(sizeof TEMP_STEPS / sizeof TEMP_STEPS[0]);
  const int nFAN = (int)(sizeof FAN_STEPS / sizeof FAN_STEPS[0]);
  const int nVOLT = (int)(sizeof VOLT_STEPS / sizeof VOLT_STEPS[0]);
  double curPL = g_state.pl_sustained > 0 ? g_state.pl_sustained : g_state.pl_igcl;

  HMENU pl = CreatePopupMenu();
  int mPL = mark_at(PL_STEPS, nPL, curPL, 12.0);
  for (int i = 0; i < nPL; i++) {
    wchar_t label[32];
    swprintf(label, 32, L"%d W%s", PL_STEPS[i], i == mPL ? L"  ●" : L"");
    AppendMenuW(pl, MF_STRING, 2000 + i, label);
  }
  wchar_t plHead[48];
  swprintf(plHead, 48, L"Power limit  (%.0f W)", curPL);
  AppendMenuW(root, MF_POPUP, (UINT_PTR)pl, plHead);

  HMENU core = CreatePopupMenu();
  int mCORE = mark_at(CORE_STEPS, nCORE, g_state.core_off, 12.0);
  for (int i = 0; i < nCORE; i++) {
    wchar_t label[32];
    swprintf(label, 32, L"%+d MHz%s", CORE_STEPS[i], i == mCORE ? L"  ●" : L"");
    AppendMenuW(core, MF_STRING, 3000 + i, label);
  }
  wchar_t coreHead[48];
  swprintf(coreHead, 48, L"Core offset  (%+.0f MHz)", g_state.core_off);
  AppendMenuW(root, MF_POPUP, (UINT_PTR)core, coreHead);

  HMENU tl = CreatePopupMenu();
  int mTEMP = mark_at(TEMP_STEPS, nTEMP, g_state.temp_limit, 2.5);
  for (int i = 0; i < nTEMP; i++) {
    wchar_t label[32];
    swprintf(label, 32, L"%d \x00B0""C%s", TEMP_STEPS[i], i == mTEMP ? L"  ●" : L"");
    AppendMenuW(tl, MF_STRING, 4000 + i, label);
  }
  wchar_t tlHead[56];
  swprintf(tlHead, 56, L"Temperature limit  (%.0f \x00B0""C)", g_state.temp_limit);
  AppendMenuW(root, MF_POPUP, (UINT_PTR)tl, tlHead);

  HMENU volt = CreatePopupMenu();
  int mVOLT = mark_at(VOLT_STEPS, nVOLT, g_state.volt_off * 1000.0, 12.0);
  for (int i = 0; i < nVOLT; i++) {
    wchar_t label[32];
    swprintf(label, 32, L"%+d mV%s", VOLT_STEPS[i], i == mVOLT ? L"  ●" : L"");
    AppendMenuW(volt, MF_STRING, 6000 + i, label);
  }
  wchar_t voltHead[56];
  swprintf(voltHead, 56, L"Voltage offset  (%+.0f mV)", g_state.volt_off * 1000.0);
  AppendMenuW(root, MF_POPUP, (UINT_PTR)volt, voltHead);

  HMENU fan = CreatePopupMenu();
  AppendMenuW(fan, MF_STRING, 5000, g_state.fan_mode == 0 ? L"Auto  ●" : L"Auto");
  for (int i = 0; i < nFAN; i++) {
    wchar_t label[32];
    swprintf(label, 32, L"%d%%%s", FAN_STEPS[i], (g_state.fan_mode != 0 && mark_at(FAN_STEPS, nFAN, g_state.fan_set_pct, 2.0) == i) ? L"  ●" : L"");
    AppendMenuW(fan, MF_STRING, 5001 + i, label);
  }
  AppendMenuW(root, MF_POPUP, (UINT_PTR)fan, L"Fan");

  AppendMenuW(root, MF_SEPARATOR, 0, NULL);
  AppendMenuW(root, MF_STRING, 9001, L"Open config file");
  AppendMenuW(root, MF_STRING, 9002, L"Reload config");
  AppendMenuW(root, MF_STRING, 9003, L"Refresh");
  // ---- Graphics (non-OC IGCL features) ----
  {
    static const wchar_t *LLN[] = {L"off  — driver default", L"on  — cuts input lag", L"on + boost  — lowest lag, more power"};
    static const wchar_t *FLIPN[] = {
      L"application default  — game decides",
      L"vsync off  — tearing, lowest lag",
      L"vsync on  — no tearing, adds lag",
      L"smooth sync  — reduces tearing on async flips",
      L"speed frame  — app-unaware triple buffering",
      L"capped to panel rate  — smart vsync"};
    int en = 0;
    HMENU gfx = CreatePopupMenu();

    HMENU ll = CreatePopupMenu();
    unsigned curLL = 0;
    if (gfx_get(&g_arc, 16, &en, &curLL)) {
      for (int i = 0; i < 3; i++) {
        wchar_t l[128];
        swprintf(l, 128, L"%s%s", LLN[i], curLL == (unsigned)GFX_LL[i] ? L"  ●" : L"");
        AppendMenuW(ll, MF_STRING, 8000 + i, l);
      }
      wchar_t head[96];
      swprintf(head, 96, L"Low latency  (%s)", curLL < 3 ? LLN[curLL] : L"?");
      AppendMenuW(gfx, MF_POPUP, (UINT_PTR)ll, head);
    }

    unsigned curFlip = 0;
    if (gfx_get(&g_arc, 9, &en, &curFlip)) {
      HMENU fm = CreatePopupMenu();
      for (int i = 0; i < 6; i++) {
        wchar_t l[160];
        swprintf(l, 160, L"%s%s", FLIPN[i], curFlip == GFX_FLIP[i] ? L"  ●" : L"");
        AppendMenuW(fm, MF_STRING, 8100 + i, l);
      }
      wchar_t head[96];
      swprintf(head, 96, L"VSync / flip mode  (0x%x)", curFlip);
      AppendMenuW(gfx, MF_POPUP, (UINT_PTR)fm, head);
    }

    unsigned curFps = 0;
    if (gfx_get(&g_arc, 2, &en, &curFps)) {
      HMENU fp = CreatePopupMenu();
      for (int i = 0; i < 8; i++) {
        wchar_t l[96];
        if (GFX_FPS[i] == 0) swprintf(l, 96, L"off  — uncapped%s", curFps == 0 ? L"  ●" : L"");
        else swprintf(l, 96, L"%d fps  — cap frame rate%s", GFX_FPS[i], curFps == (unsigned)GFX_FPS[i] ? L"  ●" : L"");
        AppendMenuW(fp, MF_STRING, 8200 + i, l);
      }
      wchar_t head[96];
      swprintf(head, 96, L"FPS limit  (%u)", curFps);
      AppendMenuW(gfx, MF_POPUP, (UINT_PTR)fp, head);
    }
    // XeSS frame generation override (17)
    unsigned curFg = 0;
    if (gfx_get(&g_arc, 17, &en, &curFg)) {
      static const wchar_t *FGN[] = {L"application choice  — game decides", L"2x  — 1 generated frame", L"3x  — 2 generated frames", L"4x  — 3 generated frames"};
      HMENU fgm = CreatePopupMenu();
      for (int i = 0; i < 4; i++) {
        wchar_t l[128];
        swprintf(l, 128, L"%s%s", FGN[i], curFg == (unsigned)i ? L"  ●" : L"");
        AppendMenuW(fgm, MF_STRING, 8300 + i, l);
      }
      wchar_t head[96];
      swprintf(head, 96, L"Frame generation (XeSS FG)  (%s)", curFg < 4 ? FGN[curFg] : L"?");
      AppendMenuW(gfx, MF_POPUP, (UINT_PTR)fgm, head);
    }
    // CMAA (4) and emulated 64-bit atomics (13) are plain off/on enums
    unsigned curCmaa = 0, curAtom = 0;
    if (gfx_get(&g_arc, 4, &en, &curCmaa)) {
      HMENU m = CreatePopupMenu();
      AppendMenuW(m, MF_STRING, 8400 + 0, curCmaa == 0 ? L"off  — driver default  ●" : L"off  — driver default");
      AppendMenuW(m, MF_STRING, 8400 + 1, curCmaa != 0 ? L"on  — morphological AA  ●" : L"on  — morphological AA");
      AppendMenuW(gfx, MF_POPUP, (UINT_PTR)m, L"CMAA (anti-aliasing)");
    }
    if (gfx_get(&g_arc, 13, &en, &curAtom)) {
      HMENU m = CreatePopupMenu();
      AppendMenuW(m, MF_STRING, 8500 + 0, curAtom == 0 ? L"off  — default  ●" : L"off  — default");
      AppendMenuW(m, MF_STRING, 8500 + 1, curAtom != 0 ? L"on  — fixes some D3D12 titles  ●" : L"on  — fixes some D3D12 titles");
      AppendMenuW(gfx, MF_POPUP, (UINT_PTR)m, L"Emulated 64-bit atomics");
    }

    if (GetMenuItemCount(gfx) > 0) {
      AppendMenuW(gfx, MF_SEPARATOR, 0, NULL);
      AppendMenuW(gfx, MF_STRING, 8600, L"Save these settings (snapshot)");
      AppendMenuW(gfx, MF_STRING, 8601, L"Restore saved settings");
      AppendMenuW(root, MF_POPUP, (UINT_PTR)gfx, L"Graphics");
    }
  }

  HMENU tw = CreatePopupMenu();
  for (int i = 0; i < tweaks_count(); i++) {
    const TweakDef *td = tweak_at(i);
    int st = tweak_state(td);
    UINT flags = MF_STRING | (st == 1 ? MF_CHECKED : 0);
    wchar_t label[192];
    swprintf(label, 192, L"%s  —  %s", td->name, td->short_desc);
    AppendMenuW(tw, flags, 7000 + i, label);
  }
  AppendMenuW(tw, MF_SEPARATOR, 0, NULL);
  AppendMenuW(tw, MF_STRING, 7100, L"Tweaks help...");
  AppendMenuW(tw, MF_STRING, 7101, L"Revert all tweaks");
  AppendMenuW(root, MF_POPUP, (UINT_PTR)tw, L"Tweaks (latency)");
  AppendMenuW(root, MF_STRING | (autostart_enabled() ? MF_CHECKED : 0), 9006, L"Start with Windows");
  if (!g_state.elevated) AppendMenuW(root, MF_STRING, 9005, L"Restart as administrator");
  HMENU about = CreatePopupMenu();
  AppendMenuW(about, MF_STRING, 9007, L"About ArcTray...");
  AppendMenuW(about, MF_SEPARATOR, 0, NULL);
  AppendMenuW(about, MF_STRING, 9101, L"GitHub — gotofuckinghell");
  AppendMenuW(about, MF_STRING, 9102, L"GitLab — grubyrams");
  AppendMenuW(about, MF_STRING, 9103, L"Telegram — /HW/ RETARDS");
  AppendMenuW(root, MF_POPUP, (UINT_PTR)about, L"About");
  AppendMenuW(root, MF_SEPARATOR, 0, NULL);
  AppendMenuW(root, MF_STRING, 9004, L"Exit");
  return root;
}

static void handle_cmd(HWND hwnd, int cmd) {
  char msg[256];
  if (cmd != 9004 && cmd != 9005) {
    if (!ensure_open()) { balloon(hwnd, L"ArcTray", L"GPU/IGCL unavailable", NIIF_WARNING); return; }
    SetTimer(hwnd, IDLE_TIMER, 20000, NULL);
  }
  if (cmd >= 1000 && cmd < 1000 + g_profc) { apply_profile(hwnd, &g_prof[cmd - 1000]); return; }
  if (cmd >= 2000 && cmd < 2000 + (int)(sizeof PL_STEPS / sizeof PL_STEPS[0])) {
    int w = PL_STEPS[cmd - 2000];
    int ok = arc_set_power(&g_arc, w, msg, sizeof msg);
    show_result(hwnd, ok, msg);
    return;
  }
  if (cmd >= 3000 && cmd < 3000 + (int)(sizeof CORE_STEPS / sizeof CORE_STEPS[0])) {
    int v = CORE_STEPS[cmd - 3000];
    int ok = arc_set_core(&g_arc, v, msg, sizeof msg);
    show_result(hwnd, ok, msg);
    return;
  }
  if (cmd >= 4000 && cmd < 4000 + (int)(sizeof TEMP_STEPS / sizeof TEMP_STEPS[0])) {
    int v = TEMP_STEPS[cmd - 4000];
    int ok = (v > 90) ? arc_set_temp_ext(&g_arc, v, msg, sizeof msg) : arc_set_temp(&g_arc, v, msg, sizeof msg);
    show_result(hwnd, ok, msg);
    return;
  }
  if (cmd == 8600 || cmd == 8601) {
    char path[MAX_PATH];
    if (!gfx_snap_path(path, sizeof path, NULL)) { show_result(hwnd, 0, "snapshot path unavailable"); return; }
    int ok = (cmd == 8600) ? gfx_save(path) : gfx_restore(path);
    show_result(hwnd, ok, cmd == 8600 ? "graphics settings saved" : "graphics settings restored");
    return;
  }
  if (cmd >= 8300 && cmd < 8304) {
    int ok = gfx_set(&g_arc, 17, 1, (unsigned)(cmd - 8300));
    char m[96]; snprintf(m, sizeof m, "frame generation -> %d (%s)", cmd - 8300, ok ? "ok" : "refused");
    show_result(hwnd, ok, m); return;
  }
  if (cmd >= 8400 && cmd < 8402) {
    int ok = gfx_set(&g_arc, 4, 1, (unsigned)(cmd - 8400));
    char m[96]; snprintf(m, sizeof m, "CMAA -> %d (%s)", cmd - 8400, ok ? "ok" : "refused");
    show_result(hwnd, ok, m); return;
  }
  if (cmd >= 8500 && cmd < 8502) {
    int ok = gfx_set(&g_arc, 13, 1, (unsigned)(cmd - 8500));
    char m[96]; snprintf(m, sizeof m, "atomics-64 -> %d (%s)", cmd - 8500, ok ? "ok" : "refused");
    show_result(hwnd, ok, m); return;
  }
  if (cmd >= 8000 && cmd < 8003) {
    int v = GFX_LL[cmd - 8000];
    int ok = gfx_set(&g_arc, 16, 1, (unsigned)v);
    char m[96]; snprintf(m, sizeof m, "low latency -> %d (%s)", v, ok ? "ok" : "refused");
    show_result(hwnd, ok, m);
    return;
  }
  if (cmd >= 8100 && cmd < 8106) {
    unsigned v = GFX_FLIP[cmd - 8100];
    int ok = gfx_set(&g_arc, 9, 1, v);
    char m[96]; snprintf(m, sizeof m, "flip mode -> 0x%x (%s)", v, ok ? "ok" : "refused");
    show_result(hwnd, ok, m);
    return;
  }
  if (cmd >= 8200 && cmd < 8208) {
    unsigned v = (unsigned)GFX_FPS[cmd - 8200];
    int ok = gfx_set(&g_arc, 2, 1, v);
    char m[96]; snprintf(m, sizeof m, "fps limit -> %u (%s)", v, ok ? "ok" : "refused");
    show_result(hwnd, ok, m);
    return;
  }
  if (cmd >= 7000 && cmd < 7000 + tweaks_count()) {
    const TweakDef *td = tweak_at(cmd - 7000);
    int want = tweak_state(td) != 1;
    wchar_t err[256] = {0};
    int ok = tweak_apply(td, want, err, 256);
    wchar_t msg[768];
    if (ok) swprintf(msg, 768, L"%s: %s%s\n%s", td->name, want ? L"ON" : L"OFF",
                     td->needs_reboot ? L" (reboot needed)" : L"", td->desc);
    else swprintf(msg, 768, L"%s: %s", td->name, err);
    balloon(hwnd, ok ? L"ArcTray" : L"ArcTray — failed", ok ? msg : err, ok ? NIIF_INFO : NIIF_WARNING);
    return;
  }
  if (cmd >= 6000 && cmd < 6000 + (int)(sizeof VOLT_STEPS / sizeof VOLT_STEPS[0])) {
    int mv = VOLT_STEPS[cmd - 6000];
    int ok = arc_set_volt(&g_arc, mv, msg, sizeof msg);
    show_result(hwnd, ok, msg);
    return;
  }
  if (cmd == 5000) { int ok = arc_set_fan(&g_arc, 0, 0, msg, sizeof msg); show_result(hwnd, ok, msg); return; }
  if (cmd > 5000 && cmd <= 5000 + (int)(sizeof FAN_STEPS / sizeof FAN_STEPS[0])) {
    int v = FAN_STEPS[cmd - 5001];
    int ok = arc_set_fan(&g_arc, 1, v, msg, sizeof msg);
    show_result(hwnd, ok, msg);
    return;
  }
  switch (cmd) {
    case 7100: { // help
      wchar_t text[4000];
      int off = swprintf(text, 4000, L"Registry tweaks (all reversible, admin only)\n\n");
      for (int i = 0; i < tweaks_count() && off < 3600; i++) {
        const TweakDef *td = tweak_at(i);
        int st = tweak_state(td);
        off += swprintf(text + off, 4000 - off, L"%s  [%s]%s\n    %s\n    %s\n\n",
                        td->name, st == 1 ? L"ON" : st == 0 ? L"OFF" : L"MIXED/absent",
                        td->needs_reboot ? L"  (needs reboot)" : L"", td->short_desc, td->desc);
      }
      MessageBoxW(hwnd, text, L"ArcTray — tweaks", MB_OK | MB_ICONINFORMATION);
      break;
    }
    case 7101: { // revert all
      wchar_t err[256] = {0};
      int ok = 1;
      for (int i = 0; i < tweaks_count(); i++)
        if (!tweak_revert(tweak_at(i), err, 256)) ok = 0;
      balloon(hwnd, ok ? L"ArcTray" : L"ArcTray — failed",
              ok ? L"All tweaks reverted to Windows defaults" : err, ok ? NIIF_INFO : NIIF_WARNING);
      break;
    }
    case 9007: {
      wchar_t text[1600];
      swprintf(text, 1600,
          L"ArcTray %s\n"
          L"Headless Intel Arc tuner: tray menu, no Electron, no window.\n"
          L"Power limit (Sysman + IGCL fallback), core offset, voltage offset,\n"
          L"temperature limit 60-115 C, fan auto/percent, logon autostart task.\n\n"
          L"Struct layouts, the 115 C runtime route and the flat-table fan\n"
          L"mechanism are ported from Arc-Power by YamsSE (GPL-2.0):\n"
          L"https://github.com/YamsSE/Arc-Power\n\n"
          L"Author:\n"
          L"GitHub  %s\n"
          L"GitLab  %s\n"
          L"Telegram  %s\n\n"
          L"Not affiliated with or endorsed by Intel.\n"
          L"Idle footprint ~8.5 MB working set.",
          APP_VERSION, URL_GITHUB, URL_GITLAB, URL_TELEGRAM);
      MessageBoxW(hwnd, text, L"About ArcTray", MB_OK | MB_ICONINFORMATION);
      break;
    }
    case 9101: ShellExecuteW(NULL, L"open", URL_GITHUB, NULL, NULL, SW_SHOWNORMAL); break;
    case 9102: ShellExecuteW(NULL, L"open", URL_GITLAB, NULL, NULL, SW_SHOWNORMAL); break;
    case 9103: ShellExecuteW(NULL, L"open", URL_TELEGRAM, NULL, NULL, SW_SHOWNORMAL); break;
    case 9006: {
      int want = !autostart_enabled();
      int ok = autostart_set(want);
      balloon(hwnd, ok ? L"ArcTray" : L"ArcTray — failed",
              ok ? (want ? L"Autostart enabled (logon task, highest rights)" : L"Autostart disabled")
                 : L"Could not change the logon task (admin rights?)",
              ok ? NIIF_INFO : NIIF_WARNING);
      break;
    }
    case 9001: {
      char quoted[MAX_PATH + 3];
      snprintf(quoted, sizeof quoted, "\"%s\"", g_ini);
      ShellExecuteA(NULL, "open", "notepad.exe", quoted, NULL, SW_SHOWNORMAL);
      break;
    }
    case 9002: load_ini(); balloon(hwnd, L"ArcTray", L"Config reloaded", NIIF_INFO); break;
    case 9003: arc_read(&g_arc, &g_state); { wchar_t s[128]; fmt_state(s, 128); balloon(hwnd, L"ArcTray", s, NIIF_INFO); } break;
    case 9004: PostQuitMessage(0); break;
    case 9005: {
      wchar_t exe[MAX_PATH];
      GetModuleFileNameW(NULL, exe, MAX_PATH);
      SHELLEXECUTEINFOW sei;
      memset(&sei, 0, sizeof sei);
      sei.cbSize = sizeof sei;
      sei.lpVerb = L"runas";
      sei.lpFile = exe;
      sei.nShow = SW_SHOWNORMAL;
      if (ShellExecuteExW(&sei)) PostQuitMessage(0);
      break;
    }
  }
}

static LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  if (g_taskbar_created && msg == g_taskbar_created) { // Explorer restarted
    logf_("TaskbarCreated: re-adding icon");
    g_tray_added = 0;
    g_tray_attempts = 0;
    if (!tray_add_once()) SetTimer(hwnd, TIMER_TRAY_RETRY, 500, NULL);
    return 0;
  }
  switch (msg) {
    case WM_TRAY:
      if (LOWORD(lp) == WM_RBUTTONUP || LOWORD(lp) == WM_CONTEXTMENU) {
        POINT pt;
        GetCursorPos(&pt);
        SetForegroundWindow(hwnd);
        HMENU m = build_menu();
        int cmd = (int)TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_LEFTALIGN, pt.x, pt.y, 0, hwnd, NULL);
        DestroyMenu(m);
        PostMessage(hwnd, WM_NULL, 0, 0);
        logf_("popup cmd=%d", cmd);
        if (cmd) handle_cmd(hwnd, cmd);
        return 0;
      }
      if (LOWORD(lp) == WM_LBUTTONUP) {
        if (!ensure_open()) { balloon(hwnd, L"ArcTray", L"GPU/IGCL unavailable", NIIF_WARNING); return 0; }
        SetTimer(hwnd, IDLE_TIMER, 20000, NULL);
        arc_read(&g_arc, &g_state);
        wchar_t s[160];
        fmt_state(s, 160);
        balloon(hwnd, L"ArcTray", s, NIIF_INFO);
      }
      return 0;
    case WM_TIMER:
      if (wp == IDLE_TIMER) { KillTimer(hwnd, IDLE_TIMER); release_hw(); }
      if (wp == TIMER_TRAY_RETRY) {
        g_tray_attempts++;
        if (tray_add_once() || g_tray_attempts > 120) {
          logf_("tray retry done: added=%d attempts=%d", g_tray_added, g_tray_attempts);
          KillTimer(hwnd, TIMER_TRAY_RETRY);
        }
      }
      return 0;
    case WM_DESTROY:
      logf_("WM_DESTROY");
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

static int run_tray(HINSTANCE inst) {
  logf_("run_tray start");
  load_ini();

  WNDCLASSW wc;
  memset(&wc, 0, sizeof wc);
  wc.lpfnWndProc = wndproc;
  wc.hInstance = inst;
  wc.lpszClassName = L"ArcTrayWnd";
  RegisterClassW(&wc);
  // A hidden TOP-LEVEL popup, not HWND_MESSAGE: broadcast messages such as
  // "TaskbarCreated" only reach top-level windows, and without them the tray
  // icon never comes back after an Explorer restart.
  HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, L"ArcTrayWnd", L"ArcTray", WS_POPUP, 0, 0, 0, 0, NULL, NULL, inst, NULL);
  if (!hwnd) { logf_("CreateWindowEx failed: %lu", GetLastError()); return 1; }
  g_hwnd = hwnd;
  logf_("window created hwnd=%p", (void *)hwnd);

  NOTIFYICONDATAW nid;
  memset(&g_nid, 0, sizeof g_nid);
  g_nid.cbSize = sizeof g_nid;
  g_nid.hWnd = hwnd;
  g_nid.uID = ID_TRAY;
  g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
  g_nid.uCallbackMessage = WM_TRAY;
  g_taskbar_created = RegisterWindowMessageW(L"TaskbarCreated");
  nid = g_nid;
  wchar_t ico[MAX_PATH];
  GetModuleFileNameW(NULL, ico, MAX_PATH);
  wchar_t *slash = wcsrchr(ico, L'\\');
  if (slash) wcscpy_s(slash + 1, MAX_PATH - (size_t)(slash + 1 - ico), L"arc.ico");
  nid.hIcon = (HICON)LoadImageW(NULL, ico, IMAGE_ICON, 32, 32, LR_LOADFROMFILE);
  if (!nid.hIcon) nid.hIcon = LoadIconW(NULL, (LPCWSTR)IDI_APPLICATION);
  wcscpy_s(g_nid.szTip, 128, L"ArcTray — Intel Arc tuner");
  g_nid = nid;
  BOOL added = tray_add_once();
  logf_("tray add=%d err=%lu icon=%p", added, added ? 0 : GetLastError(), (void *)g_nid.hIcon);
  if (!added) SetTimer(hwnd, TIMER_TRAY_RETRY, 500, NULL); // Explorer may not be ready yet

  logf_("entering loop");
  MSG m;
  while (GetMessageW(&m, NULL, 0, 0) > 0) { TranslateMessage(&m); DispatchMessageW(&m); }

  logf_("loop exit");
  Shell_NotifyIconW(NIM_DELETE, &nid);
  release_hw();
  return 0;
}

// ---- CLI mode (verification / scripting) ----
static void print_selftest(void) {
  typedef struct { ULONG a, b, c, d; WCHAR s[128]; } osvi_t;
  osvi_t vi;
  memset(&vi, 0, sizeof vi);
  vi.a = sizeof vi;
  HMODULE nt = GetModuleHandleA("ntdll.dll");
  LONG(WINAPI *rtlGetVer)(osvi_t *) = nt ? (LONG(WINAPI *)(osvi_t *))GetProcAddress(nt, "RtlGetVersion") : NULL;
  if (rtlGetVer && rtlGetVer(&vi) == 0)
    printf("os         : %lu.%lu build %lu\n", vi.b, vi.c, vi.d);
  printf("igcl dll   : %s\n", g_arc.dll_path);
  printf("driver ver : 0x%016llx\n", (unsigned long long)g_arc.props.driver_version);
  printf("ze_loader  : %s\n", g_arc.ze ? "loaded" : "missing");
  const char *need[] = {"ctlOverclockWaiverSet", "ctlOverclockPowerLimitSetV2", "ctlOverclockGpuFrequencyOffsetSetV2",
                        "ctlOverclockGpuMaxVoltageOffsetSetV2", "ctlOverclockTemperatureLimitSetV2",
                        "ctlFanSetFixedSpeedMode", "ctlFanSetSpeedTableMode", "ctlPowerTelemetryGetV2",
                        "ctlOverclockReadVFCurve"};
  for (int i = 0; i < (int)(sizeof need / sizeof need[0]); i++)
    printf("  %-38s %s\n", need[i], GetProcAddress(g_arc.igcl, need[i]) ? "yes" : "no");
  const char *zes[] = {"zesPowerGetLimits", "zesPowerSetLimits", "zesDeviceEnumFrequencyDomains",
                       "zesFrequencyOcSetVoltageTarget", "zesFrequencyOcSetMode"};
  for (int i = 0; i < (int)(sizeof zes / sizeof zes[0]); i++)
    printf("  %-38s %s\n", zes[i], g_arc.ze && GetProcAddress(g_arc.ze, zes[i]) ? "yes" : "no");
  printf("power dom  : %s\n", g_arc.pwr ? "yes" : "no");
  printf("freq dom   : %s\n", g_arc.freq ? "yes" : "no");
  printf("fan handle : %s\n", g_arc.fan ? "yes" : "no");
  printf("ext runtime: %s\n", arc_ext_available(&g_arc) ? "loaded (115 C path)" : "missing");
}

static double now_s(void) {
  LARGE_INTEGER f, c;
  QueryPerformanceFrequency(&f);
  QueryPerformanceCounter(&c);
  return (double)c.QuadPart / (double)f.QuadPart;
}

static int sample_energy(ArcDev *a, double *energy, ctl_power_telemetry_v2_t *t) {
  if (!a->ctlPowerTelemetryGetV2) return 0;
  memset(t, 0, sizeof *t);
  t->Size = sizeof *t; t->Version = 2;
  if (a->ctlPowerTelemetryGetV2(a->dev, t) != 0) return 0;
  const ctl_oc_telemetry_item_t *item = t->totalCardEnergyCounter.bSupported ? &t->totalCardEnergyCounter
                                                                           : &t->gpuEnergyCounter;
  if (!item->bSupported) return 0;
  *energy = item->value; // IGCL reports the energy counter as a DOUBLE in joules
  return 1;
}

// --watch: derive real board power from the energy counter (IGCL does not expose watts)
static void print_watch(ArcDev *a, int seconds) {
  ctl_power_telemetry_v2_t t;
  double e0 = 0, e1 = 0;
  double t0 = now_s();
  if (!sample_energy(a, &e0, &t)) { printf("no energy counter on this stack\n"); return; }
  int first = 1;
  printf("time   watts    clock   temp  pct  limit\n");
  double start = now_s();
  while (now_s() - start < seconds) {
    Sleep(500);
    if (!sample_energy(a, &e1, &t)) { printf("energy read stopped\n"); return; }
    double t1 = now_s();
    double dt = t1 - t0;
    if (dt <= 0) continue;
    double de = e1 - e0;
    if (de < 0) de = 0;
    double watts = de / dt;
    (void)first;
    printf("%5.1f  %7.1f  %5.0f   %4.0f  %3.0f  %s%s%s\n",
           t1 - start, watts,
           t.gpuCurrentClockFrequency.bSupported ? t.gpuCurrentClockFrequency.value : 0,
           t.gpuCurrentTemperature.bSupported ? t.gpuCurrentTemperature.value : 0,
           t.gpuPowerPercent.bSupported ? t.gpuPowerPercent.value : 0,
           t.gpuPowerLimited ? "POWER " : "",
           t.gpuTemperatureLimited ? "TEMP " : "",
           t.gpuCurrentLimited ? "CURRENT" : "");
    fflush(stdout);
    e0 = e1;
    t0 = t1;
  }
}

static void print_caps(void) {
  if (!g_arc.freq) { printf("no sysman frequency domain\n"); return; }
  printf("freq handle=%p\n", g_arc.freq);
  if (g_arc.zesFrequencyOcGetCapabilities) {
    zes_frequency_oc_capabilities_t c;
    memset(&c, 0, sizeof c);
    c.stype = 0x1c;
    ze_result_t r = g_arc.zesFrequencyOcGetCapabilities(g_arc.freq, &c);
    printf("caps r=0x%x oc=%u defFreq=%.1f defVolt=%.1f maxOcFreq=%.1f minOff=%.1f maxOff=%.1f maxOcVolt=%.1f\n",
           (unsigned)r, c.isOcSupported, c.maxFactoryDefaultFrequency, c.maxFactoryDefaultVoltage,
           c.maxOcFrequency, c.minOcVoltageOffset, c.maxOcVoltageOffset, c.maxOcVoltage);
  }
  if (g_arc.zesFrequencyOcGetVoltageTarget) {
    double tv = 0, ov = 0;
    ze_result_t r = g_arc.zesFrequencyOcGetVoltageTarget(g_arc.freq, &tv, &ov);
    printf("voltageTarget r=0x%x target=%.3f offset=%.3f\n", (unsigned)r, tv, ov);
  }
  if (g_arc.zesFrequencyOcGetFrequencyTarget) {
    double fv = 0;
    ze_result_t r = g_arc.zesFrequencyOcGetFrequencyTarget(g_arc.freq, &fv);
    printf("freqTarget r=0x%x target=%.1f\n", (unsigned)r, fv);
  }
}

static void print_state(void) {
  arc_read(&g_arc, &g_state);
  printf("gpu        : %s (%02x:%02x.%x)\n", g_state.name, g_state.bus, g_state.dev, g_state.fn);
  printf("elevated   : %s\n", g_state.elevated ? "yes" : "no");
  printf("sysman     : %s\n", g_state.have_sysman ? "yes" : "no");
  printf("PL         : sustained=%.0f W burst=%.0f W igcl=%.0f W  (igcl range %.0f..%.0f, def %.0f)\n",
         g_state.pl_sustained, g_state.pl_burst, g_state.pl_igcl, g_state.pl_min, g_state.pl_max, g_state.pl_def);
  printf("core offset: %+.0f MHz  (range %+.0f..%+.0f)\n", g_state.core_off, g_state.core_min, g_state.core_max);
  printf("volt offset: %.3f V  (range %.3f..%.3f)\n", g_state.volt_off, g_state.volt_min, g_state.volt_max);
  printf("temp limit : %.0f C  (range %.0f..%.0f, def %.0f)\n", g_state.temp_limit, g_state.temp_min, g_state.temp_max, g_state.temp_def);
  printf("vram       : clock %.0f MHz, effective %.0f MHz, temp %.0f C\n", g_state.vram_mhz, g_state.vram_eff_mhz, g_state.vram_temp);
  printf("live       : %.0f MHz, %.0f C, fan %d%% (mode %d, state %d %s, maxRPM %d)\n", g_state.gpu_mhz, g_state.gpu_temp,
         g_state.fan_pct, g_state.fan_mode, g_state.fan_state, g_state.fan_state_units ? "%" : "rpm", g_state.fan_max_rpm);
}

static void dump_menu(HMENU m, int depth) {
  int n = GetMenuItemCount(m);
  for (int i = 0; i < n; i++) {
    wchar_t w[(192)];
    w[0] = 0;
    GetMenuStringW(m, (UINT)i, w, 192, MF_BYPOSITION);
    char utf8[512] = {0};
    WideCharToMultiByte(CP_UTF8, 0, w, -1, utf8, sizeof utf8, NULL, NULL);
    UINT id = GetMenuItemID(m, i);
    HMENU sub = GetSubMenu(m, i);
    for (int d = 0; d < depth; d++) fputs("  ", stdout);
    printf("%s[%u]\n", utf8, id);
    if (sub) dump_menu(sub, depth + 1);
  }
}

int main(int argc, char **argv) {
  char err[256];
  int cli = argc > 1;
  if (!cli) {
    return run_tray(GetModuleHandleW(NULL));
  }
  if (!strcmp(argv[1], "--help")) {
    puts("ArcTray — headless Intel Arc tuner\n"
         "  --state                print current GPU state\n"
         "  --set pl=N             power limit (W)\n"
         "  --set core=N           core clock offset (MHz)\n"
         "  --set temp=N           temperature limit (C)\n"
         "  --set fan=auto|N       fan mode / fixed percent\n"
         "  --sysman               EXPERIMENTAL: write the voltage offset through the\n"
         "                         Sysman V/F pair (allows >288 mV and negative values)\n"
         "  (no args)              run tray icon");
    return 0;
  }
  if (!arc_open(&g_arc, err, sizeof err)) { fprintf(stderr, "open failed: %s\n", err); return 2; }
  if (!strcmp(argv[1], "--state")) { print_state(); arc_close(&g_arc); return 0; }
  if (!strcmp(argv[1], "--caps")) { print_caps(); arc_close(&g_arc); return 0; }
  if (!strcmp(argv[1], "--selftest")) { print_selftest(); arc_close(&g_arc); return 0; }
  if (!strncmp(argv[1], "--watch", 7)) {
    int secs = 10;
    if (argv[1][7] == '=') secs = atoi(argv[1] + 8);
    print_watch(&g_arc, secs);
    arc_close(&g_arc);
    return 0;
  }
  if (!strncmp(argv[1], "--gfx-watch", 11)) {
    int iv = (argv[1][11] == '=') ? atoi(argv[1] + 12) : 30;
    if (iv < 5) iv = 5;
    int rc = gfx_watch_loop(iv);
    arc_close(&g_arc);
    return rc;
  }
  if (!strncmp(argv[1], "--gfx-save", 10)) {
    char path[MAX_PATH];
    if (!gfx_snap_path(path, sizeof path, argv[1][10] == '=' ? argv[1] + 11 : NULL)) return 2;
    int ok = gfx_save(path);
    arc_close(&g_arc);
    return ok ? 0 : 1;
  }
  if (!strncmp(argv[1], "--gfx-restore", 13)) {
    char path[MAX_PATH];
    if (!gfx_snap_path(path, sizeof path, argv[1][13] == '=' ? argv[1] + 14 : NULL)) return 2;
    int ok = gfx_restore(path);
    arc_close(&g_arc);
    return ok ? 0 : 1;
  }
  if (!strcmp(argv[1], "--gfx")) {
    GfxFeature f[64];
    int n = gfx_caps(&g_arc, f, 64);
    printf("features: %d\n", n);
    for (int i = 0; i < n; i++) {
      int en = -1; unsigned v = 0;
      int ok = gfx_get(&g_arc, f[i].id, &en, &v);
      printf("  %-18s id=%2d type=%d perApp=%d  %s enable=%d value=%u\n",
             f[i].name, f[i].id, f[i].value_type, f[i].per_app,
             ok ? "ok  " : "n/a ", en, v);
    }
    arc_close(&g_arc);
    return 0;
  }
  if (!strncmp(argv[1], "--gfx-set=", 10)) {
    const char *spec = argv[1] + 10;          // <name>:<value>
    const char *colon = strchr(spec, ':');
    if (!colon) { fprintf(stderr, "usage: --gfx-set=<name>:<value>\n"); arc_close(&g_arc); return 2; }
    char name[64] = {0};
    size_t n = (size_t)(colon - spec); if (n >= sizeof name) n = sizeof name - 1;
    memcpy(name, spec, n);
    int id = gfx_id_by_name(name);
    if (id < 0) { fprintf(stderr, "unknown feature: %s\n", name); arc_close(&g_arc); return 2; }
    unsigned val = (unsigned)atoi(colon + 1);
    int ok = gfx_set(&g_arc, id, 1, val);
    int en = -1; unsigned got = 0;
    int rd = gfx_get(&g_arc, id, &en, &got);
    printf("%s set %u: %s; read back %s value=%u (enable=%d)\n",
           name, val, ok ? "ok" : "refused", rd ? "ok" : "n/a", got, en);
    arc_close(&g_arc);
    return (ok && rd && got == val) ? 0 : 1;
  }
  if (!strcmp(argv[1], "--tweaks")) {
    for (int i = 0; i < tweaks_count(); i++) {
      const TweakDef *td = tweak_at(i);
      int st = tweak_state(td);
      char id[64] = {0}, name[160] = {0};
      WideCharToMultiByte(CP_UTF8, 0, td->id, -1, id, sizeof id, NULL, NULL);
      WideCharToMultiByte(CP_UTF8, 0, td->name, -1, name, sizeof name, NULL, NULL);
      printf("%-22s %-4s %s%s\n", id, st == 1 ? "ON" : st == 0 ? "OFF" : "MIX", name,
             td->needs_reboot ? "  (reboot)" : "");
      if (getenv("ARCTRAY_TWEAK_DEBUG")) tweak_debug(td);
    }
    arc_close(&g_arc);
    return 0;
  }
  if (!strncmp(argv[1], "--tweak=", 8)) {
    const char *spec = argv[1] + 8;          // <id>:on|off|revert
    char id[64] = {0};
    const char *colon = strchr(spec, ':');
    if (!colon) { fprintf(stderr, "usage: --tweak=<id>:on|off|revert\n"); arc_close(&g_arc); return 2; }
    size_t n = (size_t)(colon - spec); if (n >= sizeof id) n = sizeof id - 1;
    memcpy(id, spec, n);
    const char *act = colon + 1;
    for (int i = 0; i < tweaks_count(); i++) {
      const TweakDef *td = tweak_at(i);
      char tid[64];
      WideCharToMultiByte(CP_UTF8, 0, td->id, -1, tid, sizeof tid, NULL, NULL);
      if (_stricmp(tid, id) != 0) continue;
      wchar_t err[256] = {0};
      int ok;
      if (!strcmp(act, "revert")) ok = tweak_revert(td, err, 256);
      else ok = tweak_apply(td, !strcmp(act, "on"), err, 256);
      printf("%s %s: %s\n", id, act, ok ? "ok" : "failed");
      if (!ok) printf("  %ls\n", err);
      arc_close(&g_arc);
      return ok ? 0 : 1;
    }
    fprintf(stderr, "unknown tweak id: %s\n", id);
    arc_close(&g_arc);
    return 2;
  }
  if (!strcmp(argv[1], "--about")) {
    printf("ArcTray %ls\n", APP_VERSION);
    printf("Headless Intel Arc tuner: tray menu, no Electron, no window.\n");
    printf("Power limit, core offset, voltage offset, temperature limit 60-115 C, fan, autostart task.\n");
    printf("Struct layouts, 115 C runtime route and flat-table fan ported from Arc-Power by YamsSE (GPL-2.0):\n");
    printf("  https://github.com/YamsSE/Arc-Power\n");
    printf("GitHub   %ls\nGitLab   %ls\nTelegram %ls\n", URL_GITHUB, URL_GITLAB, URL_TELEGRAM);
    printf("Not affiliated with Intel.\n");
    arc_close(&g_arc);
    return 0;
  }
  if (!strncmp(argv[1], "--autostart", 11)) {
    const char *v = argv[1] + 11;
    if (*v == '=') v++;
    if (!strcmp(v, "status")) { printf("%s\n", autostart_enabled() ? "on" : "off"); arc_close(&g_arc); return 0; }
    if (!strcmp(v, "on") || !strcmp(v, "off")) {
      int ok = autostart_set(!strcmp(v, "on"));
      printf("autostart %s: %s\n", v, ok ? "ok" : "failed");
      arc_close(&g_arc);
      return ok ? 0 : 1;
    }
    fprintf(stderr, "usage: --autostart=on|off|status\n");
    arc_close(&g_arc);
    return 2;
  }
  if (!strcmp(argv[1], "--menudump")) {
    load_ini();
    g_open = 1;
    HMENU m = build_menu();
    dump_menu(m, 0);
    DestroyMenu(m);
    ExitProcess(0); // skip teardown: the menus are gone, the process is done
  }
  if (!strncmp(argv[1], "--profile=", 10)) {
    load_ini();
    const char *want = argv[1] + 10;
    for (int i = 0; i < g_profc; i++) {
      if (_stricmp(g_prof[i].name, want) == 0) {
        char msg[256];
        int ok = 1;
        if (g_prof[i].pl > 0) { ok &= arc_set_power(&g_arc, g_prof[i].pl, msg, sizeof msg); printf("pl: %s\n", msg); }
        ok &= arc_set_core(&g_arc, g_prof[i].core, msg, sizeof msg); printf("core: %s\n", msg);
        if (g_prof[i].temp > 0) { ok &= arc_set_temp(&g_arc, g_prof[i].temp, msg, sizeof msg); printf("temp: %s\n", msg); }
        if (g_prof[i].fan != 0) {
          ok &= arc_set_fan(&g_arc, g_prof[i].fan < 0 ? 0 : 1, g_prof[i].fan < 0 ? 0 : g_prof[i].fan, msg, sizeof msg);
          printf("fan: %s\n", msg);
        }
        arc_close(&g_arc);
        return ok ? 0 : 1;
      }
    }
    fprintf(stderr, "profile '%s' not found in %s\n", want, g_ini);
    arc_close(&g_arc);
    return 2;
  }
  if (!strcmp(argv[1], "--set")) {
    int ok = 1;
    for (int i = 2; i < argc; i++) if (!strcmp(argv[i], "--sysman")) _putenv_s("ARCTRAY_VOLT_SYSMAN", "1");
    for (int i = 2; i < argc; i++) {
      char msg[256] = {0};
      if (!strcmp(argv[i], "--sysman")) continue;
      char *eq = strchr(argv[i], '=');
      if (!eq) { fprintf(stderr, "bad arg %s\n", argv[i]); ok = 0; continue; }
      *eq = 0;
      const char *k = argv[i], *v = eq + 1;
      if (!strcmp(k, "pl")) ok &= arc_set_power(&g_arc, atof(v), msg, sizeof msg);
      else if (!strcmp(k, "core")) ok &= arc_set_core(&g_arc, atof(v), msg, sizeof msg);
      else if (!strcmp(k, "temp")) {
        double c = atof(v);
        ok &= (c > 90) ? arc_set_temp_ext(&g_arc, c, msg, sizeof msg) : arc_set_temp(&g_arc, c, msg, sizeof msg);
      }
      else if (!strcmp(k, "volt")) ok &= arc_set_volt(&g_arc, atoi(v), msg, sizeof msg);
      else if (!strcmp(k, "fan")) {
        if (!_stricmp(v, "auto")) ok &= arc_set_fan(&g_arc, 0, 0, msg, sizeof msg);
        else ok &= arc_set_fan(&g_arc, 1, atoi(v), msg, sizeof msg);
      } else { fprintf(stderr, "unknown key %s\n", k); ok = 0; continue; }
      printf("%s=%s: %s\n", k, v, msg);
    }
    arc_read(&g_arc, &g_state);
    print_state();
    arc_close(&g_arc);
    return ok ? 0 : 1;
  }
  fprintf(stderr, "unknown command (see --help)\n");
  arc_close(&g_arc);
  return 2;
}
