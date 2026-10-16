/* CornerBtn v2 - маленькая полупрозрачная кнопка поверх всех окон (Windows 10)
 * Своя кнопка на каждом мониторе, настройки в CornerBtn.ini рядом с программой.
 */
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WINVER 0x0601
#define _WIN32_WINNT 0x0601
#define _WIN32_IE 0x0600
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <servprov.h>
#include <oleauto.h>
#include <dwmapi.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>
#include <stdarg.h>

#define APPNAME L"CornerBtn"
#define APPVER L"2.3.1"
#define IDI_APP 100
#define MAXE 400
#define MAXD 7
#define MAXMON 8
#define MAXLIST 64
#define WSZ(a) (sizeof(a) / sizeof((a)[0]))

/* ---------- данные ---------- */
typedef struct { wchar_t action[520]; wchar_t args[260]; } Act;
typedef struct { int depth, type; wchar_t name[128], action[520], args[260]; } Entry; /* type: 0 пункт, 1 папка, 2 разделитель */

typedef struct MonBtn {
    HWND hwnd;
    HMONITOR mon;
    wchar_t dev[40], key[48];
    RECT rc;
    int primary, enabled, corner, dx, dy; /* corner: 0 ЛВ, 1 ПВ, 2 ЛН, 3 ПН; dx,dy - отступ от угла внутрь */
    int hidden, alive, drag, hover, skipUp;
    POINT dp, wo;
    HWND saved[128];
    unsigned char savedMax[128];
    int nsaved;
} MonBtn;

static HINSTANCE g_hinst;
static HWND g_ctl;          /* скрытое управляющее окно */
static HFONT g_font;
static int g_dpi = 96;
static wchar_t g_ini[MAX_PATH], g_dir[MAX_PATH];
static COLORREF g_cust[16];

static MonBtn *g_mb[MAXMON];
static int g_nmb;

static Entry g_e[MAXE];
static int g_n;
static Act g_click, g_dbl, g_mid;
static struct { int size, opacity, hover, shape, localKeys, pinTop, pinAll, keepTop, raisePinned; COLORREF color; wchar_t icon[MAX_PATH]; } g_cfg;
static wchar_t g_excl[MAXLIST][64], g_remote[MAXLIST][64];
static int g_nexcl, g_nremote;
static HICON g_icon;
static int g_inMenu, g_modal, g_debug;
static wchar_t g_log[MAX_PATH];
static HWND g_prevFg, g_lastFg;
static int g_lastFgEx;

#define S(x) MulDiv((x), g_dpi, 96)

static const struct { const wchar_t *id, *label; } BI[] = {
    { L"@mondesk",  L"Свернуть / вернуть окна на ЭТОМ мониторе" },
    { L"@desktop",  L"Свернуть / вернуть окна на ВСЕХ мониторах" },
    { L"@minall",   L"Свернуть все окна (без возврата)" },
    { L"@taskview", L"Обзор задач (Win+Tab)" },
    { L"@pin",      L"Закрепить / открепить активное окно" },
    { L"@taskmgr",  L"Диспетчер задач" },
    { L"@lock",     L"Заблокировать компьютер" },
    { L"@none",     L"Нет действия" },
};
#define NBI ((int)WSZ(BI))

/* ---------- утилиты ---------- */
static void cp(wchar_t *d, size_t n, const wchar_t *s) { wcsncpy(d, s, n - 1); d[n - 1] = 0; }
static void Trim(wchar_t *s) {
    size_t n = wcslen(s);
    while (n && iswspace(s[n - 1])) s[--n] = 0;
    wchar_t *p = s;
    while (*p && iswspace(*p)) p++;
    if (p != s) memmove(s, p, (wcslen(p) + 1) * sizeof(wchar_t));
}
static COLORREF ParseColor(const wchar_t *s, COLORREF def) {
    while (*s == L' ' || *s == L'#') s++;
    wchar_t *e;
    unsigned long v = wcstoul(s, &e, 16);
    if (e == s) return def;
    return RGB((v >> 16) & 255, (v >> 8) & 255, v & 255);
}
static int Clamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

/* журнал диагностики (UTF-16, CornerBtn.log рядом с программой) */
static void Log(const wchar_t *fmt, ...) {
    if (!g_debug || !g_log[0]) return;
    wchar_t msg[1000], line[1100];
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf(msg, 999, fmt, ap);
    va_end(ap);
    msg[999] = 0;
    SYSTEMTIME st;
    GetLocalTime(&st);
    _snwprintf(line, 1100, L"%02d:%02d:%02d.%03d  %ls\r\n", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, msg);
    line[1099] = 0;
    HANDLE h = CreateFileW(g_log, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD w;
    if (GetLastError() != ERROR_ALREADY_EXISTS) { WORD bom = 0xFEFF; WriteFile(h, &bom, 2, &w, NULL); }
    WriteFile(h, line, (DWORD)(wcslen(line) * sizeof(wchar_t)), &w, NULL);
    CloseHandle(h);
}
static void WinBuild(wchar_t *out, int n) {
    HKEY k;
    wchar_t b[64] = L"?", v[64] = L"";
    DWORD sz, ubr = 0;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", 0, KEY_READ | KEY_WOW64_64KEY, &k) == ERROR_SUCCESS) {
        sz = sizeof b; RegQueryValueExW(k, L"CurrentBuildNumber", NULL, NULL, (BYTE *)b, &sz);
        sz = sizeof v; if (RegQueryValueExW(k, L"DisplayVersion", NULL, NULL, (BYTE *)v, &sz) != ERROR_SUCCESS) { sz = sizeof v; RegQueryValueExW(k, L"ReleaseId", NULL, NULL, (BYTE *)v, &sz); }
        sz = sizeof ubr; RegQueryValueExW(k, L"UBR", NULL, NULL, (BYTE *)&ubr, &sz);
        RegCloseKey(k);
    }
    _snwprintf(out, (size_t)n, L"Windows: сборка %ls.%lu %ls", b, (unsigned long)ubr, v);
    out[n - 1] = 0;
}

/* ---------- ini ---------- */
static int EnsureIni(void) { /* 1 если файл только что создан */
    if (GetFileAttributesW(g_ini) != INVALID_FILE_ATTRIBUTES) return 0;
    HANDLE h = CreateFileW(g_ini, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        BYTE bom[2] = { 0xFF, 0xFE }; /* UTF-16 LE BOM: Win32 тогда хранит кириллицу правильно */
        DWORD w;
        WriteFile(h, bom, 2, &w, NULL);
        CloseHandle(h);
    }
    return 1;
}
static void GetS(const wchar_t *sec, const wchar_t *key, const wchar_t *def, wchar_t *out, DWORD n) {
    GetPrivateProfileStringW(sec, key, def, out, n, g_ini);
}
static int GetI(const wchar_t *sec, const wchar_t *key, int def) {
    wchar_t b[32];
    GetS(sec, key, L"", b, 32);
    if (!b[0]) return def;
    return (int)wcstol(b, NULL, 10);
}
static void PutS(const wchar_t *sec, const wchar_t *key, const wchar_t *v) { WritePrivateProfileStringW(sec, key, v, g_ini); }
static void PutI(const wchar_t *sec, const wchar_t *key, int v) {
    wchar_t b[32];
    _snwprintf(b, 32, L"%d", v);
    b[31] = 0;
    PutS(sec, key, b);
}

static void AddE(int d, int t, const wchar_t *n, const wchar_t *a, const wchar_t *ar) {
    if (g_n >= MAXE) return;
    Entry *e = &g_e[g_n++];
    memset(e, 0, sizeof *e);
    e->depth = d;
    e->type = t;
    cp(e->name, WSZ(e->name), n);
    cp(e->action, WSZ(e->action), a);
    cp(e->args, WSZ(e->args), ar);
}
static void Normalize(void) {
    for (int i = 0; i < g_n; i++) {
        int mx = (i == 0) ? 0 : (g_e[i - 1].type == 1 ? g_e[i - 1].depth + 1 : g_e[i - 1].depth);
        if (mx > MAXD) mx = MAXD;
        if (g_e[i].depth < 0) g_e[i].depth = 0;
        if (g_e[i].depth > mx) g_e[i].depth = mx;
    }
}
static wchar_t *Fld(wchar_t **pp) {
    wchar_t *s = *pp;
    if (!s) return (wchar_t *)L"";
    wchar_t *q = wcschr(s, L'|');
    if (q) { *q = 0; *pp = q + 1; } else *pp = NULL;
    return s;
}

static void LoadList(const wchar_t *sec, wchar_t (*arr)[64], int *n, const wchar_t **defs, int ndefs) {
    *n = 0;
    int cnt = GetI(sec, L"Count", -1);
    if (cnt < 0) {
        for (int i = 0; i < ndefs && *n < MAXLIST; i++) cp(arr[(*n)++], 64, defs[i]);
        return;
    }
    for (int i = 1; i <= cnt && *n < MAXLIST; i++) {
        wchar_t key[16], v[64];
        _snwprintf(key, 16, L"E%d", i);
        key[15] = 0;
        GetS(sec, key, L"", v, 64);
        Trim(v);
        if (v[0]) cp(arr[(*n)++], 64, v);
    }
}
static void SaveList(const wchar_t *sec, wchar_t (*arr)[64], int n) {
    WritePrivateProfileStringW(sec, NULL, NULL, g_ini);
    PutI(sec, L"Count", n);
    for (int i = 0; i < n; i++) {
        wchar_t key[16];
        _snwprintf(key, 16, L"E%d", i + 1);
        key[15] = 0;
        PutS(sec, key, arr[i]);
    }
}

static void SaveActs(void) {
    PutS(L"Click", L"Action", g_click.action);
    PutS(L"Click", L"Args", g_click.args);
    PutS(L"DoubleClick", L"Action", g_dbl.action);
    PutS(L"DoubleClick", L"Args", g_dbl.args);
    PutS(L"MiddleClick", L"Action", g_mid.action);
    PutS(L"MiddleClick", L"Args", g_mid.args);
}

static void LoadAll(void) {
    static const wchar_t *remDefs[] = { L"mstsc.exe", L"msrdc.exe", L"vncviewer.exe", L"tvnviewer.exe", L"vmconnect.exe" };
    wchar_t b[64];
    g_cfg.size = Clamp(GetI(L"Button", L"Size", 8), 2, 200);
    g_cfg.opacity = Clamp(GetI(L"Button", L"Opacity", 35), 5, 100);
    g_cfg.hover = Clamp(GetI(L"Button", L"HoverOpacity", 90), 5, 100);
    g_cfg.shape = GetI(L"Button", L"Shape", 0) ? 1 : 0;
    g_cfg.localKeys = GetI(L"Options", L"LocalKeys", 1) ? 1 : 0;
    g_cfg.pinTop = GetI(L"Options", L"PinTopmost", 1) ? 1 : 0;
    g_cfg.pinAll = GetI(L"Options", L"PinAllDesktops", 1) ? 1 : 0;
    g_cfg.keepTop = GetI(L"Options", L"KeepTopmost", 0) ? 1 : 0;
    g_cfg.raisePinned = GetI(L"Options", L"RaisePinned", 1) ? 1 : 0;
    g_debug = GetI(L"Options", L"Debug", 0) ? 1 : 0;
    GetS(L"Button", L"Color", L"3399FF", b, 64);
    g_cfg.color = ParseColor(b, RGB(0x33, 0x99, 0xFF));
    GetS(L"Button", L"Icon", L"", g_cfg.icon, MAX_PATH);

    GetS(L"Click", L"Action", L"@mondesk", g_click.action, 520);
    GetS(L"Click", L"Args", L"", g_click.args, 260);
    GetS(L"DoubleClick", L"Action", L"@taskview", g_dbl.action, 520);
    GetS(L"DoubleClick", L"Args", L"", g_dbl.args, 260);
    GetS(L"MiddleClick", L"Action", L"@none", g_mid.action, 520);
    GetS(L"MiddleClick", L"Args", L"", g_mid.args, 260);

    if (GetI(L"Button", L"Version", 1) < 2) { /* миграция с v1: "@desktop" теперь значит "все мониторы" */
        if (!wcscmp(g_click.action, L"@desktop")) cp(g_click.action, WSZ(g_click.action), L"@mondesk");
        PutI(L"Button", L"Version", 2);
        SaveActs();
    }

    LoadList(L"Exclude", g_excl, &g_nexcl, NULL, 0);
    LoadList(L"Remote", g_remote, &g_nremote, remDefs, (int)WSZ(remDefs));
    g_lastFg = NULL;

    g_n = 0;
    int cnt = GetI(L"Menu", L"Count", -1);
    if (cnt < 0) {
        AddE(0, 1, L"Программы", L"", L"");
        AddE(1, 0, L"Блокнот", L"notepad.exe", L"");
        AddE(1, 0, L"Калькулятор", L"calc.exe", L"");
        AddE(0, 0, L"Диспетчер задач", L"@taskmgr", L"");
        AddE(0, 2, L"", L"", L"");
        AddE(0, 0, L"Заблокировать компьютер", L"@lock", L"");
    } else {
        for (int i = 1; i <= cnt && g_n < MAXE; i++) {
            wchar_t key[16], line[1500];
            _snwprintf(key, 16, L"I%d", i);
            key[15] = 0;
            GetS(L"Menu", key, L"", line, 1500);
            if (!line[0]) continue;
            wchar_t *p = line;
            wchar_t *d = Fld(&p), *t = Fld(&p), *n = Fld(&p), *a = Fld(&p);
            AddE(_wtoi(d), _wtoi(t), n, a, p ? p : L"");
            if (g_e[g_n - 1].type < 0 || g_e[g_n - 1].type > 2) g_e[g_n - 1].type = 0;
        }
    }
    Normalize();
}

static void SaveCfg(void) {
    wchar_t b[32];
    PutI(L"Button", L"Version", 2);
    PutI(L"Button", L"Size", g_cfg.size);
    PutI(L"Button", L"Opacity", g_cfg.opacity);
    PutI(L"Button", L"HoverOpacity", g_cfg.hover);
    PutI(L"Button", L"Shape", g_cfg.shape);
    _snwprintf(b, 32, L"%02X%02X%02X", GetRValue(g_cfg.color), GetGValue(g_cfg.color), GetBValue(g_cfg.color));
    b[31] = 0;
    PutS(L"Button", L"Color", b);
    PutS(L"Button", L"Icon", g_cfg.icon);
    PutI(L"Options", L"LocalKeys", g_cfg.localKeys);
    PutI(L"Options", L"PinTopmost", g_cfg.pinTop);
    PutI(L"Options", L"PinAllDesktops", g_cfg.pinAll);
    PutI(L"Options", L"KeepTopmost", g_cfg.keepTop);
    PutI(L"Options", L"RaisePinned", g_cfg.raisePinned);
    PutI(L"Options", L"Debug", g_debug);
}
static void SaveMenu(void) {
    WritePrivateProfileStringW(L"Menu", NULL, NULL, g_ini); /* удалить секцию */
    PutI(L"Menu", L"Count", g_n);
    for (int i = 0; i < g_n; i++) {
        wchar_t key[16], line[1500], nm[128], ac[520];
        cp(nm, WSZ(nm), g_e[i].name);
        cp(ac, WSZ(ac), g_e[i].action);
        for (wchar_t *c = nm; *c; c++) if (*c == L'|') *c = L'/';
        for (wchar_t *c = ac; *c; c++) if (*c == L'|') *c = L'/';
        _snwprintf(key, 16, L"I%d", i + 1);
        key[15] = 0;
        _snwprintf(line, 1500, L"%d|%d|%ls|%ls|%ls", g_e[i].depth, g_e[i].type, nm, ac, g_e[i].args);
        line[1499] = 0;
        PutS(L"Menu", key, line);
    }
}

/* ---------- настройки мониторов ---------- */
static void MonKey(const wchar_t *dev, wchar_t *out) {
    const wchar_t *p = dev;
    while (*p == L'\\' || *p == L'.') p++;
    _snwprintf(out, 48, L"Mon.%ls", p);
    out[47] = 0;
}
static void MonLoad(MonBtn *b) {
    b->enabled = GetI(b->key, L"Enabled", 1) ? 1 : 0;
    b->corner = GetI(b->key, L"Corner", b->primary ? 0 : 1) & 3;
    b->dx = Clamp(GetI(b->key, L"DX", 0), 0, 5000);
    b->dy = Clamp(GetI(b->key, L"DY", 0), 0, 5000);
}
static void MonSave(MonBtn *b) {
    PutI(b->key, L"Enabled", b->enabled);
    PutI(b->key, L"Corner", b->corner);
    PutI(b->key, L"DX", b->dx);
    PutI(b->key, L"DY", b->dy);
}
static void SaveAll(void) {
    SaveCfg();
    SaveActs();
    SaveMenu();
    SaveList(L"Exclude", g_excl, g_nexcl);
    SaveList(L"Remote", g_remote, g_nremote);
    for (int i = 0; i < g_nmb; i++) MonSave(g_mb[i]);
}

/* ---------- автозапуск ---------- */
#define RUNKEY L"Software\\Microsoft\\Windows\\CurrentVersion\\Run"
static int AutoGet(void) {
    HKEY k;
    int r = 0;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, RUNKEY, 0, KEY_READ, &k) == ERROR_SUCCESS) {
        r = RegQueryValueExW(k, APPNAME, NULL, NULL, NULL, NULL) == ERROR_SUCCESS;
        RegCloseKey(k);
    }
    return r;
}
static void AutoSet(int on) {
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, RUNKEY, 0, KEY_WRITE, &k) != ERROR_SUCCESS) return;
    if (on) {
        wchar_t exe[MAX_PATH], v[MAX_PATH + 4];
        GetModuleFileNameW(NULL, exe, MAX_PATH);
        _snwprintf(v, MAX_PATH + 4, L"\"%ls\"", exe);
        v[MAX_PATH + 3] = 0;
        RegSetValueExW(k, APPNAME, 0, REG_SZ, (BYTE *)v, (DWORD)((wcslen(v) + 1) * sizeof(wchar_t)));
    } else RegDeleteValueW(k, APPNAME);
    RegCloseKey(k);
}

/* ---------- окна, процессы ---------- */
static int IsOurs(HWND w) {
    DWORD pid = 0;
    if (!w) return 0;
    GetWindowThreadProcessId(w, &pid);
    return pid == GetCurrentProcessId();
}
static void ExeOf(HWND w, wchar_t *out, int n) {
    out[0] = 0;
    DWORD pid = 0;
    GetWindowThreadProcessId(w, &pid);
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (h) {
        wchar_t p[MAX_PATH];
        DWORD sz = MAX_PATH;
        if (QueryFullProcessImageNameW(h, 0, p, &sz)) {
            wchar_t *s = wcsrchr(p, L'\\');
            cp(out, (size_t)n, s ? s + 1 : p);
        }
        CloseHandle(h);
    }
}
static int InList(wchar_t (*arr)[64], int n, const wchar_t *exe) {
    if (!exe[0]) return 0;
    for (int i = 0; i < n; i++) if (!_wcsicmp(arr[i], exe)) return 1;
    return 0;
}
static int BadClass(HWND w) {
    wchar_t c[64];
    GetClassNameW(w, c, 64);
    return !wcscmp(c, L"Shell_TrayWnd") || !wcscmp(c, L"Shell_SecondaryTrayWnd") || !wcscmp(c, L"Progman") || !wcscmp(c, L"WorkerW");
}
static void DescWin(HWND w, wchar_t *out, int n) {
    wchar_t cls[64] = L"", title[48] = L"", exe[MAX_PATH] = L"";
    GetClassNameW(w, cls, 64);
    GetWindowTextW(w, title, 48);
    ExeOf(w, exe, MAX_PATH);
    DWORD cl = 0;
    DwmGetWindowAttribute(w, 13 /* DWMWA_CLOAKED */, &cl, sizeof cl);
    _snwprintf(out, (size_t)n, L"hwnd=%p exe=%ls class=%ls cloaked=%lu title=\"%ls\"", (void *)w, exe, cls, (unsigned long)cl, title);
    out[n - 1] = 0;
}

/* IVirtualDesktopManager (документированный): принадлежит ли окно текущему виртуальному рабочему столу */
static const GUID G_ClsidVDM = { 0xAA509086, 0x5CA9, 0x4C25, { 0x8F, 0x95, 0x58, 0x9D, 0x3C, 0x07, 0xB4, 0x8A } };
static const GUID G_IidVDM   = { 0xA5CD92FF, 0x29BE, 0x454C, { 0x8D, 0x04, 0xD8, 0x28, 0x79, 0xFB, 0x3F, 0x1B } };
typedef struct {
    HRESULT (STDMETHODCALLTYPE *QI)(void *, REFIID, void **);
    ULONG (STDMETHODCALLTYPE *AddRef)(void *);
    ULONG (STDMETHODCALLTYPE *Release)(void *);
    HRESULT (STDMETHODCALLTYPE *IsWindowOnCurrentVirtualDesktop)(void *, HWND, BOOL *);
    HRESULT (STDMETHODCALLTYPE *GetWindowDesktopId)(void *, HWND, GUID *);
    void *MoveWindowToDesktop;
} VDMVtbl;
typedef struct { VDMVtbl *v; } VDM;
static VDM *g_vdm;
static HRESULT g_vdmHr;
static HWND g_pinned[32];
static int g_npinned, g_haveDesk;
static GUID g_lastDesk;
static int OnCurDesk(HWND w) {
    BOOL on = TRUE;
    if (!g_vdm) return 1;
    if (FAILED(g_vdm->v->IsWindowOnCurrentVirtualDesktop(g_vdm, w, &on))) return 1;
    return on ? 1 : 0;
}

/* закреплённые нами окна: поднимаем наверх после смены виртуального стола */
static void PinnedAdd(HWND w) {
    for (int i = 0; i < g_npinned; i++) if (g_pinned[i] == w) return;
    if (g_npinned < 32) g_pinned[g_npinned++] = w;
}
static void PinnedRemove(HWND w) {
    for (int i = 0; i < g_npinned; i++)
        if (g_pinned[i] == w) { g_pinned[i] = g_pinned[--g_npinned]; return; }
}
/* сколько видимых обычных окон лежит выше w и пересекается с ним (по z-порядку) */
static int CountAbove(HWND w) {
    RECT r;
    if (!GetWindowRect(w, &r)) return -1;
    int n = 0;
    for (HWND h = GetWindow(w, GW_HWNDPREV); h; h = GetWindow(h, GW_HWNDPREV)) {
        if (!IsWindowVisible(h) || IsIconic(h)) continue;
        LONG ex = GetWindowLongW(h, GWL_EXSTYLE);
        if (ex & (WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE)) continue;
        if (IsOurs(h) || BadClass(h)) continue;
        DWORD cl = 0;
        DwmGetWindowAttribute(h, 13 /* DWMWA_CLOAKED */, &cl, sizeof cl);
        if (cl) continue;
        RECT hr, x;
        if (!GetWindowRect(h, &hr)) continue;
        if (IntersectRect(&x, &r, &hr)) n++;
    }
    return n;
}
/* поднять окно без активации; результат проверяется по z-порядку */
static void RaiseWin(HWND w) {
    const UINT f = SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE;
    if (GetWindowLongW(w, GWL_EXSTYLE) & WS_EX_TOPMOST) return; /* уже "поверх всех" */
    int before = CountAbove(w);
    if (before <= 0) return;
    BOOL ok1 = SetWindowPos(w, HWND_TOP, 0, 0, 0, 0, f);
    DWORD e1 = ok1 ? 0 : GetLastError();
    int a1 = CountAbove(w);
    if (a1 > 0) { /* HWND_TOP не помог: кратко делаем "поверх всех" и сразу возвращаем */
        BOOL ok2 = SetWindowPos(w, HWND_TOPMOST, 0, 0, 0, 0, f);
        DWORD e2 = ok2 ? 0 : GetLastError();
        BOOL ok3 = SetWindowPos(w, HWND_NOTOPMOST, 0, 0, 0, 0, f);
        DWORD e3 = ok3 ? 0 : GetLastError();
        int a2 = CountAbove(w);
        Log(L"  raise hwnd=%p: выше было %d; HWND_TOP ok=%d err=%lu -> выше %d; TOPMOST ok=%d err=%lu, NOTOPMOST ok=%d err=%lu -> выше %d",
            (void *)w, before, (int)ok1, (unsigned long)e1, a1, (int)ok2, (unsigned long)e2, (int)ok3, (unsigned long)e3, a2);
    } else Log(L"  raise hwnd=%p: выше было %d; HWND_TOP ok=%d -> выше %d", (void *)w, before, (int)ok1, a1);
}
static void RaisePinned(void) {
    for (int i = g_npinned - 1; i >= 0; i--) {
        HWND w = g_pinned[i];
        if (!IsWindow(w)) { g_pinned[i] = g_pinned[--g_npinned]; continue; }
        if (IsIconic(w)) continue;
        RaiseWin(w);
    }
}
static int g_raiseTicks, g_haveReg;
static GUID g_regDesk;
/* номер текущего виртуального стола из реестра (обновляется проводником при переключении) */
static int ReadCurDesk(GUID *g) {
    DWORD sid = 0;
    wchar_t path[260];
    ProcessIdToSessionId(GetCurrentProcessId(), &sid);
    for (int pass = 0; pass < 2; pass++) {
        if (pass == 0) _snwprintf(path, 260, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\SessionInfo\\%lu\\VirtualDesktops", (unsigned long)sid);
        else cp(path, 260, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\VirtualDesktops");
        path[259] = 0;
        HKEY k;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, path, 0, KEY_READ, &k) == ERROR_SUCCESS) {
            BYTE b[16];
            DWORD sz = 16;
            LONG r = RegQueryValueExW(k, L"CurrentVirtualDesktop", NULL, NULL, b, &sz);
            RegCloseKey(k);
            if (r == ERROR_SUCCESS && sz == 16) { memcpy(g, b, 16); return pass + 1; }
        }
    }
    return 0;
}
static void CheckDesktopSwitch(void) {
    if (g_inMenu || g_modal) return;
    GUID cur;
    int src = ReadCurDesk(&cur);
    if (src) {
        if (!g_haveReg) { g_regDesk = cur; g_haveReg = 1; Log(L"текущий виртуальный стол определён по реестру (источник %d)", src); }
        else if (memcmp(&cur, &g_regDesk, sizeof cur)) {
            g_regDesk = cur;
            Log(L"смена виртуального стола (реестр)");
            if (g_debug) { HWND fg = GetForegroundWindow(); wchar_t dd[700]; if (fg) { DescWin(fg, dd, 700); Log(L"  активное окно сейчас: %ls", dd); } }
            g_raiseTicks = 5;
        }
    } else if (g_vdm && g_npinned) { /* запасной способ: по активному окну */
        HWND fg = GetForegroundWindow();
        if (fg && !IsOurs(fg) && !BadClass(fg)) {
            int mine = 0;
            for (int i = 0; i < g_npinned; i++) if (g_pinned[i] == fg) mine = 1;
            GUID id;
            if (!mine && SUCCEEDED(g_vdm->v->GetWindowDesktopId(g_vdm, fg, &id))) {
                if (!g_haveDesk) { g_lastDesk = id; g_haveDesk = 1; }
                else if (memcmp(&id, &g_lastDesk, sizeof id)) { g_lastDesk = id; Log(L"смена виртуального стола (по активному окну)"); g_raiseTicks = 5; }
            }
        }
    }
    if (g_raiseTicks > 0) {
        g_raiseTicks--;
        if (g_npinned && g_cfg.raisePinned) RaisePinned();
    }
}

static HWND FgTarget(void) { /* активное окно "до нас" */
    HWND f = GetForegroundWindow();
    if (!f || IsOurs(f)) f = g_prevFg;
    return f;
}

/* свернуть окна только на одном мониторе */
static BOOL Candidate(HWND w) {
    if (!IsWindowVisible(w) || IsIconic(w)) return FALSE;
    LONG st = GetWindowLongW(w, GWL_STYLE), ex = GetWindowLongW(w, GWL_EXSTYLE);
    if (!(st & WS_MINIMIZEBOX)) return FALSE;
    if (ex & WS_EX_TOOLWINDOW) return FALSE;
    if ((ex & WS_EX_TOPMOST) && g_cfg.keepTop) return FALSE; /* по настройке: окна "поверх всех" не трогаем */
    if (GetWindow(w, GW_OWNER) && !(ex & WS_EX_APPWINDOW)) return FALSE;
    DWORD cloaked = 0;
    if (SUCCEEDED(DwmGetWindowAttribute(w, 13 /* DWMWA_CLOAKED */, &cloaked, sizeof cloaked)) && cloaked) return FALSE; /* другой рабочий стол */
    if (IsOurs(w) || BadClass(w)) return FALSE;
    if (!OnCurDesk(w)) return FALSE; /* окно другого рабочего стола */
    return TRUE;
}
typedef struct { HMONITOR mon; HWND *out; int n, max; } EW;
static BOOL CALLBACK EnumMin(HWND w, LPARAM l) {
    EW *e = (EW *)l;
    if (e->n >= e->max) return FALSE;
    if (!Candidate(w)) return TRUE;
    if (MonitorFromWindow(w, MONITOR_DEFAULTTONEAREST) != e->mon) return TRUE;
    e->out[e->n++] = w;
    return TRUE;
}
static void RestoreWin(HWND w, int wasMax) {
    ShowWindow(w, SW_SHOWNOACTIVATE); /* без активации: не вызывает переключение рабочего стола */
    if (IsIconic(w)) ShowWindow(w, SW_RESTORE);
    if (wasMax && !IsZoomed(w)) ShowWindow(w, SW_SHOWMAXIMIZED);
}
static void MonDeskToggle(MonBtn *b) {
    wchar_t d[700];
    if (!b) return;
    if (b->nsaved) {
        int any = 0;
        Log(L"[%ls] восстановление, запомнено окон: %d", b->dev, b->nsaved);
        for (int i = b->nsaved - 1; i >= 0; i--) {
            HWND w = b->saved[i];
            if (!IsWindow(w) || !IsIconic(w)) continue;
            int on = OnCurDesk(w);
            if (g_debug) { DescWin(w, d, 700); Log(L"  restore onCurrentDesktop=%d %ls", on, d); }
            if (!on) continue;
            RestoreWin(w, b->savedMax[i]);
            any = 1;
        }
        b->nsaved = 0;
        if (any) return;
    }
    EW e = { b->mon, b->saved, 0, 128 };
    EnumWindows(EnumMin, (LPARAM)&e);
    b->nsaved = e.n;
    Log(L"[%ls] сворачивание, окон: %d", b->dev, e.n);
    for (int i = 0; i < e.n; i++) {
        b->savedMax[i] = IsZoomed(b->saved[i]) ? 1 : 0;
        if (g_debug) { DescWin(b->saved[i], d, 700); Log(L"  minimize max=%d %ls", b->savedMax[i], d); }
        ShowWindow(b->saved[i], SW_SHOWMINNOACTIVE);
    }
}

/* оболочка Windows через COM: работает локально и не зависит от того, куда идут клавиши */
static void ShellCall(const wchar_t *method) {
    CLSID clsid;
    IDispatch *d = NULL;
    if (FAILED(CLSIDFromProgID(L"Shell.Application", &clsid))) return;
    if (FAILED(CoCreateInstance(&clsid, NULL, CLSCTX_ALL, &IID_IDispatch, (void **)&d)) || !d) return;
    DISPID id;
    LPOLESTR n = (LPOLESTR)method;
    if (SUCCEEDED(d->lpVtbl->GetIDsOfNames(d, &IID_NULL, &n, 1, LOCALE_USER_DEFAULT, &id))) {
        DISPPARAMS dp;
        memset(&dp, 0, sizeof dp);
        d->lpVtbl->Invoke(d, id, &IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_METHOD, &dp, NULL, NULL, NULL);
    }
    d->lpVtbl->Release(d);
}

/* клавиши: если активен RDP/VNC - сначала забрать фокус на локальную сторону */
static void LocalizeFocus(MonBtn *b) {
    if (!g_cfg.localKeys) return;
    HWND fg = GetForegroundWindow();
    if (!fg || IsOurs(fg)) return;
    wchar_t exe[MAX_PATH];
    ExeOf(fg, exe, MAX_PATH);
    if (!InList(g_remote, g_nremote, exe)) return;
    HWND me = b ? b->hwnd : NULL;
    if (me) SetForegroundWindow(me);
    if (!me || GetForegroundWindow() != me) {
        HWND tr = FindWindowW(L"Shell_TrayWnd", NULL);
        if (tr) SetForegroundWindow(tr);
    }
    Sleep(60);
}
static void SendWinKey(WORD vk, MonBtn *b) {
    LocalizeFocus(b);
    INPUT in[4];
    ZeroMemory(in, sizeof in);
    in[0].type = INPUT_KEYBOARD; in[0].ki.wVk = VK_LWIN;
    in[1].type = INPUT_KEYBOARD; in[1].ki.wVk = vk;
    in[2] = in[1]; in[2].ki.dwFlags = KEYEVENTF_KEYUP;
    in[3] = in[0]; in[3].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(4, in, sizeof(INPUT));
}

/* закрепление окна: поверх всех + на всех виртуальных рабочих столах (недокументированный COM Windows 10) */
static const GUID G_ImmersiveShell = { 0xC2F03A33, 0x21F5, 0x47FA, { 0xB4, 0xBB, 0x15, 0x63, 0x62, 0xA2, 0xF2, 0x39 } };
static const GUID G_SidPinned      = { 0xB5A399E7, 0x1C87, 0x46B8, { 0x88, 0xE9, 0xFC, 0x57, 0x47, 0xB1, 0x71, 0xBD } };
static const GUID G_IidPinned      = { 0x4CE81583, 0x1E4C, 0x4632, { 0xA6, 0x21, 0x07, 0xA5, 0x35, 0x43, 0x14, 0x8F } };
static const GUID G_IidViewColl    = { 0x1841C6D7, 0x4F9D, 0x42C0, { 0xAF, 0x41, 0x87, 0x47, 0x53, 0x8F, 0x10, 0xE5 } };
typedef struct {
    HRESULT (STDMETHODCALLTYPE *QI)(void *, REFIID, void **);
    ULONG (STDMETHODCALLTYPE *AddRef)(void *);
    ULONG (STDMETHODCALLTYPE *Release)(void *);
    void *GetViews, *GetViewsByZOrder, *GetViewsByAppUserModelId;
    HRESULT (STDMETHODCALLTYPE *GetViewForHwnd)(void *, HWND, void **);
} AVCVtbl;
typedef struct { AVCVtbl *v; } AVC;
typedef struct {
    HRESULT (STDMETHODCALLTYPE *QI)(void *, REFIID, void **);
    ULONG (STDMETHODCALLTYPE *AddRef)(void *);
    ULONG (STDMETHODCALLTYPE *Release)(void *);
    void *IsAppIdPinned, *PinAppID, *UnpinAppID;
    HRESULT (STDMETHODCALLTYPE *IsViewPinned)(void *, void *, BOOL *);
    HRESULT (STDMETHODCALLTYPE *PinView)(void *, void *);
    HRESULT (STDMETHODCALLTYPE *UnpinView)(void *, void *);
} PAVtbl;
typedef struct { PAVtbl *v; } PA;

static const GUID G_IidView = { 0x372E1D3B, 0x38D3, 0x42E4, { 0xA1, 0x5B, 0x8A, 0xB2, 0xB1, 0x78, 0xF5, 0x13 } };
static void SetErr(wchar_t *err, int en, const wchar_t *step, HRESULT hr) {
    if (!err) return;
    _snwprintf(err, (size_t)en, L"%ls: HRESULT 0x%08lX", step, (unsigned long)hr);
    err[en - 1] = 0;
}
/* op: 0 открепить, 1 закрепить, 2 спросить (1 = закреплено, 0 = нет); при ошибке -1 и текст в err */
static int PinWin(HWND w, int op, wchar_t *err, int en) {
    IServiceProvider *sp = NULL;
    AVC *coll = NULL;
    PA *pa = NULL;
    void *view = NULL, *view2 = NULL, *v = NULL;
    int res = -1;
    BOOL pinned = FALSE;
    HRESULT hr;
    if (err) err[0] = 0;
    hr = CoCreateInstance(&G_ImmersiveShell, NULL, CLSCTX_LOCAL_SERVER, &IID_IServiceProvider, (void **)&sp);
    Log(L"pin op=%d hwnd=%p: ImmersiveShell hr=0x%08lX", op, (void *)w, (unsigned long)hr);
    if (FAILED(hr) || !sp) { SetErr(err, en, L"шаг 1 (оболочка Windows)", hr); return -1; }
    hr = sp->lpVtbl->QueryService(sp, &G_IidViewColl, &G_IidViewColl, (void **)&coll);
    Log(L"pin: ApplicationViewCollection hr=0x%08lX", (unsigned long)hr);
    if (FAILED(hr) || !coll) { SetErr(err, en, L"шаг 2 (список окон)", hr); goto done; }
    hr = sp->lpVtbl->QueryService(sp, &G_SidPinned, &G_IidPinned, (void **)&pa);
    Log(L"pin: VirtualDesktopPinnedApps hr=0x%08lX", (unsigned long)hr);
    if (FAILED(hr) || !pa) { SetErr(err, en, L"шаг 3 (закрепление)", hr); goto done; }
    hr = coll->v->GetViewForHwnd(coll, w, &view);
    Log(L"pin: GetViewForHwnd hr=0x%08lX view=%p", (unsigned long)hr, view);
    if (FAILED(hr) || !view) {
        HWND ro = GetAncestor(w, GA_ROOTOWNER);
        if (ro && ro != w) {
            view = NULL;
            hr = coll->v->GetViewForHwnd(coll, ro, &view);
            Log(L"pin: повтор для окна-владельца hwnd=%p hr=0x%08lX view=%p", (void *)ro, (unsigned long)hr, view);
        }
    }
    if (FAILED(hr) || !view) { SetErr(err, en, L"шаг 4 (окно в списке)", FAILED(hr) ? hr : E_POINTER); goto done; }
    hr = ((IUnknown *)view)->lpVtbl->QueryInterface((IUnknown *)view, &G_IidView, &view2);
    Log(L"pin: QueryInterface(IApplicationView) hr=0x%08lX", (unsigned long)hr);
    v = (SUCCEEDED(hr) && view2) ? view2 : view;
    hr = pa->v->IsViewPinned(pa, v, &pinned);
    Log(L"pin: IsViewPinned hr=0x%08lX pinned=%d", (unsigned long)hr, (int)pinned);
    if (FAILED(hr)) { SetErr(err, en, L"шаг 5 (проверка закрепления)", hr); goto done; }
    if (op == 2) { res = pinned ? 1 : 0; goto done; }
    hr = S_OK;
    if (op == 1 && !pinned) hr = pa->v->PinView(pa, v);
    else if (op == 0 && pinned) hr = pa->v->UnpinView(pa, v);
    Log(L"pin: Pin/UnpinView hr=0x%08lX", (unsigned long)hr);
    if (FAILED(hr)) { SetErr(err, en, op ? L"шаг 6 (закрепить)" : L"шаг 6 (открепить)", hr); goto done; }
    res = 1;
done:
    if (view2) ((IUnknown *)view2)->lpVtbl->Release((IUnknown *)view2);
    if (view) ((IUnknown *)view)->lpVtbl->Release((IUnknown *)view);
    if (pa) pa->v->Release(pa);
    if (coll) coll->v->Release(coll);
    if (sp) sp->lpVtbl->Release(sp);
    return res;
}
static int IsPinned(HWND w) {
    if (GetWindowLongW(w, GWL_EXSTYLE) & WS_EX_TOPMOST) return 1;
    if (g_cfg.pinAll && PinWin(w, 2, NULL, 0) == 1) return 1;
    return 0;
}
static void PinToggle(HWND w) {
    if (w) w = GetAncestor(w, GA_ROOT);
    if (!w || !IsWindow(w) || IsOurs(w) || BadClass(w)) {
        MessageBoxW(NULL, L"Нет подходящего активного окна.", APPNAME, MB_ICONINFORMATION);
        return;
    }
    if (!g_cfg.pinTop && !g_cfg.pinAll) {
        MessageBoxW(NULL, L"В настройках отключены оба режима закрепления.\nВключите хотя бы один: Настройки → Закрепление окна.", APPNAME, MB_ICONINFORMATION);
        return;
    }
    if (g_debug) { wchar_t dd[700]; DescWin(w, dd, 700); Log(L"pin target: %ls", dd); }
    if (!IsPinned(w)) {
        int ok = 1;
        wchar_t err[300] = L"", build[120], msg[900];
        if (g_cfg.pinAll) ok = (PinWin(w, 1, err, 300) == 1);
        if (ok) {
            if (g_cfg.pinAll) PinnedAdd(w);
            if (g_cfg.pinTop) SetWindowPos(w, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        } else {
            WinBuild(build, 120);
            _snwprintf(msg, 900, L"Не удалось закрепить окно на всех рабочих столах.\n\n%ls\n%ls\n\n"
                       L"Окно не изменено. Закреплять только «поверх всех окон» можно так: Настройки → Закрепление окна → снять галочку «На всех рабочих столах».",
                       err, build);
            msg[899] = 0;
            MessageBoxW(NULL, msg, APPNAME, MB_ICONWARNING);
        }
    } else {
        if (GetWindowLongW(w, GWL_EXSTYLE) & WS_EX_TOPMOST) SetWindowPos(w, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        PinWin(w, 0, NULL, 0);
        PinnedRemove(w);
    }
}

/* ---------- действия ---------- */
static void RunSpec(const wchar_t *a, const wchar_t *args, MonBtn *b) {
    if (!a || !*a) return;
    if (a[0] == L'@') {
        if (!wcscmp(a, L"@mondesk")) MonDeskToggle(b ? b : (g_nmb ? g_mb[0] : NULL));
        else if (!wcscmp(a, L"@desktop")) ShellCall(L"ToggleDesktop");
        else if (!wcscmp(a, L"@minall")) ShellCall(L"MinimizeAll");
        else if (!wcscmp(a, L"@taskview")) SendWinKey(VK_TAB, b);
        else if (!wcscmp(a, L"@pin")) PinToggle(FgTarget());
        else if (!wcscmp(a, L"@taskmgr")) ShellExecuteW(NULL, L"open", L"taskmgr.exe", NULL, NULL, SW_SHOWNORMAL);
        else if (!wcscmp(a, L"@lock")) LockWorkStation();
        return;
    }
    wchar_t ex[1100], ea[1100];
    ExpandEnvironmentStringsW(a, ex, 1100);
    ea[0] = 0;
    if (args && *args) ExpandEnvironmentStringsW(args, ea, 1100);
    HINSTANCE r = ShellExecuteW(NULL, L"open", ex, ea[0] ? ea : NULL, g_dir, SW_SHOWNORMAL);
    if ((INT_PTR)r <= 32) {
        wchar_t msg[1300];
        _snwprintf(msg, 1300, L"Не удалось запустить:\n%ls", ex);
        msg[1299] = 0;
        MessageBoxW(NULL, msg, APPNAME, MB_ICONWARNING);
    }
}
static void RunAct(const Act *a, MonBtn *b) { RunSpec(a->action, a->args, b); }

/* ---------- кнопки на мониторах ---------- */
static void LoadIconCfg(void) {
    if (g_icon) { DestroyIcon(g_icon); g_icon = NULL; }
    if (!g_cfg.icon[0] || g_cfg.size < 10) return;
    if (!wcscmp(g_cfg.icon, L"@app")) { /* значок самой программы */
        g_icon = (HICON)LoadImageW(g_hinst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, g_cfg.size, g_cfg.size, 0);
        return;
    }
    wchar_t raw[MAX_PATH], p[MAX_PATH * 2];
    ExpandEnvironmentStringsW(g_cfg.icon, raw, MAX_PATH);
    if (raw[1] != L':' && raw[0] != L'\\') _snwprintf(p, MAX_PATH * 2, L"%ls%ls", g_dir, raw);
    else cp(p, WSZ(p), raw);
    p[MAX_PATH * 2 - 1] = 0;
    wchar_t *dot = wcsrchr(p, L'.');
    if (dot && !_wcsicmp(dot, L".ico"))
        g_icon = (HICON)LoadImageW(NULL, p, IMAGE_ICON, g_cfg.size, g_cfg.size, LR_LOADFROMFILE);
    else {
        HICON big = NULL;
        if (ExtractIconExW(p, 0, &big, NULL, 1) >= 1) g_icon = big;
    }
}
static void ApplyAlpha(MonBtn *b) {
    int pct = b->hover ? g_cfg.hover : g_cfg.opacity;
    SetLayeredWindowAttributes(b->hwnd, 0, (BYTE)(255 * pct / 100), LWA_ALPHA);
}
static void UpdateVis(MonBtn *b) {
    int want = b->enabled && !b->hidden;
    if (want) { if (!IsWindowVisible(b->hwnd)) ShowWindow(b->hwnd, SW_SHOWNOACTIVATE); }
    else if (IsWindowVisible(b->hwnd)) ShowWindow(b->hwnd, SW_HIDE);
}
static void ApplyBtn(MonBtn *b) {
    int s = g_cfg.size;
    int mw = b->rc.right - b->rc.left, mh = b->rc.bottom - b->rc.top;
    int dx = Clamp(b->dx, 0, mw > s ? mw - s : 0), dy = Clamp(b->dy, 0, mh > s ? mh - s : 0);
    int x = (b->corner & 1) ? b->rc.right - s - dx : b->rc.left + dx;
    int y = (b->corner & 2) ? b->rc.bottom - s - dy : b->rc.top + dy;
    SetWindowPos(b->hwnd, HWND_TOPMOST, x, y, s, s, SWP_NOACTIVATE);
    if (g_cfg.shape == 1) SetWindowRgn(b->hwnd, CreateEllipticRgn(0, 0, s + 1, s + 1), TRUE);
    else SetWindowRgn(b->hwnd, NULL, TRUE);
    ApplyAlpha(b);
    InvalidateRect(b->hwnd, NULL, TRUE);
    UpdateVis(b);
}
static void ApplyAll(void) {
    LoadIconCfg();
    for (int i = 0; i < g_nmb; i++) ApplyBtn(g_mb[i]);
}
static void EnableButtons(BOOL en) {
    for (int i = 0; i < g_nmb; i++) EnableWindow(g_mb[i]->hwnd, en);
}
static void EndDrag(MonBtn *b) {
    RECT r;
    GetWindowRect(b->hwnd, &r);
    int s = g_cfg.size;
    int cx = r.left + s / 2, cy = r.top + s / 2;
    int right = cx > (b->rc.left + b->rc.right) / 2, bottom = cy > (b->rc.top + b->rc.bottom) / 2;
    b->corner = (right ? 1 : 0) | (bottom ? 2 : 0);
    b->dx = right ? b->rc.right - r.right : r.left - b->rc.left;
    b->dy = bottom ? b->rc.bottom - r.bottom : r.top - b->rc.top;
    if (b->dx < 0) b->dx = 0;
    if (b->dy < 0) b->dy = 0;
    MonSave(b);
    ApplyBtn(b);
}

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l);
typedef struct { MONITORINFOEXW mi[MAXMON]; HMONITOR h[MAXMON]; int n; } MonEnum;
static BOOL CALLBACK MEnum(HMONITOR hm, HDC dc, LPRECT r, LPARAM l) {
    MonEnum *e = (MonEnum *)l;
    (void)dc; (void)r;
    if (e->n >= MAXMON) return TRUE;
    memset(&e->mi[e->n], 0, sizeof(MONITORINFOEXW));
    e->mi[e->n].cbSize = sizeof(MONITORINFOEXW);
    GetMonitorInfoW(hm, (MONITORINFO *)&e->mi[e->n]);
    e->h[e->n] = hm;
    e->n++;
    return TRUE;
}
static void SyncMonitors(void) {
    static MonEnum e;
    e.n = 0;
    EnumDisplayMonitors(NULL, NULL, MEnum, (LPARAM)&e);
    for (int i = 0; i < g_nmb; i++) g_mb[i]->alive = 0;
    for (int k = 0; k < e.n; k++) {
        MonBtn *b = NULL;
        for (int i = 0; i < g_nmb; i++) if (!wcscmp(g_mb[i]->dev, e.mi[k].szDevice)) b = g_mb[i];
        if (!b) {
            if (g_nmb >= MAXMON) continue;
            b = (MonBtn *)calloc(1, sizeof(MonBtn));
            if (!b) continue;
            cp(b->dev, WSZ(b->dev), e.mi[k].szDevice);
            MonKey(b->dev, b->key);
            b->primary = (e.mi[k].dwFlags & MONITORINFOF_PRIMARY) ? 1 : 0;
            MonLoad(b);
            b->mon = e.h[k];
            b->rc = e.mi[k].rcMonitor;
            b->hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"CBBtn", APPNAME, WS_POPUP,
                                      b->rc.left, b->rc.top, g_cfg.size, g_cfg.size, NULL, NULL, g_hinst, b);
            if (!b->hwnd) { free(b); continue; }
            g_mb[g_nmb++] = b;
        }
        b->alive = 1;
        b->mon = e.h[k];
        b->rc = e.mi[k].rcMonitor;
        b->primary = (e.mi[k].dwFlags & MONITORINFOF_PRIMARY) ? 1 : 0;
    }
    for (int i = g_nmb - 1; i >= 0; i--) {
        if (!g_mb[i]->alive) {
            DestroyWindow(g_mb[i]->hwnd);
            free(g_mb[i]);
            for (int j = i; j < g_nmb - 1; j++) g_mb[j] = g_mb[j + 1];
            g_nmb--;
        }
    }
    ApplyAll();
}
static void ReloadMonSettings(void) {
    for (int i = 0; i < g_nmb; i++) MonLoad(g_mb[i]);
}

/* исключения и поверх всех */
static void UpdateExclusion(void) {
    if (g_inMenu || g_modal) return;
    HWND fg = GetForegroundWindow();
    if (!fg || IsOurs(fg)) return;
    if (fg != g_lastFg) {
        wchar_t exe[MAX_PATH];
        g_lastFg = fg;
        ExeOf(fg, exe, MAX_PATH);
        g_lastFgEx = InList(g_excl, g_nexcl, exe);
    }
    HMONITOR m = MonitorFromWindow(fg, MONITOR_DEFAULTTONEAREST);
    for (int i = 0; i < g_nmb; i++) {
        int hid = g_lastFgEx && g_mb[i]->mon == m;
        if (hid != g_mb[i]->hidden) { g_mb[i]->hidden = hid; UpdateVis(g_mb[i]); }
    }
}
static void Reassert(void) {
    if (g_inMenu || g_modal) return;
    for (int i = 0; i < g_nmb; i++)
        if (!g_mb[i]->drag && IsWindowVisible(g_mb[i]->hwnd))
            SetWindowPos(g_mb[i]->hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

/* ---------- мини-диалоги ---------- */
typedef struct Dlg {
    HWND h;
    int done, result;
    void (*oncmd)(struct Dlg *, int, int);
    LRESULT (*onnotify)(struct Dlg *, NMHDR *);
} Dlg;

static LRESULT CALLBACK DlgProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_NCCREATE) SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)((CREATESTRUCTW *)l)->lpCreateParams);
    Dlg *d = (Dlg *)GetWindowLongPtrW(h, GWLP_USERDATA);
    switch (m) {
    case WM_COMMAND: if (d && d->oncmd) d->oncmd(d, LOWORD(w), HIWORD(w)); return 0;
    case WM_NOTIFY: if (d && d->onnotify) return d->onnotify(d, (NMHDR *)l); return 0;
    case WM_CLOSE: if (d) { d->result = 0; d->done = 1; } return 0;
    }
    return DefWindowProcW(h, m, w, l);
}
static HWND Mk(HWND p, const wchar_t *cls, const wchar_t *txt, DWORD st, DWORD ex, int x, int y, int w, int h, int id) {
    HWND c = CreateWindowExW(ex, cls, txt, WS_CHILD | WS_VISIBLE | st, S(x), S(y), S(w), S(h), p, (HMENU)(INT_PTR)id, g_hinst, NULL);
    SendMessageW(c, WM_SETFONT, (WPARAM)g_font, TRUE);
    return c;
}
static HWND Lbl(HWND p, const wchar_t *t, int x, int y, int w, int h) { return Mk(p, L"STATIC", t, SS_LEFT, 0, x, y, w, h, -1); }
static HWND Edt(HWND p, int x, int y, int w, int h, int id) { return Mk(p, L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, x, y, w, h, id); }
static HWND Btn(HWND p, const wchar_t *t, int x, int y, int w, int h, int id, int def) {
    return Mk(p, L"BUTTON", t, WS_TABSTOP | (def ? BS_DEFPUSHBUTTON : BS_PUSHBUTTON), 0, x, y, w, h, id);
}
static void NewDlg(Dlg *d, const wchar_t *title, int cw, int ch) {
    memset(d, 0, sizeof *d);
    DWORD st = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    RECT r = { 0, 0, S(cw), S(ch) };
    AdjustWindowRectEx(&r, st, FALSE, WS_EX_DLGMODALFRAME);
    int w = r.right - r.left, h = r.bottom - r.top;
    POINT pt;
    GetCursorPos(&pt);
    MONITORINFO mi;
    mi.cbSize = sizeof mi;
    GetMonitorInfoW(MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST), &mi);
    int x = (mi.rcWork.left + mi.rcWork.right - w) / 2, y = (mi.rcWork.top + mi.rcWork.bottom - h) / 2;
    d->h = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST, L"CBDlg", title, st, x, y, w, h, NULL, NULL, g_hinst, d);
}
static int RunModal(Dlg *d, HWND owner) {
    g_modal++;
    if (owner == g_ctl) EnableButtons(FALSE); else EnableWindow(owner, FALSE);
    ShowWindow(d->h, SW_SHOW);
    SetForegroundWindow(d->h);
    MSG msg;
    while (!d->done) {
        BOOL r = GetMessageW(&msg, NULL, 0, 0);
        if (r <= 0) { if (r == 0) PostQuitMessage((int)msg.wParam); break; }
        if (!IsDialogMessageW(d->h, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }
    if (owner == g_ctl) EnableButtons(TRUE);
    else { EnableWindow(owner, TRUE); SetForegroundWindow(owner); }
    DestroyWindow(d->h);
    g_modal--;
    return d->result;
}

/* --- диалог "Внешний вид" --- */
typedef struct { Dlg d; HWND eSize, eOp, eHov, eCol, eIcon, cbShape; } AppDlg;

static void AppCmd(Dlg *d, int id, int code) {
    AppDlg *a = (AppDlg *)d;
    (void)code;
    switch (id) {
    case 205: {
        wchar_t b[32];
        GetWindowTextW(a->eCol, b, 32);
        CHOOSECOLORW cc;
        memset(&cc, 0, sizeof cc);
        cc.lStructSize = sizeof cc;
        cc.hwndOwner = d->h;
        cc.rgbResult = ParseColor(b, g_cfg.color);
        cc.lpCustColors = g_cust;
        cc.Flags = CC_RGBINIT | CC_FULLOPEN;
        if (ChooseColorW(&cc)) {
            _snwprintf(b, 32, L"%02X%02X%02X", GetRValue(cc.rgbResult), GetGValue(cc.rgbResult), GetBValue(cc.rgbResult));
            b[31] = 0;
            SetWindowTextW(a->eCol, b);
        }
        break;
    }
    case 209: SetWindowTextW(a->eIcon, L"@app"); break;
    case 207: {
        wchar_t f[MAX_PATH] = L"";
        OPENFILENAMEW o;
        memset(&o, 0, sizeof o);
        o.lStructSize = sizeof o;
        o.hwndOwner = d->h;
        o.lpstrFilter = L"Иконки (*.ico;*.exe;*.dll)\0*.ico;*.exe;*.dll\0Все файлы\0*.*\0";
        o.lpstrFile = f;
        o.nMaxFile = MAX_PATH;
        o.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (GetOpenFileNameW(&o)) SetWindowTextW(a->eIcon, f);
        break;
    }
    case IDOK: {
        wchar_t b[MAX_PATH];
        GetWindowTextW(a->eSize, b, 32);  g_cfg.size = Clamp((int)wcstol(b, NULL, 10), 2, 200);
        GetWindowTextW(a->eOp, b, 32);    g_cfg.opacity = Clamp((int)wcstol(b, NULL, 10), 5, 100);
        GetWindowTextW(a->eHov, b, 32);   g_cfg.hover = Clamp((int)wcstol(b, NULL, 10), 5, 100);
        GetWindowTextW(a->eCol, b, 32);   g_cfg.color = ParseColor(b, g_cfg.color);
        GetWindowTextW(a->eIcon, g_cfg.icon, MAX_PATH);
        Trim(g_cfg.icon);
        g_cfg.shape = (int)SendMessageW(a->cbShape, CB_GETCURSEL, 0, 0) == 1 ? 1 : 0;
        d->result = 1;
        d->done = 1;
        break;
    }
    case IDCANCEL: d->done = 1; break;
    }
}
static void ShowAppearance(void) {
    AppDlg a;
    memset(&a, 0, sizeof a);
    NewDlg(&a.d, L"Внешний вид кнопок", 440, 290);
    a.d.oncmd = AppCmd;
    HWND h = a.d.h;
    wchar_t b[MAX_PATH];
    int y = 14;
    Lbl(h, L"Размер (пиксели, 2–200):", 12, y + 3, 230, 20);
    a.eSize = Edt(h, 250, y, 80, 24, 201);
    _snwprintf(b, 32, L"%d", g_cfg.size); SetWindowTextW(a.eSize, b);
    y += 32;
    Lbl(h, L"Непрозрачность, % (5–100):", 12, y + 3, 230, 20);
    a.eOp = Edt(h, 250, y, 80, 24, 202);
    _snwprintf(b, 32, L"%d", g_cfg.opacity); SetWindowTextW(a.eOp, b);
    y += 32;
    Lbl(h, L"Непрозрачность при наведении, %:", 12, y + 3, 235, 20);
    a.eHov = Edt(h, 250, y, 80, 24, 203);
    _snwprintf(b, 32, L"%d", g_cfg.hover); SetWindowTextW(a.eHov, b);
    y += 32;
    Lbl(h, L"Цвет (RRGGBB):", 12, y + 3, 230, 20);
    a.eCol = Edt(h, 250, y, 80, 24, 204);
    _snwprintf(b, 32, L"%02X%02X%02X", GetRValue(g_cfg.color), GetGValue(g_cfg.color), GetBValue(g_cfg.color));
    SetWindowTextW(a.eCol, b);
    Btn(h, L"Выбрать…", 340, y, 88, 24, 205, 0);
    y += 32;
    Lbl(h, L"Форма:", 12, y + 3, 230, 20);
    a.cbShape = Mk(h, L"COMBOBOX", L"", WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, 0, 250, y, 120, 120, 208);
    SendMessageW(a.cbShape, CB_ADDSTRING, 0, (LPARAM)L"Квадрат");
    SendMessageW(a.cbShape, CB_ADDSTRING, 0, (LPARAM)L"Круг");
    SendMessageW(a.cbShape, CB_SETCURSEL, g_cfg.shape, 0);
    y += 36;
    Lbl(h, L"Иконка: файл .ico/.exe/.dll или @app (значок программы). Пусто — без иконки. Рисуется при размере кнопки от 10.", 12, y, 416, 32);
    y += 34;
    a.eIcon = Edt(h, 12, y, 206, 24, 206);
    SetWindowTextW(a.eIcon, g_cfg.icon);
    Btn(h, L"Значок программы", 226, y, 112, 24, 209, 0);
    Btn(h, L"Обзор…", 346, y, 82, 24, 207, 0);
    y += 44;
    Btn(h, L"OK", 248, y, 85, 28, IDOK, 1);
    Btn(h, L"Отмена", 343, y, 85, 28, IDCANCEL, 0);
    if (RunModal(&a.d, g_ctl)) { SaveCfg(); ApplyAll(); }
}

/* --- диалог действия / пункта / папки --- */
enum { AM_ACTION = 0, AM_ITEM = 1, AM_FOLDER = 2 };
typedef struct { Dlg d; int mode; wchar_t name[128], action[520], args[260]; HWND eName, cb, ePath, eArgs; } ActDlg;

static void ActEnable(ActDlg *a) {
    BOOL open = (int)SendMessageW(a->cb, CB_GETCURSEL, 0, 0) == NBI;
    EnableWindow(a->ePath, open);
    EnableWindow(a->eArgs, open);
    EnableWindow(GetDlgItem(a->d.h, 305), open);
    EnableWindow(GetDlgItem(a->d.h, 306), open);
}
static void ActCmd(Dlg *d, int id, int code) {
    ActDlg *a = (ActDlg *)d;
    switch (id) {
    case 302:
        if (code == CBN_SELCHANGE) ActEnable(a);
        break;
    case 305: {
        wchar_t f[MAX_PATH] = L"";
        OPENFILENAMEW o;
        memset(&o, 0, sizeof o);
        o.lStructSize = sizeof o;
        o.hwndOwner = d->h;
        o.lpstrFilter = L"Все файлы\0*.*\0";
        o.lpstrFile = f;
        o.nMaxFile = MAX_PATH;
        o.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (GetOpenFileNameW(&o)) SetWindowTextW(a->ePath, f);
        break;
    }
    case 306: {
        BROWSEINFOW bi;
        memset(&bi, 0, sizeof bi);
        bi.hwndOwner = d->h;
        bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
        LPITEMIDLIST p = SHBrowseForFolderW(&bi);
        if (p) {
            wchar_t f[MAX_PATH];
            if (SHGetPathFromIDListW(p, f)) SetWindowTextW(a->ePath, f);
            CoTaskMemFree(p);
        }
        break;
    }
    case IDOK:
        if (a->mode != AM_ACTION) {
            GetWindowTextW(a->eName, a->name, 128);
            Trim(a->name);
            if (!a->name[0]) { MessageBoxW(d->h, L"Введите название.", APPNAME, MB_ICONINFORMATION); return; }
        }
        if (a->mode != AM_FOLDER) {
            int s = (int)SendMessageW(a->cb, CB_GETCURSEL, 0, 0);
            if (s >= 0 && s < NBI) { cp(a->action, WSZ(a->action), BI[s].id); a->args[0] = 0; }
            else {
                GetWindowTextW(a->ePath, a->action, 520);
                Trim(a->action);
                GetWindowTextW(a->eArgs, a->args, 260);
                if (!a->action[0]) { MessageBoxW(d->h, L"Введите путь, ссылку или программу.", APPNAME, MB_ICONINFORMATION); return; }
            }
        }
        d->result = 1;
        d->done = 1;
        break;
    case IDCANCEL: d->done = 1; break;
    }
}
/* возвращает 1 если OK; значения записываются в name/action/args только при OK */
static int EditActDlg(int mode, const wchar_t *title, wchar_t *name, wchar_t *action, wchar_t *args, HWND owner) {
    ActDlg a;
    memset(&a, 0, sizeof a);
    a.mode = mode;
    NewDlg(&a.d, title, 480, mode == AM_FOLDER ? 108 : (mode == AM_ITEM ? 276 : 220));
    a.d.oncmd = ActCmd;
    HWND h = a.d.h;
    int y = 12;
    if (mode != AM_ACTION) {
        Lbl(h, L"Название:", 12, y, 456, 18);
        y += 20;
        a.eName = Edt(h, 12, y, 456, 24, 301);
        SetWindowTextW(a.eName, name);
        y += 36;
    }
    if (mode != AM_FOLDER) {
        Lbl(h, L"Действие:", 12, y, 456, 18);
        y += 20;
        a.cb = Mk(h, L"COMBOBOX", L"", WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, 0, 12, y, 456, 260, 302);
        for (int i = 0; i < NBI; i++) SendMessageW(a.cb, CB_ADDSTRING, 0, (LPARAM)BI[i].label);
        SendMessageW(a.cb, CB_ADDSTRING, 0, (LPARAM)L"Открыть файл / папку / ссылку / программу");
        y += 36;
        Lbl(h, L"Путь, ссылка или программа:", 12, y, 456, 18);
        y += 20;
        a.ePath = Edt(h, 12, y, 300, 24, 303);
        Btn(h, L"Файл…", 320, y, 70, 24, 305, 0);
        Btn(h, L"Папка…", 396, y, 72, 24, 306, 0);
        y += 36;
        Lbl(h, L"Аргументы (необязательно):", 12, y, 456, 18);
        y += 20;
        a.eArgs = Edt(h, 12, y, 456, 24, 304);
        y += 36;
        int sel = NBI;
        for (int i = 0; i < NBI; i++) if (!wcscmp(action, BI[i].id)) sel = i;
        SendMessageW(a.cb, CB_SETCURSEL, sel, 0);
        if (sel == NBI) { SetWindowTextW(a.ePath, action); SetWindowTextW(a.eArgs, args); }
        ActEnable(&a);
    }
    Btn(h, L"OK", 288, y, 85, 28, IDOK, 1);
    Btn(h, L"Отмена", 383, y, 85, 28, IDCANCEL, 0);
    if (!RunModal(&a.d, owner)) return 0;
    if (mode != AM_ACTION) cp(name, 128, a.name);
    if (mode != AM_FOLDER) { cp(action, 520, a.action); cp(args, 260, a.args); }
    return 1;
}

/* --- диалог списка программ (исключения / RDP-VNC) --- */
typedef struct { Dlg d; HWND ed; wchar_t (*arr)[64]; int *n; } ListDlg;
static void ListCmd(Dlg *d, int id, int code) {
    ListDlg *l = (ListDlg *)d;
    (void)code;
    switch (id) {
    case 502: {
        wchar_t f[MAX_PATH] = L"";
        OPENFILENAMEW o;
        memset(&o, 0, sizeof o);
        o.lStructSize = sizeof o;
        o.hwndOwner = d->h;
        o.lpstrFilter = L"Программы (*.exe)\0*.exe\0Все файлы\0*.*\0";
        o.lpstrFile = f;
        o.nMaxFile = MAX_PATH;
        o.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (GetOpenFileNameW(&o)) {
            wchar_t *s = wcsrchr(f, L'\\');
            static wchar_t buf[MAXLIST * 66 + 80];
            GetWindowTextW(l->ed, buf, (int)WSZ(buf) - 70);
            size_t n = wcslen(buf);
            if (n && buf[n - 1] != L'\n') wcscat(buf, L"\r\n");
            wcsncat(buf, s ? s + 1 : f, 63);
            wcscat(buf, L"\r\n");
            SetWindowTextW(l->ed, buf);
        }
        break;
    }
    case IDOK: {
        static wchar_t buf[MAXLIST * 66 + 80];
        GetWindowTextW(l->ed, buf, (int)WSZ(buf));
        int n = 0;
        wchar_t *p = buf;
        while (*p && n < MAXLIST) {
            wchar_t *s = p;
            while (*p && *p != L'\r' && *p != L'\n') p++;
            wchar_t sv = *p;
            *p = 0;
            Trim(s);
            if (*s) cp(l->arr[n++], 64, s);
            if (sv) p++; else break;
        }
        *l->n = n;
        d->result = 1;
        d->done = 1;
        break;
    }
    case IDCANCEL: d->done = 1; break;
    }
}
static int EditList(const wchar_t *title, const wchar_t *hint, wchar_t (*arr)[64], int *n) {
    ListDlg l;
    memset(&l, 0, sizeof l);
    NewDlg(&l.d, title, 420, 330);
    l.d.oncmd = ListCmd;
    l.arr = arr;
    l.n = n;
    HWND h = l.d.h;
    Lbl(h, hint, 12, 10, 396, 56);
    l.ed = Mk(h, L"EDIT", L"", WS_TABSTOP | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN, WS_EX_CLIENTEDGE, 12, 70, 396, 190, 501);
    static wchar_t buf[MAXLIST * 66 + 80];
    buf[0] = 0;
    for (int i = 0; i < *n; i++) { wcscat(buf, arr[i]); wcscat(buf, L"\r\n"); }
    SetWindowTextW(l.ed, buf);
    Btn(h, L"Добавить .exe…", 12, 276, 130, 28, 502, 0);
    Btn(h, L"OK", 228, 276, 85, 28, IDOK, 1);
    Btn(h, L"Отмена", 323, 276, 85, 28, IDCANCEL, 0);
    return RunModal(&l.d, g_ctl);
}

/* --- редактор меню --- */
static HWND g_tree;
static HTREEITEM g_hti[MAXE];

static int EndOf(int i) {
    int d = g_e[i].depth, j = i + 1;
    while (j < g_n && g_e[j].depth > d) j++;
    return j;
}
static int PrevSib(int i) {
    int d = g_e[i].depth;
    for (int j = i - 1; j >= 0; j--) {
        if (g_e[j].depth < d) return -1;
        if (g_e[j].depth == d) return j;
    }
    return -1;
}
static int MoveBlock(int s, int e, int to, int dd) {
    int len = e - s;
    Entry *tmp = (Entry *)malloc((size_t)len * sizeof(Entry));
    if (!tmp) return s;
    memcpy(tmp, &g_e[s], (size_t)len * sizeof(Entry));
    memmove(&g_e[s], &g_e[e], (size_t)(g_n - e) * sizeof(Entry));
    int n2 = g_n - len;
    if (to > e) to -= len;
    memmove(&g_e[to + len], &g_e[to], (size_t)(n2 - to) * sizeof(Entry));
    for (int k = 0; k < len; k++) tmp[k].depth += dd;
    memcpy(&g_e[to], tmp, (size_t)len * sizeof(Entry));
    free(tmp);
    return to;
}
static void InsertAt(int pos, const Entry *e) {
    if (g_n >= MAXE) return;
    memmove(&g_e[pos + 1], &g_e[pos], (size_t)(g_n - pos) * sizeof(Entry));
    g_e[pos] = *e;
    g_n++;
}
static int SelIdx(void) {
    HTREEITEM h = TreeView_GetSelection(g_tree);
    if (!h) return -1;
    TVITEMW it;
    memset(&it, 0, sizeof it);
    it.mask = TVIF_PARAM;
    it.hItem = h;
    TreeView_GetItem(g_tree, &it);
    return (int)it.lParam;
}
static void Rebuild(int sel) {
    HTREEITEM stack[MAXD + 2];
    SendMessageW(g_tree, WM_SETREDRAW, FALSE, 0);
    TreeView_DeleteAllItems(g_tree);
    for (int i = 0; i < g_n; i++) {
        wchar_t txt[700];
        if (g_e[i].type == 2) cp(txt, WSZ(txt), L"──────────");
        else if (g_e[i].type == 1) cp(txt, WSZ(txt), g_e[i].name);
        else { _snwprintf(txt, 700, L"%ls    →  %ls", g_e[i].name, g_e[i].action); txt[699] = 0; }
        TVINSERTSTRUCTW t;
        memset(&t, 0, sizeof t);
        t.hParent = g_e[i].depth ? stack[g_e[i].depth - 1] : TVI_ROOT;
        t.hInsertAfter = TVI_LAST;
        t.item.mask = TVIF_TEXT | TVIF_PARAM;
        t.item.pszText = txt;
        t.item.lParam = i;
        g_hti[i] = TreeView_InsertItem(g_tree, &t);
        if (g_e[i].depth <= MAXD) stack[g_e[i].depth] = g_hti[i];
    }
    for (int i = 0; i < g_n; i++) if (g_e[i].type == 1) TreeView_Expand(g_tree, g_hti[i], TVE_EXPAND);
    if (sel >= 0 && sel < g_n) TreeView_SelectItem(g_tree, g_hti[sel]);
    else if (g_n > 0) TreeView_SelectItem(g_tree, g_hti[0]);
    SendMessageW(g_tree, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_tree, NULL, TRUE);
}
static int InsPos(int *depth) {
    int i = SelIdx();
    if (i < 0) { *depth = 0; return g_n; }
    if (g_e[i].type == 1) { *depth = g_e[i].depth + 1; return EndOf(i); }
    *depth = g_e[i].depth;
    return i + 1;
}
static void EdCmd(Dlg *d, int id, int code) {
    int i = SelIdx(), ns = -1, chg = 0;
    (void)code;
    switch (id) {
    case 411: /* вверх */
        if (i >= 0) { int p = PrevSib(i); if (p >= 0) { MoveBlock(i, EndOf(i), p, 0); ns = p; chg = 1; } }
        break;
    case 412: /* вниз */
        if (i >= 0) {
            int e = EndOf(i);
            if (e < g_n && g_e[e].depth == g_e[i].depth) {
                int e2 = EndOf(e);
                MoveBlock(i, e, e2, 0);
                ns = i + (e2 - e);
                chg = 1;
            }
        }
        break;
    case 413: /* из папки (на уровень выше) */
        if (i >= 0 && g_e[i].depth > 0) {
            int par = i - 1;
            while (par >= 0 && g_e[par].depth >= g_e[i].depth) par--;
            if (par >= 0) {
                int e = EndOf(i), pe = EndOf(par);
                MoveBlock(i, e, pe, -1);
                ns = pe - (e - i);
                chg = 1;
            }
        }
        break;
    case 414: /* в папку выше */
        if (i >= 0) {
            int p = PrevSib(i);
            if (p >= 0 && g_e[p].type == 1 && g_e[i].depth < MAXD) {
                int e = EndOf(i);
                for (int k = i; k < e; k++) g_e[k].depth++;
                ns = i;
                chg = 1;
            }
        }
        break;
    case 415: {
        Entry e;
        memset(&e, 0, sizeof e);
        if (EditActDlg(AM_ITEM, L"Новый пункт", e.name, e.action, e.args, d->h)) {
            int dp, pos = InsPos(&dp);
            e.type = 0; e.depth = dp;
            InsertAt(pos, &e);
            ns = pos; chg = 1;
        }
        break;
    }
    case 416: {
        Entry e;
        memset(&e, 0, sizeof e);
        cp(e.name, WSZ(e.name), L"Новая папка");
        if (EditActDlg(AM_FOLDER, L"Новая папка", e.name, e.action, e.args, d->h)) {
            int dp, pos = InsPos(&dp);
            e.type = 1; e.depth = dp;
            InsertAt(pos, &e);
            ns = pos; chg = 1;
        }
        break;
    }
    case 417: {
        Entry e;
        memset(&e, 0, sizeof e);
        int dp, pos = InsPos(&dp);
        e.type = 2; e.depth = dp;
        InsertAt(pos, &e);
        ns = pos; chg = 1;
        break;
    }
    case 418: /* изменить */
        if (i >= 0) {
            Entry *e = &g_e[i];
            if (e->type == 0) { if (EditActDlg(AM_ITEM, L"Изменить пункт", e->name, e->action, e->args, d->h)) { ns = i; chg = 1; } }
            else if (e->type == 1) { if (EditActDlg(AM_FOLDER, L"Изменить папку", e->name, e->action, e->args, d->h)) { ns = i; chg = 1; } }
        }
        break;
    case 419: /* удалить */
        if (i >= 0) {
            int e = EndOf(i);
            if (e - i > 1 && MessageBoxW(d->h, L"Удалить папку со всем содержимым?", APPNAME, MB_YESNO | MB_ICONQUESTION) != IDYES) break;
            memmove(&g_e[i], &g_e[e], (size_t)(g_n - e) * sizeof(Entry));
            g_n -= e - i;
            ns = i < g_n ? i : g_n - 1;
            chg = 1;
        }
        break;
    case IDOK: d->result = 1; d->done = 1; return;
    case IDCANCEL: d->done = 1; return;
    }
    if (chg) { Normalize(); Rebuild(ns); }
}
static LRESULT EdNotify(Dlg *d, NMHDR *nm) {
    if (nm->idFrom == 401 && nm->code == NM_DBLCLK) EdCmd(d, 418, 0);
    return 0;
}
static void ShowEditor(void) {
    static Entry bak[MAXE];
    int bn = g_n;
    memcpy(bak, g_e, sizeof(Entry) * (size_t)g_n);
    Dlg d;
    NewDlg(&d, L"Редактор меню", 560, 420);
    d.oncmd = EdCmd;
    d.onnotify = EdNotify;
    HWND h = d.h;
    g_tree = Mk(h, L"SysTreeView32", L"", WS_TABSTOP | TVS_HASLINES | TVS_HASBUTTONS | TVS_LINESATROOT | TVS_SHOWSELALWAYS,
                WS_EX_CLIENTEDGE, 12, 12, 370, 360, 401);
    int y = 12;
    Btn(h, L"↑  Вверх", 394, y, 154, 28, 411, 0); y += 34;
    Btn(h, L"↓  Вниз", 394, y, 154, 28, 412, 0); y += 34;
    Btn(h, L"←  Из папки", 394, y, 154, 28, 413, 0); y += 34;
    Btn(h, L"→  В папку выше", 394, y, 154, 28, 414, 0); y += 40;
    Btn(h, L"Добавить пункт…", 394, y, 154, 28, 415, 0); y += 34;
    Btn(h, L"Добавить папку…", 394, y, 154, 28, 416, 0); y += 34;
    Btn(h, L"Добавить разделитель", 394, y, 154, 28, 417, 0); y += 40;
    Btn(h, L"Изменить…", 394, y, 154, 28, 418, 0); y += 34;
    Btn(h, L"Удалить", 394, y, 154, 28, 419, 0);
    Lbl(h, L"Двойной щелчок по пункту — изменить.", 12, 384, 370, 18);
    Btn(h, L"Сохранить", 394, 380, 74, 28, IDOK, 1);
    Btn(h, L"Отмена", 474, 380, 74, 28, IDCANCEL, 0);
    Rebuild(-1);
    if (RunModal(&d, g_ctl)) SaveMenu();
    else { g_n = bn; memcpy(g_e, bak, sizeof(Entry) * (size_t)bn); }
}

/* ---------- меню ---------- */
enum { ID_APPEAR = 100, ID_CLICK, ID_DBL, ID_MID, ID_EDITMENU, ID_AUTORUN, ID_OPENINI, ID_RELOAD, ID_EXIT,
       ID_PIN, ID_EXCL, ID_REMOTE, ID_LOCALKEYS, ID_PINTOP, ID_PINALL, ID_KEEPTOP, ID_DEBUG, ID_COPYLOG, ID_RAISE, ID_CUSTOM = 1000, ID_MON = 2000 };

static int BuildSub(HMENU m, int *i, int depth) {
    int count = 0;
    while (*i < g_n && g_e[*i].depth >= depth) {
        Entry *e = &g_e[*i];
        if (e->depth > depth) { (*i)++; continue; }
        if (e->type == 2) { AppendMenuW(m, MF_SEPARATOR, 0, NULL); (*i)++; count++; }
        else if (e->type == 1) {
            HMENU sub = CreatePopupMenu();
            (*i)++;
            int c = BuildSub(sub, i, depth + 1);
            if (!c) AppendMenuW(sub, MF_GRAYED | MF_STRING, 0, L"(пусто)");
            AppendMenuW(m, MF_POPUP | MF_STRING, (UINT_PTR)sub, e->name);
            count++;
        } else { AppendMenuW(m, MF_STRING, ID_CUSTOM + *i, e->name); (*i)++; count++; }
    }
    return count;
}
static void LogStartup(void) {
    wchar_t bld[120];
    WinBuild(bld, 120);
    Log(L"=== CornerBtn: журнал включён. %ls. VirtualDesktopManager: %ls (hr=0x%08lX)", bld, g_vdm ? L"да" : L"НЕТ", (unsigned long)g_vdmHr);
    for (int i = 0; i < g_nmb; i++)
        Log(L"  монитор %d: %ls %ld,%ld..%ld,%ld основной=%d включён=%d угол=%d dx=%d dy=%d", i + 1, g_mb[i]->dev,
            g_mb[i]->rc.left, g_mb[i]->rc.top, g_mb[i]->rc.right, g_mb[i]->rc.bottom, g_mb[i]->primary, g_mb[i]->enabled, g_mb[i]->corner, g_mb[i]->dx, g_mb[i]->dy);
}
static void CopyLog(void) {
    HANDLE h = CreateFileW(g_log, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) { MessageBoxW(NULL, L"Журнал пуст. Включите его, повторите действие и скопируйте снова.", APPNAME, MB_ICONINFORMATION); return; }
    DWORD sz = GetFileSize(h, NULL), rd = 0, skip = 0;
    if (sz > 60000) { SetFilePointer(h, (LONG)(sz - 60000), NULL, FILE_END); sz = 60000; }
    BYTE *buf = (BYTE *)malloc(sz + 4);
    if (!buf) { CloseHandle(h); return; }
    ReadFile(h, buf, sz, &rd, NULL);
    CloseHandle(h);
    if (rd >= 2 && buf[0] == 0xFF && buf[1] == 0xFE) skip = 2;
    rd &= ~1u;
    HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, rd - skip + 2);
    if (g) {
        BYTE *p = (BYTE *)GlobalLock(g);
        memcpy(p, buf + skip, rd - skip);
        p[rd - skip] = 0; p[rd - skip + 1] = 0;
        GlobalUnlock(g);
        if (OpenClipboard(g_ctl)) { EmptyClipboard(); SetClipboardData(CF_UNICODETEXT, g); CloseClipboard(); }
        else GlobalFree(g);
    }
    free(buf);
    MessageBoxW(NULL, L"Журнал скопирован в буфер обмена. Вставьте его (Ctrl+V) в чат.", APPNAME, MB_ICONINFORMATION);
}
static void HandleCmd(int cmd, MonBtn *b) {
    if (cmd >= ID_CUSTOM && cmd < ID_CUSTOM + g_n) { RunSpec(g_e[cmd - ID_CUSTOM].action, g_e[cmd - ID_CUSTOM].args, b); return; }
    if (cmd >= ID_MON && cmd < ID_MON + MAXMON * 16) {
        int idx = (cmd - ID_MON) / 16, sub = (cmd - ID_MON) % 16;
        if (idx >= g_nmb) return;
        MonBtn *q = g_mb[idx];
        if (sub == 0) {
            int cnt = 0;
            for (int k = 0; k < g_nmb; k++) if (g_mb[k]->enabled) cnt++;
            if (q->enabled && cnt <= 1) { MessageBoxW(NULL, L"Нельзя отключить последнюю кнопку: иначе меню будет недоступно.", APPNAME, MB_ICONINFORMATION); return; }
            q->enabled = !q->enabled;
        } else if (sub >= 1 && sub <= 4) { q->corner = sub - 1; q->dx = q->dy = 0; }
        else if (sub == 5) { q->dx = q->dy = 0; }
        MonSave(q);
        ApplyBtn(q);
        return;
    }
    switch (cmd) {
    case ID_APPEAR: ShowAppearance(); break;
    case ID_CLICK:
        { wchar_t nm[1] = L""; if (EditActDlg(AM_ACTION, L"Действие: левый клик", nm, g_click.action, g_click.args, g_ctl)) SaveActs(); }
        break;
    case ID_DBL:
        { wchar_t nm[1] = L""; if (EditActDlg(AM_ACTION, L"Действие: двойной клик", nm, g_dbl.action, g_dbl.args, g_ctl)) SaveActs(); }
        break;
    case ID_MID:
        { wchar_t nm[1] = L""; if (EditActDlg(AM_ACTION, L"Действие: средняя кнопка", nm, g_mid.action, g_mid.args, g_ctl)) SaveActs(); }
        break;
    case ID_EDITMENU: ShowEditor(); break;
    case ID_PIN: PinToggle(g_prevFg); break;
    case ID_EXCL:
        if (EditList(L"Исключения: где скрывать кнопку",
                     L"Имена программ (по одному в строке, например vlc.exe). Когда окно такой программы активно, кнопка на том мониторе, где это окно, скрывается.",
                     g_excl, &g_nexcl)) { SaveList(L"Exclude", g_excl, g_nexcl); g_lastFg = NULL; }
        break;
    case ID_REMOTE:
        if (EditList(L"Программы удалённого доступа (RDP/VNC)",
                     L"Имена программ (по одному в строке). Если одна из них активна, клавиатурные действия (например Win+Tab) выполняются на ЭТОМ компьютере, а не в удалённом сеансе.",
                     g_remote, &g_nremote)) SaveList(L"Remote", g_remote, g_nremote);
        break;
    case ID_LOCALKEYS: g_cfg.localKeys = !g_cfg.localKeys; PutI(L"Options", L"LocalKeys", g_cfg.localKeys); break;
    case ID_PINTOP: g_cfg.pinTop = !g_cfg.pinTop; PutI(L"Options", L"PinTopmost", g_cfg.pinTop); break;
    case ID_PINALL: g_cfg.pinAll = !g_cfg.pinAll; PutI(L"Options", L"PinAllDesktops", g_cfg.pinAll); break;
    case ID_RAISE: g_cfg.raisePinned = !g_cfg.raisePinned; PutI(L"Options", L"RaisePinned", g_cfg.raisePinned); break;
    case ID_KEEPTOP: g_cfg.keepTop = !g_cfg.keepTop; PutI(L"Options", L"KeepTopmost", g_cfg.keepTop); break;
    case ID_DEBUG: g_debug = !g_debug; PutI(L"Options", L"Debug", g_debug); if (g_debug) { DeleteFileW(g_log); LogStartup(); } break;
    case ID_COPYLOG: CopyLog(); break;
    case ID_AUTORUN: AutoSet(!AutoGet()); break;
    case ID_OPENINI: ShellExecuteW(NULL, L"open", L"notepad.exe", g_ini, NULL, SW_SHOWNORMAL); break;
    case ID_RELOAD: LoadAll(); ReloadMonSettings(); ApplyAll(); break;
    case ID_EXIT: DestroyWindow(g_ctl); break;
    }
}
static void ShowMenu(MonBtn *b) {
    HWND f = GetForegroundWindow();
    if (f && !IsOurs(f)) g_prevFg = f;
    HMENU m = CreatePopupMenu();
    int i = 0;
    if (BuildSub(m, &i, 0)) AppendMenuW(m, MF_SEPARATOR, 0, NULL);

    int pinned = 0;
    if (g_prevFg && IsWindow(g_prevFg) && !BadClass(g_prevFg)) pinned = IsPinned(GetAncestor(g_prevFg, GA_ROOT));
    AppendMenuW(m, MF_STRING, ID_PIN, pinned ? L"Открепить активное окно" : L"Закрепить активное окно");

    HMENU s = CreatePopupMenu(), mm = CreatePopupMenu();
    AppendMenuW(s, MF_STRING, ID_APPEAR, L"Внешний вид…");
    AppendMenuW(s, MF_STRING, ID_CLICK, L"Действие левого клика…");
    AppendMenuW(s, MF_STRING, ID_DBL, L"Действие двойного клика…");
    AppendMenuW(s, MF_STRING, ID_MID, L"Действие средней кнопки…");
    AppendMenuW(s, MF_SEPARATOR, 0, NULL);
    for (int k = 0; k < g_nmb; k++) {
        MonBtn *q = g_mb[k];
        HMENU sm = CreatePopupMenu();
        int base = ID_MON + k * 16;
        static const wchar_t *cn[4] = { L"Левый верхний угол", L"Правый верхний угол", L"Левый нижний угол", L"Правый нижний угол" };
        AppendMenuW(sm, MF_STRING | (q->enabled ? MF_CHECKED : 0), base, L"Показывать кнопку на этом мониторе");
        AppendMenuW(sm, MF_SEPARATOR, 0, NULL);
        for (int c = 0; c < 4; c++) AppendMenuW(sm, MF_STRING | (q->corner == c ? MF_CHECKED : 0), base + 1 + c, cn[c]);
        AppendMenuW(sm, MF_SEPARATOR, 0, NULL);
        AppendMenuW(sm, MF_STRING, base + 5, L"Сбросить отступ от угла");
        wchar_t t[160];
        _snwprintf(t, 160, L"Монитор %d%ls — %dx%d%ls", k + 1, q->primary ? L" (основной)" : L"",
                   (int)(q->rc.right - q->rc.left), (int)(q->rc.bottom - q->rc.top), q == b ? L"   ← эта кнопка" : L"");
        t[159] = 0;
        AppendMenuW(mm, MF_POPUP, (UINT_PTR)sm, t);
    }
    AppendMenuW(s, MF_POPUP, (UINT_PTR)mm, L"Мониторы и углы");
    AppendMenuW(s, MF_STRING, ID_EDITMENU, L"Редактор меню…");
    AppendMenuW(s, MF_SEPARATOR, 0, NULL);
    AppendMenuW(s, MF_STRING, ID_EXCL, L"Исключения: где скрывать кнопку…");
    HMENU pm = CreatePopupMenu();
    AppendMenuW(pm, MF_STRING | (g_cfg.pinTop ? MF_CHECKED : 0), ID_PINTOP, L"Поверх всех окон");
    AppendMenuW(pm, MF_STRING | (g_cfg.pinAll ? MF_CHECKED : 0), ID_PINALL, L"На всех рабочих столах");
    AppendMenuW(pm, MF_SEPARATOR, 0, NULL);
    AppendMenuW(pm, MF_STRING | (g_cfg.raisePinned ? MF_CHECKED : 0), ID_RAISE, L"Поднимать закреплённые окна после смены стола");
    AppendMenuW(pm, MF_STRING | (g_cfg.keepTop ? MF_CHECKED : 0), ID_KEEPTOP, L"Не сворачивать кнопкой окна «поверх всех»");
    AppendMenuW(s, MF_POPUP, (UINT_PTR)pm, L"Закрепление окна");
    AppendMenuW(s, MF_STRING | (g_cfg.localKeys ? MF_CHECKED : 0), ID_LOCALKEYS, L"Клавиши выполнять локально (если активен RDP/VNC)");
    AppendMenuW(s, MF_STRING, ID_REMOTE, L"Список программ RDP/VNC…");
    AppendMenuW(s, MF_SEPARATOR, 0, NULL);
    AppendMenuW(s, MF_STRING | (AutoGet() ? MF_CHECKED : 0), ID_AUTORUN, L"Запускать вместе с Windows");
    AppendMenuW(s, MF_STRING | (g_debug ? MF_CHECKED : 0), ID_DEBUG, L"Журнал диагностики (CornerBtn.log)");
    if (g_debug) AppendMenuW(s, MF_STRING, ID_COPYLOG, L"Скопировать журнал в буфер обмена");
    AppendMenuW(s, MF_STRING, ID_OPENINI, L"Открыть файл настроек (.ini)");
    AppendMenuW(s, MF_STRING, ID_RELOAD, L"Перечитать настройки");
    AppendMenuW(m, MF_POPUP, (UINT_PTR)s, L"Настройки");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_GRAYED | MF_STRING, 0, L"CornerBtn версия " APPVER);
    AppendMenuW(m, MF_STRING, ID_EXIT, L"Выход");

    POINT pt;
    GetCursorPos(&pt);
    g_inMenu = 1;
    SetForegroundWindow(b->hwnd);
    int cmd = (int)TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, b->hwnd, NULL);
    PostMessageW(b->hwnd, WM_NULL, 0, 0);
    g_inMenu = 0;
    DestroyMenu(m);
    if (cmd) HandleCmd(cmd, b);
}

/* ---------- оконные процедуры ---------- */
static void BeginDrag(MonBtn *b) {
    b->drag = 1;
    SetCapture(b->hwnd);
    GetCursorPos(&b->dp);
    RECT r;
    GetWindowRect(b->hwnd, &r);
    b->wo.x = r.left;
    b->wo.y = r.top;
}
static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_NCCREATE) SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)((CREATESTRUCTW *)l)->lpCreateParams);
    MonBtn *b = (MonBtn *)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (!b) return DefWindowProcW(h, m, w, l);
    switch (m) {
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT r;
        GetClientRect(h, &r);
        HBRUSH br = CreateSolidBrush(g_cfg.color);
        FillRect(dc, &r, br);
        DeleteObject(br);
        if (g_icon) DrawIconEx(dc, 0, 0, g_icon, g_cfg.size, g_cfg.size, 0, NULL, DI_NORMAL);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN:
        if (GetKeyState(VK_SHIFT) < 0) BeginDrag(b);
        return 0;
    case WM_MOUSEMOVE:
        if (!b->hover) {
            TRACKMOUSEEVENT t = { sizeof t, TME_LEAVE, h, 0 };
            TrackMouseEvent(&t);
            b->hover = 1;
            ApplyAlpha(b);
        }
        if (b->drag) {
            POINT p;
            GetCursorPos(&p);
            int s = g_cfg.size;
            int x = Clamp(b->wo.x + p.x - b->dp.x, b->rc.left, b->rc.right - s);
            int y = Clamp(b->wo.y + p.y - b->dp.y, b->rc.top, b->rc.bottom - s);
            SetWindowPos(h, HWND_TOPMOST, x, y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
        }
        return 0;
    case WM_MOUSELEAVE:
        b->hover = 0;
        ApplyAlpha(b);
        return 0;
    case WM_LBUTTONUP:
        if (b->drag) { b->drag = 0; ReleaseCapture(); EndDrag(b); return 0; }
        if (b->skipUp) { b->skipUp = 0; return 0; }
        if (!g_dbl.action[0] || !wcscmp(g_dbl.action, L"@none")) RunAct(&g_click, b);
        else SetTimer(h, 1, GetDoubleClickTime(), NULL);
        return 0;
    case WM_LBUTTONDBLCLK:
        KillTimer(h, 1);
        if (GetKeyState(VK_SHIFT) < 0) { BeginDrag(b); return 0; }
        b->skipUp = 1;
        RunAct(&g_dbl, b);
        return 0;
    case WM_MBUTTONUP:
        RunAct(&g_mid, b);
        return 0;
    case WM_RBUTTONUP:
        KillTimer(h, 1);
        ShowMenu(b);
        return 0;
    case WM_TIMER:
        if (w == 1) { KillTimer(h, 1); RunAct(&g_click, b); }
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}
static LRESULT CALLBACK CtlProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_DISPLAYCHANGE: SetTimer(h, 9, 700, NULL); return 0;
    case WM_TIMER:
        if (w == 9) { KillTimer(h, 9); SyncMonitors(); }
        else if (w == 2) Reassert();
        else if (w == 3) { UpdateExclusion(); CheckDesktopSwitch(); }
        return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE hp, PWSTR cl, int ns) {
    (void)hp; (void)cl; (void)ns;
    g_hinst = hi;
    HANDLE mtx = CreateMutexW(NULL, FALSE, L"Local\\CornerBtn_SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;
    (void)mtx;
    CoInitialize(NULL);
    INITCOMMONCONTROLSEX icc = { sizeof icc, ICC_TREEVIEW_CLASSES | ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);

    HDC dc = GetDC(NULL);
    g_dpi = GetDeviceCaps(dc, LOGPIXELSX);
    ReleaseDC(NULL, dc);
    NONCLIENTMETRICSW ncm;
    memset(&ncm, 0, sizeof ncm);
    ncm.cbSize = sizeof ncm;
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0);
    g_font = CreateFontIndirectW(&ncm.lfMessageFont);

    GetModuleFileNameW(NULL, g_ini, MAX_PATH);
    cp(g_dir, MAX_PATH, g_ini);
    wchar_t *sl = wcsrchr(g_dir, L'\\');
    if (sl) sl[1] = 0;
    _snwprintf(g_log, MAX_PATH, L"%lsCornerBtn.log", g_dir);
    g_log[MAX_PATH - 1] = 0;
    wchar_t *dot = wcsrchr(g_ini, L'.');
    if (dot) *dot = 0;
    wcscat(g_ini, L".ini");

    g_vdmHr = CoCreateInstance(&G_ClsidVDM, NULL, CLSCTX_ALL, &G_IidVDM, (void **)&g_vdm);
    int fresh = EnsureIni();
    LoadAll();

    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof wc);
    wc.cbSize = sizeof wc;
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hi;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.lpszClassName = L"CBBtn";
    RegisterClassExW(&wc);
    memset(&wc, 0, sizeof wc);
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = DlgProc;
    wc.hInstance = hi;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.hIcon = LoadIconW(hi, MAKEINTRESOURCEW(IDI_APP));
    wc.hIconSm = wc.hIcon;
    wc.lpszClassName = L"CBDlg";
    RegisterClassExW(&wc);
    memset(&wc, 0, sizeof wc);
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = CtlProc;
    wc.hInstance = hi;
    wc.lpszClassName = L"CBCtl";
    RegisterClassExW(&wc);

    g_ctl = CreateWindowExW(WS_EX_TOOLWINDOW, L"CBCtl", APPNAME, WS_POPUP, 0, 0, 0, 0, NULL, NULL, hi, NULL);
    if (!g_ctl) return 1;
    SyncMonitors();
    if (g_nmb == 0) return 1;
    if (g_debug) { DeleteFileW(g_log); LogStartup(); }
    if (fresh) SaveAll();
    SetTimer(g_ctl, 2, 1000, NULL); /* держать кнопки поверх всех */
    SetTimer(g_ctl, 3, 300, NULL);  /* проверка исключений */

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    CoUninitialize();
    return 0;
}
