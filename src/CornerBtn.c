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
#include <shobjidl.h>
#include <servprov.h>
#include <oleauto.h>
#include <dwmapi.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>
#include <stdarg.h>

#define APPNAME L"CornerBtn"
#define APPVER L"2.6.0"
#define IDI_APP 100
#define MAXE 300
#define NSEC 4
#define ID_ENT0 1000
enum { SEC_PROG = 0, SEC_SCRIPT = 1, SEC_FOLDER = 2, SEC_FILES = 3 };
#define MAXD 7
#define MAXMON 8
#define MAXLIST 64
#define WSZ(a) (sizeof(a) / sizeof((a)[0]))

/* ---------- данные ---------- */
typedef struct { wchar_t action[520]; wchar_t args[260]; } Act;
typedef struct { int depth, type, flags; wchar_t name[128], action[520], args[260], icon[260], note[300], interp[260]; } Entry; /* type: 0 пункт, 1 категория, 2 разделитель; flags: 1 админ, 2 скрытое окно */

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

static Entry g_ent[NSEC][MAXE]; /* три раздела меню: Программы, Скрипты и команды, Папки */
static int g_cnt[NSEC];
static int g_cur;               /* раздел, с которым сейчас работают функции редактора */
#define g_e g_ent[g_cur]
#define g_n g_cnt[g_cur]
static HWND g_trees[NSEC];
static HTREEITEM g_htis[NSEC][MAXE];
#define g_tree g_trees[g_cur]
#define g_hti g_htis[g_cur]
static Act g_click, g_dbl, g_mid;
static struct { int size, opacity, hover, shape, localKeys, pinTop, pinAll, keepTop, raisePinned, show; COLORREF color; wchar_t icon[MAX_PATH]; wchar_t editor[MAX_PATH * 2]; } g_cfg;
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

/* ---------- разделы меню (Программы / Скрипты / Папки): хранение в ini ---------- */
static const wchar_t *g_secName[NSEC] = { L"Programs", L"Scripts", L"Folders", L"Files" };
static wchar_t g_line[7000];

static void EscCat(wchar_t *dst, size_t n, const wchar_t *s) {
    size_t l = wcslen(dst);
    for (; *s && l + 4 < n; s++) {
        const wchar_t *r = NULL;
        if (*s == L'%') r = L"%25";
        else if (*s == L'|') r = L"%7C";
        else if (*s == L'\r') r = L"%0D";
        else if (*s == L'\n') r = L"%0A";
        if (r) { wcscpy(dst + l, r); l += 3; } else dst[l++] = *s;
    }
    dst[l] = 0;
}
static void Bar(wchar_t *dst, size_t n) {
    size_t l = wcslen(dst);
    if (l + 2 < n) { dst[l] = L'|'; dst[l + 1] = 0; }
}
static void Unesc(wchar_t *s) {
    wchar_t *w = s;
    if (!*s) return;
    while (*s) {
        if (s[0] == L'%' && s[1] && s[2]) {
            if (!_wcsnicmp(s, L"%25", 3)) { *w++ = L'%'; s += 3; continue; }
            if (!_wcsnicmp(s, L"%7C", 3)) { *w++ = L'|'; s += 3; continue; }
            if (!_wcsnicmp(s, L"%0D", 3)) { *w++ = L'\r'; s += 3; continue; }
            if (!_wcsnicmp(s, L"%0A", 3)) { *w++ = L'\n'; s += 3; continue; }
        }
        *w++ = *s++;
    }
    *w = 0;
}
static void SaveSection(int s) {
    int keep = g_cur;
    g_cur = s;
    const wchar_t *sec = g_secName[s];
    WritePrivateProfileStringW(sec, NULL, NULL, g_ini); /* удалить секцию */
    PutI(sec, L"Count", g_n);
    for (int i = 0; i < g_n; i++) {
        Entry *e = &g_e[i];
        wchar_t key[16];
        _snwprintf(key, 16, L"I%d", i + 1);
        key[15] = 0;
        _snwprintf(g_line, 7000, L"%d|%d|%d|", e->depth, e->type, e->flags);
        g_line[6999] = 0;
        EscCat(g_line, 7000, e->name);   Bar(g_line, 7000);
        EscCat(g_line, 7000, e->action); Bar(g_line, 7000);
        EscCat(g_line, 7000, e->args);   Bar(g_line, 7000);
        EscCat(g_line, 7000, e->icon);   Bar(g_line, 7000);
        EscCat(g_line, 7000, e->note);   Bar(g_line, 7000);
        EscCat(g_line, 7000, e->interp);
        PutS(sec, key, g_line);
    }
    g_cur = keep;
}
static void SaveSections(void) {
    for (int s = 0; s < NSEC; s++) SaveSection(s);
}
static void LoadLegacy(int cnt) { /* старый формат [Menu] из версий до 2.5 */
    for (int i = 1; i <= cnt && g_n < MAXE; i++) {
        wchar_t key[16];
        _snwprintf(key, 16, L"I%d", i);
        key[15] = 0;
        GetS(L"Menu", key, L"", g_line, 1500);
        if (!g_line[0]) continue;
        wchar_t *p = g_line;
        wchar_t *d = Fld(&p), *t = Fld(&p), *n = Fld(&p), *a = Fld(&p);
        AddE(_wtoi(d), _wtoi(t), n, a, p ? p : L"");
        if (g_e[g_n - 1].type < 0 || g_e[g_n - 1].type > 2) g_e[g_n - 1].type = 0;
    }
    /* убрать папку-обёртку "Программы" из старого стартового меню: раздел и так называется "Программы" */
    if (g_n > 0 && g_e[0].type == 1 && !wcscmp(g_e[0].name, L"Программы")) {
        int e = 1;
        while (e < g_n && g_e[e].depth > g_e[0].depth) e++;
        for (int k = 1; k < e; k++) g_e[k].depth--;
        memmove(&g_e[0], &g_e[1], (size_t)(g_n - 1) * sizeof(Entry));
        g_n--;
    }
}
static int LoadSections(void) { /* 1, если нужно сохранить (первый запуск или миграция) */
    int need = 0, legacy = GetI(L"Menu", L"Count", -1);
    for (int s = 0; s < NSEC; s++) {
        g_cur = s;
        g_n = 0;
        int cnt = GetI(g_secName[s], L"Count", -1);
        if (cnt < 0) {
            need = 1;
            if (s == SEC_PROG && legacy >= 0) LoadLegacy(legacy);
            else if (s == SEC_PROG) {
                AddE(0, 0, L"Блокнот", L"notepad.exe", L"");
                AddE(0, 0, L"Калькулятор", L"calc.exe", L"");
                AddE(0, 0, L"Диспетчер задач", L"@taskmgr", L"");
                AddE(0, 2, L"", L"", L"");
                AddE(0, 0, L"Заблокировать компьютер", L"@lock", L"");
            } else if (s == SEC_FOLDER) {
                AddE(0, 0, L"Рабочий стол", L"%USERPROFILE%\\Desktop", L"");
                AddE(0, 0, L"Загрузки", L"%USERPROFILE%\\Downloads", L"");
                AddE(0, 0, L"Документы", L"%USERPROFILE%\\Documents", L"");
            }
        } else {
            for (int i = 1; i <= cnt && g_n < MAXE; i++) {
                wchar_t key[16];
                _snwprintf(key, 16, L"I%d", i);
                key[15] = 0;
                GetS(g_secName[s], key, L"", g_line, 7000);
                if (!g_line[0]) continue;
                wchar_t *p = g_line;
                wchar_t *d = Fld(&p), *t = Fld(&p), *f = Fld(&p), *n = Fld(&p), *a = Fld(&p), *ar = Fld(&p), *ic = Fld(&p), *no = Fld(&p), *ip = Fld(&p);
                Unesc(n); Unesc(a); Unesc(ar); Unesc(ic); Unesc(no); Unesc(ip);
                AddE(_wtoi(d), _wtoi(t), n, a, ar);
                Entry *e = &g_e[g_n - 1];
                e->flags = _wtoi(f);
                cp(e->icon, WSZ(e->icon), ic);
                cp(e->note, WSZ(e->note), no);
                cp(e->interp, WSZ(e->interp), ip);
                if (e->type < 0 || e->type > 2) e->type = 0;
            }
        }
        Normalize();
    }
    g_cur = 0;
    return need;
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
    g_cfg.show = GetI(L"Options", L"ShowButtons", 1) ? 1 : 0;
    GetS(L"Options", L"TextEditor", L"", g_cfg.editor, MAX_PATH * 2);
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

    if (LoadSections()) {
        SaveSections();
        WritePrivateProfileStringW(L"Menu", NULL, NULL, g_ini); /* старый формат больше не нужен */
    }
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
    PutI(L"Options", L"ShowButtons", g_cfg.show);
    PutS(L"Options", L"TextEditor", g_cfg.editor);
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
    SaveSections();
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
    int want = g_cfg.show && b->enabled && !b->hidden;
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

/* ---------- запуск записей меню, иконки в меню, подсказки-аннотации ---------- */
static int IsElevated(void) {
    HANDLE tk;
    TOKEN_ELEVATION te;
    DWORD sz = 0;
    int r = 0;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tk)) {
        if (GetTokenInformation(tk, TokenElevation, &te, sizeof te, &sz)) r = te.TokenIsElevated ? 1 : 0;
        CloseHandle(tk);
    }
    return r;
}
static int TokenSplit(const wchar_t *s, wchar_t *tok, int n, const wchar_t **rest) {
    int k = 0;
    while (*s && iswspace(*s)) s++;
    if (*s == L'"') {
        s++;
        while (*s && *s != L'"') { if (k < n - 1) tok[k++] = *s; s++; }
        if (*s) s++;
    } else {
        while (*s && !iswspace(*s)) { if (k < n - 1) tok[k++] = *s; s++; }
    }
    tok[k] = 0;
    while (*s && iswspace(*s)) s++;
    if (rest) *rest = s;
    return k;
}
static int ExtIn(const wchar_t *ext, const wchar_t *const *list) {
    for (; *list; list++) if (!wcscmp(ext, *list)) return 1;
    return 0;
}
static void RunScript(const Entry *e) {
    static const wchar_t *const wshExt[] = { L".vbs", L".vbe", L".js", L".jse", L".wsf", L".wsh", NULL };
    static const wchar_t *const cmdExt[] = { L".bat", L".cmd", NULL };
    wchar_t path[1100], args[1300], dir[MAX_PATH * 2], exe[1100], params[3000], ext[16] = L"";
    const wchar_t *pre = L"";
    int mode = 0; /* 0 - по ассоциации, 1 - exe + параметры, 2 - cmd /c */
    ExpandEnvironmentStringsW(e->action, path, 1100);
    ExpandEnvironmentStringsW(e->args, args, 1300);
    if (!path[0]) return;
    cp(dir, WSZ(dir), path);
    wchar_t *sl = wcsrchr(dir, L'\\');
    if (sl) *sl = 0; else cp(dir, WSZ(dir), g_dir);
    const wchar_t *dot = wcsrchr(path, L'.');
    if (dot && wcslen(dot) < 15) { cp(ext, 16, dot); for (wchar_t *c = ext; *c; c++) *c = (wchar_t)towlower(*c); }
    exe[0] = 0;
    if (e->interp[0]) {
        wchar_t tok[1100];
        const wchar_t *rest = L"";
        TokenSplit(e->interp, tok, 1100, &rest);
        ExpandEnvironmentStringsW(tok, exe, 1100);
        pre = rest;
        mode = 1;
    } else if (ExtIn(ext, cmdExt)) mode = 2;
    else if (ExtIn(ext, wshExt)) { cp(exe, WSZ(exe), L"wscript.exe"); mode = 1; }
    else if (!wcscmp(ext, L".ps1")) { cp(exe, WSZ(exe), L"powershell.exe"); pre = L"-NoProfile -ExecutionPolicy Bypass -File"; mode = 1; }
    if (mode == 2) { cp(exe, WSZ(exe), L"cmd.exe"); _snwprintf(params, 3000, L"/c \"\"%ls\" %ls\"", path, args); }
    else if (mode == 1) _snwprintf(params, 3000, L"%ls \"%ls\" %ls", pre, path, args);
    else { cp(exe, WSZ(exe), path); cp(params, WSZ(params), args); }
    params[2999] = 0;
    Trim(params);
    HINSTANCE r = ShellExecuteW(NULL, (e->flags & 1) ? L"runas" : L"open", exe, params[0] ? params : NULL, dir, (e->flags & 2) ? SW_HIDE : SW_SHOWNORMAL);
    if ((INT_PTR)r <= 32 && (INT_PTR)r != 5 /* отказ в UAC */) {
        wchar_t msg[1300];
        _snwprintf(msg, 1300, L"Не удалось запустить скрипт:\n%ls", path);
        msg[1299] = 0;
        MessageBoxW(NULL, msg, APPNAME, MB_ICONWARNING);
    }
}
static void RunFolder(const Entry *e) {
    wchar_t path[1100];
    ExpandEnvironmentStringsW(e->action, path, 1100);
    if (!path[0]) return;
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) {
        wchar_t msg[1300];
        _snwprintf(msg, 1300, L"Папка не найдена:\n%ls", path);
        msg[1299] = 0;
        MessageBoxW(NULL, msg, APPNAME, MB_ICONWARNING);
        return;
    }
    ShellExecuteW(NULL, L"open", path, NULL, NULL, SW_SHOWNORMAL);
}
static int IsScriptExt(const wchar_t *path) {
    static const wchar_t *const ext[] = { L".bat", L".cmd", L".vbs", L".vbe", L".js", L".jse", L".wsf", L".wsh", L".ps1", L".py", L".pyw", L".reg", NULL };
    const wchar_t *dot = wcsrchr(path, L'.');
    wchar_t e[16];
    if (!dot || wcslen(dot) > 8) return 0;
    cp(e, 16, dot);
    for (wchar_t *c = e; *c; c++) *c = (wchar_t)towlower(*c);
    return ExtIn(e, ext);
}
static void ShellRun(const wchar_t *progSpec, const wchar_t *file) { /* "программа [параметры]" + файл */
    wchar_t tok[1100], exe[1100], params[1700];
    const wchar_t *rest = L"";
    TokenSplit(progSpec, tok, 1100, &rest);
    ExpandEnvironmentStringsW(tok, exe, 1100);
    _snwprintf(params, 1700, L"%ls \"%ls\"", rest, file);
    params[1699] = 0;
    Trim(params);
    HINSTANCE r = ShellExecuteW(NULL, L"open", exe, params, NULL, SW_SHOWNORMAL);
    if ((INT_PTR)r <= 32) {
        wchar_t msg[1400];
        _snwprintf(msg, 1400, L"Не удалось запустить программу:\n%ls", exe);
        msg[1399] = 0;
        MessageBoxW(NULL, msg, APPNAME, MB_ICONWARNING);
    }
}
static void OpenInEditor(const wchar_t *path) { /* текстовый редактор из настроек, по умолчанию Блокнот */
    ShellRun(g_cfg.editor[0] ? g_cfg.editor : L"notepad.exe", path);
}
static void RunFileEntry(const Entry *e) {
    wchar_t path[1100];
    ExpandEnvironmentStringsW(e->action, path, 1100);
    if (!path[0]) return;
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) {
        wchar_t msg[1300];
        _snwprintf(msg, 1300, L"Файл не найден:\n%ls", path);
        msg[1299] = 0;
        MessageBoxW(NULL, msg, APPNAME, MB_ICONWARNING);
        return;
    }
    if (e->interp[0]) { ShellRun(e->interp, path); return; } /* открыть заданной программой */
    if (IsScriptExt(path)) { OpenInEditor(path); return; }   /* скрипты из «Файлов» открываем в редакторе, а не запускаем */
    ShellExecuteW(NULL, L"open", path, NULL, NULL, SW_SHOWNORMAL);
}
static void RunEntry(int sec, int idx, MonBtn *b) {
    Entry *e = &g_ent[sec][idx];
    if (sec == SEC_SCRIPT) RunScript(e);
    else if (sec == SEC_FOLDER) RunFolder(e);
    else if (sec == SEC_FILES) RunFileEntry(e);
    else RunSpec(e->action, e->args, b);
}

/* иконки пунктов меню: из файла/программы или указанные вручную; кэш битмапов с альфа-каналом */
#define ICACHE_MAX 1000
static struct { wchar_t key[900]; HBITMAP bmp; } g_ic[ICACHE_MAX];
static int g_nic;
static void FlushIconCache(void) {
    for (int i = 0; i < g_nic; i++) if (g_ic[i].bmp) DeleteObject(g_ic[i].bmp);
    g_nic = 0;
}
static HBITMAP BmpFromIcon(HICON ic) {
    int w = GetSystemMetrics(SM_CXSMICON), h = GetSystemMetrics(SM_CYSMICON);
    BITMAPINFO bi;
    void *bits = NULL;
    memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    HDC dc = GetDC(NULL);
    HBITMAP bm = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (bm && bits) {
        HDC mem = CreateCompatibleDC(dc);
        HGDIOBJ old = SelectObject(mem, bm);
        memset(bits, 0, (size_t)w * h * 4);
        DrawIconEx(mem, 0, 0, ic, w, h, 0, NULL, DI_NORMAL);
        SelectObject(mem, old);
        DeleteDC(mem);
        BYTE *p = (BYTE *)bits;
        int hasAlpha = 0;
        for (int i = 0; i < w * h; i++) if (p[i * 4 + 3]) { hasAlpha = 1; break; }
        if (!hasAlpha) /* значок без альфа-канала: непустые пиксели делаем непрозрачными */
            for (int i = 0; i < w * h; i++) if (p[i * 4] | p[i * 4 + 1] | p[i * 4 + 2]) p[i * 4 + 3] = 255;
    }
    ReleaseDC(NULL, dc);
    return bm;
}
static HICON ManualIcon(const wchar_t *spec) { /* "файл", "файл,номер" или "@app" */
    wchar_t raw[MAX_PATH * 2], p[MAX_PATH * 2 + 8];
    int idx = 0;
    HICON sm = NULL;
    cp(raw, WSZ(raw), spec);
    Trim(raw);
    if (!wcscmp(raw, L"@app"))
        return (HICON)LoadImageW(g_hinst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
    wchar_t *c = wcsrchr(raw, L',');
    if (c) {
        wchar_t *end = NULL;
        long v = wcstol(c + 1, &end, 10);
        if (end && end != c + 1 && !*end) { idx = (int)v; *c = 0; }
    }
    ExpandEnvironmentStringsW(raw, p, MAX_PATH * 2);
    if (p[1] != L':' && p[0] != L'\\') {
        wchar_t q[MAX_PATH * 2 + 8];
        _snwprintf(q, MAX_PATH * 2 + 8, L"%ls%ls", g_dir, p);
        q[MAX_PATH * 2 + 7] = 0;
        if (GetFileAttributesW(q) != INVALID_FILE_ATTRIBUTES) cp(p, MAX_PATH * 2, q);
    }
    if (ExtractIconExW(p, idx, NULL, &sm, 1) >= 1) return sm;
    return NULL;
}
static HICON ShellIcon(const wchar_t *path, int isDir) {
    SHFILEINFOW fi;
    DWORD fl = SHGFI_ICON | SHGFI_SMALLICON, attr = 0;
    wchar_t full[MAX_PATH * 2];
    int remote = (path[0] == L'\\' && path[1] == L'\\');
    memset(&fi, 0, sizeof fi);
    if (!remote && path[0] && path[1] == L':') {
        wchar_t root[4] = { path[0], L':', L'\\', 0 };
        if (GetDriveTypeW(root) == DRIVE_REMOTE) remote = 1;
    }
    cp(full, WSZ(full), path);
    if (!remote && !wcschr(path, L'\\') && !wcschr(path, L':')) { /* просто имя программы: ищем в PATH */
        wchar_t f2[MAX_PATH * 2];
        if (SearchPathW(NULL, path, NULL, MAX_PATH * 2, f2, NULL) || SearchPathW(NULL, path, L".exe", MAX_PATH * 2, f2, NULL)) cp(full, WSZ(full), f2);
        else { fl |= SHGFI_USEFILEATTRIBUTES; attr = FILE_ATTRIBUTE_NORMAL; }
    } else if (remote || GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) {
        fl |= SHGFI_USEFILEATTRIBUTES;
        attr = isDir ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
    }
    if (!SHGetFileInfoW(full, attr, &fi, sizeof fi, fl)) return NULL;
    return fi.hIcon;
}
/* проверка и приведение картинки от проводника к виду, который понимает меню (32 бита, альфа предумножена) */
static int FixShellBmp(HBITMAP bm) {
    DIBSECTION ds;
    int w = GetSystemMetrics(SM_CXSMICON), h = GetSystemMetrics(SM_CYSMICON);
    if (GetObject(bm, sizeof ds, &ds) < (int)sizeof(BITMAP)) return 0;
    if (ds.dsBm.bmWidth != w || abs(ds.dsBm.bmHeight) != h || ds.dsBm.bmBitsPixel != 32 || !ds.dsBm.bmBits) return 0;
    BYTE *p = (BYTE *)ds.dsBm.bmBits;
    int any = 0, straight = 0;
    for (int i = 0; i < w * h; i++) {
        BYTE b = p[i * 4], g = p[i * 4 + 1], r = p[i * 4 + 2], a = p[i * 4 + 3];
        if (a | r | g | b) any = 1;
        if (r > a || g > a || b > a) straight = 1;
    }
    if (!any) return 0;
    if (straight)
        for (int i = 0; i < w * h; i++) {
            BYTE a = p[i * 4 + 3];
            p[i * 4] = (BYTE)(p[i * 4] * a / 255);
            p[i * 4 + 1] = (BYTE)(p[i * 4 + 1] * a / 255);
            p[i * 4 + 2] = (BYTE)(p[i * 4 + 2] * a / 255);
        }
    return 1;
}
HRESULT WINAPI SHCreateItemFromParsingName(PCWSTR pszPath, IBindCtx *pbc, REFIID riid, void **ppv);
/* иконка так, как её рисует сам проводник (с альфа-каналом нужного размера) */
static HBITMAP ShellItemBmp(const wchar_t *path) {
    IShellItemImageFactory *f = NULL;
    HBITMAP bm = NULL;
    if (SUCCEEDED(SHCreateItemFromParsingName(path, NULL, &IID_IShellItemImageFactory, (void **)&f)) && f) {
        SIZE sz = { GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON) };
        if (FAILED(f->lpVtbl->GetImage(f, sz, SIIGBF_ICONONLY, &bm))) bm = NULL;
        f->lpVtbl->Release(f);
        if (bm && !FixShellBmp(bm)) { DeleteObject(bm); bm = NULL; } /* пустая или неподходящая картинка: запасной путь */
    }
    return bm;
}
static HBITMAP BmpForPath(const wchar_t *path, int isDir) {
    wchar_t full[MAX_PATH * 2];
    int remote = (path[0] == L'\\' && path[1] == L'\\');
    if (!remote && path[0] && path[1] == L':') {
        wchar_t root[4] = { path[0], L':', L'\\', 0 };
        if (GetDriveTypeW(root) == DRIVE_REMOTE) remote = 1;
    }
    cp(full, WSZ(full), path);
    if (!remote && !wcschr(path, L'\\') && !wcschr(path, L':')) { /* просто имя программы: ищем в PATH */
        if (!SearchPathW(NULL, path, NULL, MAX_PATH * 2, full, NULL) && !SearchPathW(NULL, path, L".exe", MAX_PATH * 2, full, NULL)) full[0] = 0;
    }
    if (!remote && full[0] && GetFileAttributesW(full) != INVALID_FILE_ATTRIBUTES) {
        HBITMAP b = ShellItemBmp(full);
        if (b) return b;
    }
    HICON ic = ShellIcon(path, isDir);
    HBITMAP bm = ic ? BmpFromIcon(ic) : NULL;
    if (ic) DestroyIcon(ic);
    return bm;
}
static HICON StockIconFor(const wchar_t *action) { /* значки для встроенных действий */
    SHSTOCKICONID id;
    if (!wcscmp(action, L"@lock")) id = SIID_LOCK;
    else if (!wcscmp(action, L"@desktop") || !wcscmp(action, L"@mondesk") || !wcscmp(action, L"@minall")) id = SIID_DESKTOPPC;
    else return NULL;
    SHSTOCKICONINFO si;
    memset(&si, 0, sizeof si);
    si.cbSize = sizeof si;
    if (SUCCEEDED(SHGetStockIconInfo(id, SHGSI_ICON | SHGSI_SMALLICON, &si))) return si.hIcon;
    return NULL;
}
static HBITMAP EntryBmp(const Entry *e, int sec) {
    if (e->type == 2) return NULL;
    wchar_t key[900];
    _snwprintf(key, 900, L"%d|%d|%ls|%ls", sec, e->type, e->icon, e->type == 1 ? L"" : e->action);
    key[899] = 0;
    for (int i = 0; i < g_nic; i++) if (!wcscmp(g_ic[i].key, key)) return g_ic[i].bmp;
    if (g_nic >= ICACHE_MAX) return NULL;
    HBITMAP bm = NULL;
    HICON ic = NULL;
    if (e->icon[0]) ic = ManualIcon(e->icon);
    if (!ic) {
        if (e->type == 1) {
            SHSTOCKICONINFO si;
            memset(&si, 0, sizeof si);
            si.cbSize = sizeof si;
            if (SUCCEEDED(SHGetStockIconInfo(SIID_FOLDER, SHGSI_ICON | SHGSI_SMALLICON, &si))) ic = si.hIcon;
        } else if (e->action[0]) {
            wchar_t p[1100];
            if (e->action[0] == L'@') {
                if (!wcscmp(e->action, L"@taskmgr")) {
                    ExpandEnvironmentStringsW(L"%SystemRoot%\\System32\\Taskmgr.exe", p, 1100);
                    bm = BmpForPath(p, 0);
                } else ic = StockIconFor(e->action);
            } else {
                ExpandEnvironmentStringsW(e->action, p, 1100);
                bm = BmpForPath(p, sec == SEC_FOLDER);
            }
        }
    }
    if (!bm && ic) bm = BmpFromIcon(ic);
    if (ic) DestroyIcon(ic);
    cp(g_ic[g_nic].key, 900, key);
    g_ic[g_nic].bmp = bm;
    g_nic++;
    return bm;
}
static void SetItemBmp(HMENU m, int pos, HBITMAP bm) {
    if (!bm) return;
    MENUITEMINFOW mi;
    memset(&mi, 0, sizeof mi);
    mi.cbSize = sizeof mi;
    mi.fMask = MIIM_BITMAP;
    mi.hbmpItem = bm;
    SetMenuItemInfoW(m, (UINT)pos, TRUE, &mi);
}

/* аннотации пунктов: всплывающая подсказка рядом с выделенным пунктом меню */
static HWND g_tip;
static wchar_t g_tipText[420];
static TOOLINFOW g_ti;
static void TipInit(void) {
    if (g_tip) return;
    g_tip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, NULL, WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP, 0, 0, 0, 0, g_ctl, NULL, g_hinst, NULL);
    if (!g_tip) return;
    memset(&g_ti, 0, sizeof g_ti);
    g_ti.cbSize = sizeof g_ti;
    g_ti.uFlags = TTF_TRACK | TTF_ABSOLUTE;
    g_ti.hwnd = g_ctl;
    g_ti.uId = 1;
    g_ti.lpszText = g_tipText;
    SendMessageW(g_tip, TTM_ADDTOOLW, 0, (LPARAM)&g_ti);
    SendMessageW(g_tip, TTM_SETMAXTIPWIDTH, 0, S(420));
}
static void TipHide(void) { if (g_tip) SendMessageW(g_tip, TTM_TRACKACTIVATE, FALSE, (LPARAM)&g_ti); }
static void TipShow(const wchar_t *text, int x, int y) {
    if (!g_tip) return;
    cp(g_tipText, WSZ(g_tipText), text);
    SendMessageW(g_tip, TTM_UPDATETIPTEXTW, 0, (LPARAM)&g_ti);
    SendMessageW(g_tip, TTM_TRACKPOSITION, 0, MAKELPARAM(x, y));
    SendMessageW(g_tip, TTM_TRACKACTIVATE, TRUE, (LPARAM)&g_ti);
}
static HMENU g_selMenu; /* меню, в котором сейчас выделен пункт */
static void OnMenuSelect(WPARAM w, LPARAM l) {
    UINT id = LOWORD(w), fl = HIWORD(w);
    HMENU hm = (HMENU)l;
    if (hm) g_selMenu = hm;
    if (!hm || fl == 0xFFFF || (fl & (MF_POPUP | MF_SEPARATOR)) || !id) { TipHide(); return; }
    if (id >= ID_ENT0 && id < ID_ENT0 + NSEC * MAXE) {
        int sec = (int)(id - ID_ENT0) / MAXE, idx = (int)(id - ID_ENT0) % MAXE;
        if (idx < g_cnt[sec] && g_ent[sec][idx].note[0]) {
            int n = GetMenuItemCount(hm);
            for (int p = 0; p < n; p++) {
                if (GetMenuItemID(hm, p) == id) {
                    RECT rc;
                    if (GetMenuItemRect(NULL, hm, (UINT)p, &rc)) { TipShow(g_ent[sec][idx].note, rc.right + 2, rc.top); return; }
                }
            }
        }
    }
    TipHide();
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
    DWORD st = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN;
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

/* ---------- трей и копирование данных активного окна ---------- */
#define WM_TRAY (WM_APP + 1)
static NOTIFYICONDATAW g_nid;
static int g_trayOn;
static UINT g_wmTaskbar;
static HICON g_trayIcon;

static void TrayTip(wchar_t *tip, int n) {
    _snwprintf(tip, (size_t)n, L"CornerBtn %ls — кнопка в углу: %ls", APPVER, g_cfg.show ? L"показана" : L"скрыта");
    tip[n - 1] = 0;
}
static int TrayAdd(void) {
    if (!g_trayIcon) g_trayIcon = (HICON)LoadImageW(g_hinst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
    memset(&g_nid, 0, sizeof g_nid);
    g_nid.cbSize = sizeof g_nid;
    g_nid.hWnd = g_ctl;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAY;
    g_nid.hIcon = g_trayIcon;
    TrayTip(g_nid.szTip, 128);
    g_trayOn = Shell_NotifyIconW(NIM_ADD, &g_nid) ? 1 : 0;
    return g_trayOn;
}
static void TrayRemove(void) {
    if (g_trayOn) { Shell_NotifyIconW(NIM_DELETE, &g_nid); g_trayOn = 0; }
}
static void TrayUpdate(void) {
    if (!g_trayOn) return;
    TrayTip(g_nid.szTip, 128);
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}
static void Balloon(const wchar_t *title, const wchar_t *msg) {
    if (!g_trayOn) return;
    NOTIFYICONDATAW n = g_nid;
    n.uFlags = NIF_INFO;
    n.dwInfoFlags = NIIF_NONE | NIIF_NOSOUND;
    cp(n.szInfoTitle, WSZ(n.szInfoTitle), title);
    cp(n.szInfo, WSZ(n.szInfo), msg);
    Shell_NotifyIconW(NIM_MODIFY, &n);
}
static void ToggleShow(void) {
    g_cfg.show = !g_cfg.show;
    PutI(L"Options", L"ShowButtons", g_cfg.show);
    for (int i = 0; i < g_nmb; i++) UpdateVis(g_mb[i]);
    TrayUpdate();
}
static const wchar_t *ShowLabel(void) {
    return g_cfg.show ? L"Кнопка в углу экрана: ПОКАЗАНА  (нажмите, чтобы скрыть)" : L"Кнопка в углу экрана: СКРЫТА  (нажмите, чтобы показать)";
}

typedef struct { DWORD pid; HWND found; } KidFind;
static BOOL CALLBACK EnumKid(HWND c, LPARAM l) {
    KidFind *k = (KidFind *)l;
    DWORD pid = 0;
    GetWindowThreadProcessId(c, &pid);
    if (pid && pid != k->pid) { k->found = c; return FALSE; }
    return TRUE;
}
/* для приложений из магазина окно принадлежит ApplicationFrameHost; ищем окно настоящей программы внутри */
static HWND RealWin(HWND w) {
    wchar_t cls[64];
    GetClassNameW(w, cls, 64);
    if (!wcscmp(cls, L"ApplicationFrameWindow")) {
        KidFind k = { 0, NULL };
        GetWindowThreadProcessId(w, &k.pid);
        EnumChildWindows(w, EnumKid, (LPARAM)&k);
        if (k.found) return k.found;
    }
    return w;
}
static int ExePath(HWND w, wchar_t *out, int n) {
    DWORD pid = 0;
    out[0] = 0;
    GetWindowThreadProcessId(w, &pid);
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return 0;
    DWORD sz = (DWORD)n;
    int ok = QueryFullProcessImageNameW(h, 0, out, &sz) ? 1 : 0;
    CloseHandle(h);
    return ok;
}
typedef struct { USHORT Length, MaximumLength; PWSTR Buffer; } USTR;
typedef LONG (NTAPI *NtQIPfn)(HANDLE, ULONG, PVOID, ULONG, PULONG);
static int ProcCmdLine(HWND w, wchar_t *out, int n) { /* командная строка процесса (Windows 8.1+) */
    DWORD pid = 0;
    int ok = 0;
    out[0] = 0;
    GetWindowThreadProcessId(w, &pid);
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return 0;
    static NtQIPfn f;
    if (!f) f = (NtQIPfn)(void *)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationProcess");
    if (f) {
        ULONG cap = 8192, len = 0;
        BYTE *buf = (BYTE *)malloc(cap);
        LONG st = -1;
        for (int k = 0; buf && k < 2; k++) {
            st = f(h, 60 /* ProcessCommandLineInformation */, buf, cap, &len);
            if (st == (LONG)0xC0000004 || st == (LONG)0xC0000023) {
                cap = len + 16;
                BYTE *nb = (BYTE *)realloc(buf, cap);
                if (!nb) { free(buf); buf = NULL; break; }
                buf = nb;
                continue;
            }
            break;
        }
        if (buf && st >= 0) {
            USTR *u = (USTR *)buf;
            if (u->Buffer && u->Length) {
                int cnt = u->Length / 2;
                if (cnt > n - 1) cnt = n - 1;
                memcpy(out, u->Buffer, (size_t)cnt * sizeof(wchar_t));
                out[cnt] = 0;
                ok = 1;
            }
        }
        free(buf);
    }
    CloseHandle(h);
    return ok;
}
static int CopyText(const wchar_t *s) {
    size_t bytes = (wcslen(s) + 1) * sizeof(wchar_t);
    HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!g) return 0;
    memcpy(GlobalLock(g), s, bytes);
    GlobalUnlock(g);
    if (!OpenClipboard(g_ctl)) { GlobalFree(g); return 0; }
    EmptyClipboard();
    SetClipboardData(CF_UNICODETEXT, g);
    CloseClipboard();
    return 1;
}
static void CopyInfo(int which) {
    HWND t = g_prevFg;
    if (!t || !IsWindow(t) || IsOurs(t) || BadClass(t)) {
        MessageBoxW(NULL, L"Нет подходящего активного окна.", APPNAME, MB_ICONINFORMATION);
        return;
    }
    HWND root = GetAncestor(t, GA_ROOT);
    if (!root) root = t;
    HWND rw = RealWin(root);
    static wchar_t buf[3000];
    wchar_t path[MAX_PATH * 2];
    int noargs = 0;
    buf[0] = 0;
    path[0] = 0;
    if (which == 1) {
        GetWindowTextW(root, buf, 3000);
        if (!buf[0]) { MessageBoxW(NULL, L"У этого окна нет заголовка.", APPNAME, MB_ICONINFORMATION); return; }
    } else {
        if (!ExePath(rw, path, MAX_PATH * 2)) {
            MessageBoxW(NULL, L"Не удалось определить программу этого окна (возможно, у неё повышенные права).", APPNAME, MB_ICONINFORMATION);
            return;
        }
        if (which == 2) {
            wchar_t *s = wcsrchr(path, L'\\');
            cp(buf, 3000, s ? s + 1 : path);
        } else if (which == 4) {
            _snwprintf(buf, 3000, L"\"%ls\"", path);
        } else if (which == 3) {
            static wchar_t cmd[2600], args[2600];
            const wchar_t *p;
            args[0] = 0;
            if (ProcCmdLine(rw, cmd, 2600)) {
                p = cmd;
                while (*p && iswspace(*p)) p++;
                if (*p == L'"') { p++; while (*p && *p != L'"') p++; if (*p) p++; }
                else while (*p && !iswspace(*p)) p++;
                while (*p && iswspace(*p)) p++;
                cp(args, 2600, p);
                Trim(args);
            } else noargs = 1;
            if (args[0]) _snwprintf(buf, 3000, L"\"%ls\" %ls", path, args);
            else _snwprintf(buf, 3000, L"\"%ls\"", path);
        } else if (which == 5) {
            PIDLIST_ABSOLUTE pidl = NULL;
            if (SUCCEEDED(SHParseDisplayName(path, NULL, &pidl, 0, NULL)) && pidl) {
                SHOpenFolderAndSelectItems(pidl, 0, NULL, 0);
                CoTaskMemFree(pidl);
            } else {
                wchar_t a[MAX_PATH * 2 + 16];
                _snwprintf(a, MAX_PATH * 2 + 16, L"/select,\"%ls\"", path);
                a[MAX_PATH * 2 + 15] = 0;
                ShellExecuteW(NULL, L"open", L"explorer.exe", a, NULL, SW_SHOWNORMAL);
            }
            return;
        }
    }
    buf[2999] = 0;
    if (CopyText(buf)) Balloon(noargs ? L"Скопировано без аргументов (не удалось их прочитать)" : L"Скопировано", buf);
    else MessageBoxW(NULL, L"Не удалось скопировать в буфер обмена.", APPNAME, MB_ICONWARNING);
}

static void LogStartup(void) {
    wchar_t bld[120];
    WinBuild(bld, 120);
    Log(L"Права процесса: %ls", IsElevated() ? L"АДМИНИСТРАТОР (перетаскивание из проводника блокируется Windows)" : L"обычный пользователь");
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
/* ---------- диалог записи (пункт / категория), перетаскивание файлов ---------- */
static void BaseName(const wchar_t *path, wchar_t *out, int n, int isDir) {
    wchar_t tmp[MAX_PATH * 2];
    cp(tmp, WSZ(tmp), path);
    size_t l = wcslen(tmp);
    while (l > 3 && (tmp[l - 1] == L'\\' || tmp[l - 1] == L'/')) tmp[--l] = 0;
    wchar_t *s = wcsrchr(tmp, L'\\');
    const wchar_t *b = s ? s + 1 : tmp;
    if (!*b) b = tmp;
    cp(out, (size_t)n, b);
    if (!isDir) {
        wchar_t *dot = wcsrchr(out, L'.');
        if (dot && dot != out) *dot = 0;
    }
}
static int IsDirPath(const wchar_t *p) {
    DWORD a = GetFileAttributesW(p);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}
static int Chk(HWND h) { return SendMessageW(h, BM_GETCHECK, 0, 0) == BST_CHECKED; }
static HWND Chkbox(HWND p, const wchar_t *t, int x, int y, int w, int id, int on) {
    HWND c = Mk(p, L"BUTTON", t, WS_TABSTOP | BS_AUTOCHECKBOX, 0, x, y, w, 22, id);
    SendMessageW(c, BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0);
    return c;
}
static int BrowseOpen(HWND owner, const wchar_t *filter, wchar_t *out, int n) {
    OPENFILENAMEW o;
    out[0] = 0;
    memset(&o, 0, sizeof o);
    o.lStructSize = sizeof o;
    o.hwndOwner = owner;
    o.lpstrFilter = filter;
    o.lpstrFile = out;
    o.nMaxFile = (DWORD)n;
    o.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_NODEREFERENCELINKS;
    return GetOpenFileNameW(&o) ? 1 : 0;
}
static int BrowseDir(HWND owner, wchar_t *out) {
    BROWSEINFOW bi;
    memset(&bi, 0, sizeof bi);
    bi.hwndOwner = owner;
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST p = SHBrowseForFolderW(&bi);
    if (!p) return 0;
    int ok = SHGetPathFromIDListW(p, out) ? 1 : 0;
    CoTaskMemFree(p);
    return ok;
}

enum { ED_NAME = 601, ED_ACT, ED_PATH, ED_ARGS, ED_INTERP, ED_ICON, ED_NOTE, ED_ADMIN, ED_HIDE, ED_BFILE, ED_BDIR, ED_BINTERP, ED_BICON };
typedef struct { Dlg d; int sec, type; Entry e; HWND eName, cb, ePath, eArgs, eInterp, eIcon, eNote, chAdmin, chHide; wchar_t lastAuto[128]; } EntDlg;

/* имя из пути подставляется, только если поле пустое или в нём наша прошлая подстановка;
   если пользователь ввёл имя сам, оно не трогается */
static void EntAutoName(EntDlg *a, const wchar_t *path) {
    wchar_t cur[128], nm[128], pp[MAX_PATH * 2];
    if (!a->eName || !path || !*path) return;
    GetWindowTextW(a->eName, cur, 128);
    Trim(cur);
    if (cur[0] && wcscmp(cur, a->lastAuto)) return;
    cp(pp, WSZ(pp), path);
    Trim(pp);
    size_t l = wcslen(pp);
    if (l > 1 && pp[0] == L'"') { if (pp[l - 1] == L'"') pp[l - 1] = 0; memmove(pp, pp + 1, (wcslen(pp)) * sizeof(wchar_t)); }
    BaseName(pp, nm, 128, IsDirPath(pp));
    if (!nm[0]) return;
    SetWindowTextW(a->eName, nm);
    cp(a->lastAuto, WSZ(a->lastAuto), nm);
}
/* OLE-приёмник файлов (IDropTarget): надёжный способ принимать перетаскивание из проводника */
enum { DT_TREE = 1, DT_EDIT = 2, DT_PAGETREE = 3 };
typedef struct { IDropTargetVtbl *lpVtbl; LONG ref; HWND hwnd; int kind; void *ctx; int ok; } DT;
static void TreeDropEx(HWND tree, HDROP hd, POINT pt);
static void EditDropApply(HWND h, EntDlg *a, HDROP hd) {
    wchar_t f[MAX_PATH * 2];
    if (!DragQueryFileW(hd, 0, f, MAX_PATH * 2)) return;
    if (a && a->sec == SEC_FOLDER && h == a->ePath && !IsDirPath(f)) { /* для папок: вместо файла берём его папку */
        wchar_t *sl = wcsrchr(f, L'\\');
        if (sl) { if (sl - f <= 2) sl[1] = 0; else *sl = 0; }
    }
    SetWindowTextW(h, f);
    if (a && h == a->ePath) EntAutoName(a, f);
}
static int DtHasFiles(IDataObject *o) {
    FORMATETC fe = { CF_HDROP, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    return o && o->lpVtbl->QueryGetData(o, &fe) == S_OK;
}
static HRESULT WINAPI Dt_QI(IDropTarget *t, REFIID r, void **o) {
    if (IsEqualIID(r, &IID_IUnknown) || IsEqualIID(r, &IID_IDropTarget)) { *o = t; ((DT *)t)->ref++; return S_OK; }
    *o = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI Dt_AddRef(IDropTarget *t) { return (ULONG)++((DT *)t)->ref; }
static ULONG WINAPI Dt_Release(IDropTarget *t) {
    DT *d = (DT *)t;
    LONG r = --d->ref;
    if (!r) free(d);
    return (ULONG)r;
}
static void DtHilite(DT *d, POINTL ptl, int on) {
    if (d->kind != DT_TREE) return;
    HTREEITEM hit = NULL;
    if (on) {
        TVHITTESTINFO ht;
        memset(&ht, 0, sizeof ht);
        ht.pt.x = ptl.x;
        ht.pt.y = ptl.y;
        ScreenToClient(d->hwnd, &ht.pt);
        hit = TreeView_HitTest(d->hwnd, &ht);
    }
    TreeView_SelectDropTarget(d->hwnd, hit);
}
static HRESULT WINAPI Dt_Enter(IDropTarget *t, IDataObject *o, DWORD ks, POINTL pt, DWORD *eff) {
    DT *d = (DT *)t;
    (void)ks;
    d->ok = DtHasFiles(o);
    *eff = d->ok ? DROPEFFECT_COPY : DROPEFFECT_NONE;
    DtHilite(d, pt, d->ok);
    return S_OK;
}
static HRESULT WINAPI Dt_Over(IDropTarget *t, DWORD ks, POINTL pt, DWORD *eff) {
    DT *d = (DT *)t;
    (void)ks;
    *eff = d->ok ? DROPEFFECT_COPY : DROPEFFECT_NONE;
    DtHilite(d, pt, d->ok);
    return S_OK;
}
static HRESULT WINAPI Dt_Leave(IDropTarget *t) {
    POINTL z = { 0, 0 };
    DtHilite((DT *)t, z, 0);
    return S_OK;
}
static HRESULT WINAPI Dt_Drop(IDropTarget *t, IDataObject *o, DWORD ks, POINTL ptl, DWORD *eff) {
    DT *d = (DT *)t;
    FORMATETC fe = { CF_HDROP, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    STGMEDIUM stg;
    (void)ks;
    DtHilite(d, ptl, 0);
    *eff = DROPEFFECT_NONE;
    if (!d->ok || !o) return S_OK;
    memset(&stg, 0, sizeof stg);
    if (SUCCEEDED(o->lpVtbl->GetData(o, &fe, &stg)) && stg.tymed == TYMED_HGLOBAL) {
        HDROP hd = (HDROP)stg.hGlobal;
        POINT pt = { ptl.x, ptl.y };
        Log(L"drop: kind=%d файлов=%u", d->kind, DragQueryFileW(hd, 0xFFFFFFFF, NULL, 0));
        if (d->kind == DT_TREE) { ScreenToClient(d->hwnd, &pt); TreeDropEx(d->hwnd, hd, pt); }
        else if (d->kind == DT_PAGETREE) { POINT far_ = { -1, -1 }; TreeDropEx((HWND)d->ctx, hd, far_); }
        else EditDropApply(d->hwnd, (EntDlg *)d->ctx, hd);
        ReleaseStgMedium(&stg);
        *eff = DROPEFFECT_COPY;
    }
    return S_OK;
}
static IDropTargetVtbl g_dtv = { Dt_QI, Dt_AddRef, Dt_Release, Dt_Enter, Dt_Over, Dt_Leave, Dt_Drop };
static int RegDrop(HWND w, int kind, void *ctx) { /* 1 = OLE, 0 = запасной вариант WM_DROPFILES */
    ChangeWindowMessageFilterEx(w, WM_DROPFILES, MSGFLT_ALLOW, NULL);
    ChangeWindowMessageFilterEx(w, 0x0049 /* WM_COPYGLOBALDATA */, MSGFLT_ALLOW, NULL);
    DT *d = (DT *)calloc(1, sizeof(DT));
    if (!d) { DragAcceptFiles(w, TRUE); return 0; }
    d->lpVtbl = &g_dtv;
    d->ref = 1;
    d->hwnd = w;
    d->kind = kind;
    d->ctx = ctx;
    HRESULT hr = RegisterDragDrop(w, (IDropTarget *)d);
    Log(L"RegisterDragDrop hwnd=%p kind=%d hr=0x%08lX", (void *)w, kind, (unsigned long)hr);
    Dt_Release((IDropTarget *)d); /* своя ссылка не нужна: регистрация держит свою */
    if (FAILED(hr)) { DragAcceptFiles(w, TRUE); return 0; }
    return 1;
}
static LRESULT CALLBACK DropEditSub(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR ref) {
    if (m == WM_DROPFILES) { /* запасной путь, если OLE недоступен */
        EditDropApply(h, (EntDlg *)ref, (HDROP)w);
        DragFinish((HDROP)w);
        return 0;
    }
    if (m == WM_DESTROY) RevokeDragDrop(h);
    if (m == WM_NCDESTROY) RemoveWindowSubclass(h, DropEditSub, id);
    return DefSubclassProc(h, m, w, l);
}
static void EntEnable(EntDlg *a) {
    if (a->sec != SEC_PROG || a->type != 0) return;
    BOOL open = (int)SendMessageW(a->cb, CB_GETCURSEL, 0, 0) == NBI;
    EnableWindow(a->ePath, open);
    EnableWindow(a->eArgs, open);
    EnableWindow(GetDlgItem(a->d.h, ED_BFILE), open);
    EnableWindow(GetDlgItem(a->d.h, ED_BDIR), open);
}
static void EntCmd(Dlg *d, int id, int code) {
    EntDlg *a = (EntDlg *)d;
    wchar_t f[MAX_PATH];
    switch (id) {
    case ED_ACT:
        if (code == CBN_SELCHANGE) EntEnable(a);
        break;
    case ED_PATH:
        if (code == EN_KILLFOCUS && a->ePath) { /* ушли из поля пути: подставить имя, если оно ещё не введено */
            wchar_t pp[520];
            GetWindowTextW(a->ePath, pp, 520);
            Trim(pp);
            if (pp[0]) EntAutoName(a, pp);
        }
        break;
    case ED_BFILE: {
        const wchar_t *flt = a->sec == SEC_FILES ? L"Все файлы\0*.*\0" : a->sec == SEC_SCRIPT
            ? L"Скрипты\0*.bat;*.cmd;*.vbs;*.vbe;*.js;*.jse;*.wsf;*.wsh;*.ps1;*.py;*.pyw\0Все файлы\0*.*\0"
            : L"Программы и ярлыки\0*.exe;*.com;*.bat;*.cmd;*.lnk\0Все файлы\0*.*\0";
        if (BrowseOpen(d->h, flt, f, MAX_PATH)) { SetWindowTextW(a->ePath, f); EntAutoName(a, f); }
        break;
    }
    case ED_BDIR:
        if (BrowseDir(d->h, f)) { SetWindowTextW(a->ePath, f); EntAutoName(a, f); }
        break;
    case ED_BINTERP:
        if (BrowseOpen(d->h, L"Программы\0*.exe;*.com;*.bat;*.cmd\0Все файлы\0*.*\0", f, MAX_PATH)) {
            wchar_t q[MAX_PATH + 4];
            if (wcschr(f, L' ')) _snwprintf(q, MAX_PATH + 4, L"\"%ls\"", f); else cp(q, WSZ(q), f);
            q[MAX_PATH + 3] = 0;
            SetWindowTextW(a->eInterp, q);
        }
        break;
    case ED_BICON:
        if (BrowseOpen(d->h, L"Иконки (*.ico;*.exe;*.dll)\0*.ico;*.exe;*.dll\0Все файлы\0*.*\0", f, MAX_PATH)) SetWindowTextW(a->eIcon, f);
        break;
    case IDOK: {
        Entry *e = &a->e;
        GetWindowTextW(a->eName, e->name, 128);
        Trim(e->name);
        if (!e->name[0] && a->type == 0 && a->ePath) { /* имя не введено: берём из пути */
            wchar_t pp[520];
            GetWindowTextW(a->ePath, pp, 520);
            Trim(pp);
            if (pp[0]) { EntAutoName(a, pp); GetWindowTextW(a->eName, e->name, 128); Trim(e->name); }
        }
        if (!e->name[0]) { MessageBoxW(d->h, L"Введите название.", APPNAME, MB_ICONINFORMATION); return; }
        GetWindowTextW(a->eIcon, e->icon, 260);
        Trim(e->icon);
        if (a->type == 0) {
            GetWindowTextW(a->eNote, e->note, 300);
            Trim(e->note);
            if (a->sec == SEC_PROG) {
                int s = (int)SendMessageW(a->cb, CB_GETCURSEL, 0, 0);
                if (s >= 0 && s < NBI) { cp(e->action, WSZ(e->action), BI[s].id); e->args[0] = 0; }
                else {
                    GetWindowTextW(a->ePath, e->action, 520);
                    Trim(e->action);
                    GetWindowTextW(a->eArgs, e->args, 260);
                    if (!e->action[0]) { MessageBoxW(d->h, L"Введите путь, ссылку или программу.", APPNAME, MB_ICONINFORMATION); return; }
                }
            } else {
                GetWindowTextW(a->ePath, e->action, 520);
                Trim(e->action);
                if (!e->action[0]) { MessageBoxW(d->h, a->sec == SEC_SCRIPT ? L"Укажите путь к скрипту." : (a->sec == SEC_FILES ? L"Укажите путь к файлу." : L"Укажите путь к папке."), APPNAME, MB_ICONINFORMATION); return; }
                if (a->sec == SEC_SCRIPT) {
                    GetWindowTextW(a->eArgs, e->args, 260);
                    GetWindowTextW(a->eInterp, e->interp, 260);
                    Trim(e->interp);
                    e->flags = (Chk(a->chAdmin) ? 1 : 0) | (Chk(a->chHide) ? 2 : 0);
                } else if (a->sec == SEC_FILES) {
                    GetWindowTextW(a->eInterp, e->interp, 260);
                    Trim(e->interp);
                }
            }
        }
        d->result = 1;
        d->done = 1;
        break;
    }
    case IDCANCEL: d->done = 1; break;
    }
}
/* 1 = OK (значения записаны в *e), 0 = отмена */
static int EditEntryDlg(int sec, int type, Entry *e, const wchar_t *title, HWND owner) {
    EntDlg a;
    memset(&a, 0, sizeof a);
    a.sec = sec;
    a.type = type;
    a.e = *e;
    int rows = (type == 1) ? 2 : (sec == SEC_FOLDER ? 4 : (sec == SEC_FILES ? 5 : 6));
    int ch = 12 + rows * 56 + ((sec == SEC_SCRIPT && type == 0) ? 54 : 0) + 44;
    NewDlg(&a.d, title, 520, ch);
    a.d.oncmd = EntCmd;
    HWND h = a.d.h;
    int y = 12;
    Lbl(h, L"Название (как будет в меню):", 12, y, 496, 18);
    a.eName = Edt(h, 12, y + 20, 496, 24, ED_NAME);
    SetWindowTextW(a.eName, e->name);
    y += 56;
    if (type == 0) {
        if (sec == SEC_PROG) {
            Lbl(h, L"Действие:", 12, y, 496, 18);
            a.cb = Mk(h, L"COMBOBOX", L"", WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, 0, 12, y + 20, 496, 260, ED_ACT);
            for (int i = 0; i < NBI; i++) SendMessageW(a.cb, CB_ADDSTRING, 0, (LPARAM)BI[i].label);
            SendMessageW(a.cb, CB_ADDSTRING, 0, (LPARAM)L"Открыть файл / папку / ссылку / программу");
            y += 56;
        }
        Lbl(h, sec == SEC_PROG ? L"Путь, ссылка или программа (можно перетащить файл сюда):"
                : (sec == SEC_SCRIPT ? L"Путь к скрипту (.bat, .cmd, .vbs, .js, .wsf, .ps1, .py … можно перетащить сюда):"
                : (sec == SEC_FILES ? L"Путь к файлу (можно перетащить файл сюда):"
                                     : L"Путь к папке (можно перетащить папку сюда):")), 12, y, 496, 18);
        a.ePath = Edt(h, 12, y + 20, 340, 24, ED_PATH);
        if (sec == SEC_FOLDER) Btn(h, L"Выбрать папку…", 360, y + 20, 148, 24, ED_BDIR, 0);
        else if (sec == SEC_SCRIPT || sec == SEC_FILES) Btn(h, L"Выбрать файл…", 360, y + 20, 148, 24, ED_BFILE, 0);
        else { Btn(h, L"Файл…", 360, y + 20, 70, 24, ED_BFILE, 0); Btn(h, L"Папка…", 436, y + 20, 72, 24, ED_BDIR, 0); }
        y += 56;
        if (sec != SEC_FOLDER && sec != SEC_FILES) {
            Lbl(h, L"Аргументы (необязательно):", 12, y, 496, 18);
            a.eArgs = Edt(h, 12, y + 20, 496, 24, ED_ARGS);
            y += 56;
        }
        if (sec == SEC_SCRIPT || sec == SEC_FILES) {
            Lbl(h, sec == SEC_FILES ? L"Открыть программой (пусто — скрипты в текстовом редакторе, остальное как обычно):"
                                    : L"Интерпретатор (пусто — выбирается сам по расширению; можно «python.exe -u»):", 12, y, 496, 18);
            a.eInterp = Edt(h, 12, y + 20, 340, 24, ED_INTERP);
            Btn(h, L"Обзор…", 360, y + 20, 148, 24, ED_BINTERP, 0);
            y += 56;
        }
    }
    Lbl(h, type == 1 ? L"Иконка (необязательно; пусто — обычная папка):" : L"Иконка (необязательно; пусто — берётся из самого файла/программы):", 12, y, 496, 18);
    a.eIcon = Edt(h, 12, y + 20, 340, 24, ED_ICON);
    Btn(h, L"Обзор…", 360, y + 20, 148, 24, ED_BICON, 0);
    y += 56;
    if (type == 0) {
        Lbl(h, L"Аннотация (подсказка при наведении в меню; необязательно):", 12, y, 496, 18);
        a.eNote = Edt(h, 12, y + 20, 496, 24, ED_NOTE);
        SetWindowTextW(a.eNote, e->note);
        y += 56;
    }
    if (sec == SEC_SCRIPT && type == 0) {
        a.chAdmin = Chkbox(h, L"Запускать от имени администратора", 12, y, 496, ED_ADMIN, e->flags & 1);
        a.chHide = Chkbox(h, L"Скрытое окно (для скриптов без интерфейса)", 12, y + 26, 496, ED_HIDE, e->flags & 2);
        y += 54;
    }
    SetWindowTextW(a.eIcon, e->icon);
    if (type == 0) {
        if (sec == SEC_PROG) {
            int sel = NBI;
            for (int i = 0; i < NBI; i++) if (!wcscmp(e->action, BI[i].id)) sel = i;
            SendMessageW(a.cb, CB_SETCURSEL, sel, 0);
            if (sel == NBI) { SetWindowTextW(a.ePath, e->action); SetWindowTextW(a.eArgs, e->args); }
            EntEnable(&a);
        } else {
            SetWindowTextW(a.ePath, e->action);
            if (a.eArgs) SetWindowTextW(a.eArgs, e->args);
            if (a.eInterp) SetWindowTextW(a.eInterp, e->interp);
        }
    }
    HWND dropTargets[3] = { a.ePath, a.eIcon, a.eInterp };
    for (int i = 0; i < 3; i++) if (dropTargets[i]) { SetWindowSubclass(dropTargets[i], DropEditSub, 1, (DWORD_PTR)&a); RegDrop(dropTargets[i], DT_EDIT, &a); }
    Btn(h, L"OK", 328, y, 85, 28, IDOK, 1);
    Btn(h, L"Отмена", 423, y, 85, 28, IDCANCEL, 0);
    if (!RunModal(&a.d, owner)) return 0;
    *e = a.e;
    return 1;
}

/* ---------- редактор дерева записей (вкладки Программы / Скрипты / Папки) ---------- */
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
static void TreeCmd(HWND owner, int id) {
    static const wchar_t *const newTitle[NSEC] = { L"Новая программа", L"Новый скрипт или команда", L"Новая папка быстрого доступа", L"Новый файл" };
    int i = SelIdx(), ns = -1, chg = 0;
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
    case 414: /* в категорию выше */
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
        if (EditEntryDlg(g_cur, 0, &e, newTitle[g_cur], owner)) {
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
        cp(e.name, WSZ(e.name), L"Новая категория");
        e.type = 1;
        if (EditEntryDlg(g_cur, 1, &e, L"Новая категория", owner)) {
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
            if (e->type == 0) { if (EditEntryDlg(g_cur, 0, e, L"Изменить пункт", owner)) { ns = i; chg = 1; } }
            else if (e->type == 1) { if (EditEntryDlg(g_cur, 1, e, L"Изменить категорию", owner)) { ns = i; chg = 1; } }
        }
        break;
    case 419: /* удалить */
        if (i >= 0) {
            int e = EndOf(i);
            if (e - i > 1 && MessageBoxW(owner, L"Удалить категорию со всем содержимым?", APPNAME, MB_YESNO | MB_ICONQUESTION) != IDYES) break;
            memmove(&g_e[i], &g_e[e], (size_t)(g_n - e) * sizeof(Entry));
            g_n -= e - i;
            ns = i < g_n ? i : g_n - 1;
            chg = 1;
        }
        break;
    }
    if (chg) { Normalize(); Rebuild(ns); }
}

/* перетаскивание файлов/папок из проводника прямо в дерево */
static void TreeDropEx(HWND tree, HDROP hd, POINT pt) {
    int sec = -1;
    for (int s = 0; s < NSEC; s++) if (g_trees[s] == tree) sec = s;
    if (sec < 0) return;
    g_cur = sec;
    TVHITTESTINFO ht;
    memset(&ht, 0, sizeof ht);
    ht.pt = pt;
    HTREEITEM hit = TreeView_HitTest(tree, &ht);
    int pos = g_n, depth = 0, last = -1;
    if (hit) {
        TVITEMW it;
        memset(&it, 0, sizeof it);
        it.mask = TVIF_PARAM;
        it.hItem = hit;
        TreeView_GetItem(tree, &it);
        int idx = (int)it.lParam;
        if (idx >= 0 && idx < g_n) {
            if (g_e[idx].type == 1) { depth = g_e[idx].depth + 1; pos = EndOf(idx); }
            else { depth = g_e[idx].depth; pos = idx + 1; }
        }
    }
    UINT n = DragQueryFileW(hd, 0xFFFFFFFF, NULL, 0);
    for (UINT k = 0; k < n && g_n < MAXE; k++) {
        wchar_t f[MAX_PATH * 2];
        if (!DragQueryFileW(hd, k, f, MAX_PATH * 2)) continue;
        int isDir = IsDirPath(f);
        if (sec == SEC_FOLDER && !isDir) { /* файл: добавляем его папку */
            wchar_t *sl = wcsrchr(f, L'\\');
            if (sl) { if (sl - f <= 2) sl[1] = 0; else *sl = 0; }
            isDir = 1;
        }
        Entry e;
        memset(&e, 0, sizeof e);
        e.type = 0;
        e.depth = depth;
        BaseName(f, e.name, 128, isDir);
        cp(e.action, WSZ(e.action), f);
        InsertAt(pos, &e);
        last = pos;
        pos++;
    }
    Normalize();
    Rebuild(last);
}
static void TreeDrop(HWND tree, HDROP hd) { /* запасной путь WM_DROPFILES */
    POINT pt;
    DragQueryPoint(hd, &pt);
    TreeDropEx(tree, hd, pt);
    DragFinish(hd);
}
static LRESULT CALLBACK TreeSub(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR ref) {
    (void)ref;
    if (m == WM_DROPFILES) { TreeDrop(h, (HDROP)w); return 0; }
    if (m == WM_DESTROY) RevokeDragDrop(h);
    if (m == WM_NCDESTROY) RemoveWindowSubclass(h, TreeSub, id);
    return DefSubclassProc(h, m, w, l);
}

/* ---------- окно настроек со вкладками ---------- */
enum { TAB_PROG = 0, TAB_SCRIPT, TAB_FOLDER, TAB_FILES, TAB_APP, TAB_ACT, TAB_MON, TAB_PIN, TAB_EXCL, TAB_GEN, NTABS };
typedef struct {
    Dlg d;
    HWND tab, page[NTABS];
    int cur, nm, noRestore;
    HWND eSize, eOp, eHov, eCol, cbShape, eIcon;
    Act a[3];
    HWND stAct[3];
    HWND chShow, chMon[MAXMON], cbCorner[MAXMON];
    int mdx[MAXMON], mdy[MAXMON];
    HWND chPinTop, chPinAll, chRaise, chKeep, chLocal;
    HWND eExcl, eRemote;
    HWND chAuto, chDebug, eEditor;
} SetDlg;
static Entry g_bak[NSEC][MAXE];
static int g_bakn[NSEC];

static void ActDesc(const Act *a, wchar_t *out, int n) {
    for (int i = 0; i < NBI; i++) if (!wcscmp(a->action, BI[i].id)) { cp(out, (size_t)n, BI[i].label); return; }
    if (!a->action[0]) { cp(out, (size_t)n, L"(нет действия)"); return; }
    if (a->args[0]) _snwprintf(out, (size_t)n, L"Открыть: %ls  %ls", a->action, a->args);
    else _snwprintf(out, (size_t)n, L"Открыть: %ls", a->action);
    out[n - 1] = 0;
}
static void LinesToEdit(HWND ed, wchar_t (*arr)[64], int n) {
    static wchar_t buf[MAXLIST * 66 + 80];
    buf[0] = 0;
    for (int i = 0; i < n; i++) { wcscat(buf, arr[i]); wcscat(buf, L"\r\n"); }
    SetWindowTextW(ed, buf);
}
static void EditToLines(HWND ed, wchar_t (*arr)[64], int *n) {
    static wchar_t buf[MAXLIST * 66 + 80];
    GetWindowTextW(ed, buf, (int)WSZ(buf));
    int k = 0;
    wchar_t *p = buf;
    while (*p && k < MAXLIST) {
        wchar_t *s = p;
        while (*p && *p != L'\r' && *p != L'\n') p++;
        wchar_t sv = *p;
        *p = 0;
        Trim(s);
        if (*s) cp(arr[k++], 64, s);
        if (sv) p++; else break;
    }
    *n = k;
}
static void AddExeToEdit(HWND owner, HWND ed) {
    wchar_t f[MAX_PATH];
    if (!BrowseOpen(owner, L"Программы (*.exe)\0*.exe\0Все файлы\0*.*\0", f, MAX_PATH)) return;
    wchar_t *s = wcsrchr(f, L'\\');
    static wchar_t buf[MAXLIST * 66 + 80];
    GetWindowTextW(ed, buf, (int)WSZ(buf) - 70);
    size_t n = wcslen(buf);
    if (n && buf[n - 1] != L'\n') wcscat(buf, L"\r\n");
    wcsncat(buf, s ? s + 1 : f, 63);
    wcscat(buf, L"\r\n");
    SetWindowTextW(ed, buf);
}

static LRESULT CALLBACK PageProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_COMMAND:
    case WM_NOTIFY:
        return SendMessageW(GetParent(h), m, w, l);
    case WM_CTLCOLORSTATIC:
        SetBkColor((HDC)w, GetSysColor(COLOR_WINDOW));
        return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
    }
    return DefWindowProcW(h, m, w, l);
}

static const wchar_t *const g_tabName[NTABS] = { L"Программы", L"Скрипты и команды", L"Папки", L"Файлы", L"Внешний вид", L"Действия кнопки", L"Мониторы", L"Закрепление окон", L"Исключения", L"Общее" };
static const wchar_t *const g_actTitle[3] = { L"Левый клик", L"Двойной левый клик", L"Средняя кнопка (колёсико)" };
static const wchar_t *const g_cornerName[4] = { L"Левый верхний угол", L"Правый верхний угол", L"Левый нижний угол", L"Правый нижний угол" };

static void PgTree(SetDlg *s, int sec, HWND pg, int PW, int PH) {
    (void)s;
    static const wchar_t *const addText[NSEC] = { L"Добавить программу…", L"Добавить скрипт…", L"Добавить папку…", L"Добавить файл…" };
    static const wchar_t *const hint[NSEC] = {
        L"Перетащите сюда программы, ярлыки и любые файлы из проводника: пункты создадутся сами. Иконка берётся из файла. Двойной щелчок — изменить.",
        L"Перетащите сюда скрипты (.bat, .cmd, .vbs, .js, .wsf, .ps1, .py): пункты создадутся сами. Интерпретатор выбирается по расширению или задаётся вручную.",
        L"Перетащите сюда папки из проводника. Категории (например «Фото») создаются кнопкой «Добавить категорию», папки кладутся внутрь категории.",
        L"Перетащите сюда любые файлы из проводника для быстрого открытия. Скрипты из этого раздела открываются в текстовом редакторе (задаётся на вкладке «Общее»), остальные файлы — как обычно."
    };
    HWND tr = Mk(pg, L"SysTreeView32", L"", WS_TABSTOP | TVS_HASLINES | TVS_HASBUTTONS | TVS_LINESATROOT | TVS_SHOWSELALWAYS, WS_EX_CLIENTEDGE,
                 8, 8, PW - 196, PH - 62, 701 + sec);
    g_trees[sec] = tr;
    SetWindowSubclass(tr, TreeSub, 1, 0);
    RegDrop(tr, DT_TREE, NULL);
    RegDrop(pg, DT_PAGETREE, tr); /* бросить можно и на пустое место вкладки: пункт добавится в конец */
    int x = PW - 180, y = 8;
    Btn(pg, L"↑  Вверх", x, y, 172, 28, 411, 0); y += 32;
    Btn(pg, L"↓  Вниз", x, y, 172, 28, 412, 0); y += 32;
    Btn(pg, L"←  Из категории", x, y, 172, 28, 413, 0); y += 32;
    Btn(pg, L"→  В категорию выше", x, y, 172, 28, 414, 0); y += 40;
    Btn(pg, addText[sec], x, y, 172, 28, 415, 0); y += 32;
    Btn(pg, L"Добавить категорию…", x, y, 172, 28, 416, 0); y += 32;
    Btn(pg, L"Добавить разделитель", x, y, 172, 28, 417, 0); y += 40;
    Btn(pg, L"Изменить…", x, y, 172, 28, 418, 0); y += 32;
    Btn(pg, L"Удалить", x, y, 172, 28, 419, 0);
    Lbl(pg, IsElevated() ? L"ВНИМАНИЕ: CornerBtn запущен от имени администратора, поэтому Windows блокирует перетаскивание из обычного проводника. Перезапустите программу без повышения прав."
                         : hint[sec], 8, PH - 48, PW - 16, 44);
    g_cur = sec;
    Rebuild(-1);
}
static void PgApp(SetDlg *s, HWND pg) {
    wchar_t b[MAX_PATH];
    int y = 14;
    Lbl(pg, L"Размер, пиксели (2–200):", 12, y + 3, 250, 20);
    s->eSize = Edt(pg, 270, y, 80, 24, 201);
    _snwprintf(b, 32, L"%d", g_cfg.size); SetWindowTextW(s->eSize, b);
    y += 32;
    Lbl(pg, L"Непрозрачность, % (5–100):", 12, y + 3, 250, 20);
    s->eOp = Edt(pg, 270, y, 80, 24, 202);
    _snwprintf(b, 32, L"%d", g_cfg.opacity); SetWindowTextW(s->eOp, b);
    y += 32;
    Lbl(pg, L"Непрозрачность при наведении, %:", 12, y + 3, 250, 20);
    s->eHov = Edt(pg, 270, y, 80, 24, 203);
    _snwprintf(b, 32, L"%d", g_cfg.hover); SetWindowTextW(s->eHov, b);
    y += 32;
    Lbl(pg, L"Цвет (RRGGBB):", 12, y + 3, 250, 20);
    s->eCol = Edt(pg, 270, y, 80, 24, 204);
    _snwprintf(b, 32, L"%02X%02X%02X", GetRValue(g_cfg.color), GetGValue(g_cfg.color), GetBValue(g_cfg.color));
    SetWindowTextW(s->eCol, b);
    Btn(pg, L"Выбрать…", 360, y, 100, 24, 205, 0);
    y += 32;
    Lbl(pg, L"Форма:", 12, y + 3, 250, 20);
    s->cbShape = Mk(pg, L"COMBOBOX", L"", WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, 0, 270, y, 120, 120, 208);
    SendMessageW(s->cbShape, CB_ADDSTRING, 0, (LPARAM)L"Квадрат");
    SendMessageW(s->cbShape, CB_ADDSTRING, 0, (LPARAM)L"Круг");
    SendMessageW(s->cbShape, CB_SETCURSEL, g_cfg.shape, 0);
    y += 40;
    Lbl(pg, L"Иконка на кнопке: файл .ico/.exe/.dll или @app (значок программы). Пусто — без иконки. Рисуется при размере кнопки от 10.", 12, y, 640, 34);
    y += 38;
    s->eIcon = Edt(pg, 12, y, 340, 24, 206);
    SetWindowTextW(s->eIcon, g_cfg.icon);
    Btn(pg, L"Значок программы", 360, y, 130, 24, 209, 0);
    Btn(pg, L"Обзор…", 498, y, 90, 24, 207, 0);
}
static void ActText(SetDlg *s, int k) {
    wchar_t b[1200];
    ActDesc(&s->a[k], b, 1200);
    SetWindowTextW(s->stAct[k], b);
}
static void PgAct(SetDlg *s, HWND pg, int PW) {
    s->a[0] = g_click; s->a[1] = g_dbl; s->a[2] = g_mid;
    for (int k = 0; k < 3; k++) {
        int y = 16 + k * 76;
        Lbl(pg, g_actTitle[k], 12, y, 400, 18);
        s->stAct[k] = Mk(pg, L"STATIC", L"", SS_LEFT, 0, 28, y + 22, PW - 190, 36, -1);
        Btn(pg, L"Изменить…", PW - 150, y + 20, 130, 28, 301 + k, 0);
        ActText(s, k);
    }
}
static void PgMon(SetDlg *s, HWND pg) {
    s->chShow = Chkbox(pg, L"Показывать кнопки в углах экранов (то же, что переключатель в трее)", 12, 12, 700, 311, g_cfg.show);
    s->nm = g_nmb;
    for (int i = 0; i < g_nmb && i < MAXMON; i++) {
        MonBtn *q = g_mb[i];
        int y = 52 + i * 76;
        wchar_t t[240];
        _snwprintf(t, 240, L"Монитор %d%ls — %d×%d   (%ls)", i + 1, q->primary ? L", основной" : L"", (int)(q->rc.right - q->rc.left), (int)(q->rc.bottom - q->rc.top), q->dev);
        t[239] = 0;
        Lbl(pg, t, 12, y, 720, 18);
        s->chMon[i] = Chkbox(pg, L"Показывать кнопку на этом мониторе", 28, y + 26, 300, 320 + i, q->enabled);
        s->cbCorner[i] = Mk(pg, L"COMBOBOX", L"", WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, 0, 340, y + 24, 190, 200, 340 + i);
        for (int c = 0; c < 4; c++) SendMessageW(s->cbCorner[i], CB_ADDSTRING, 0, (LPARAM)g_cornerName[c]);
        SendMessageW(s->cbCorner[i], CB_SETCURSEL, q->corner, 0);
        Btn(pg, L"Сбросить отступ", 542, y + 24, 140, 24, 360 + i, 0);
        s->mdx[i] = q->dx;
        s->mdy[i] = q->dy;
    }
    Lbl(pg, L"Положение кнопки можно менять мышью: удерживайте Shift и перетащите кнопку внутри её монитора.", 12, 52 + (g_nmb < 1 ? 1 : g_nmb) * 76, 720, 36);
}
static void PgPin(SetDlg *s, HWND pg) {
    s->chPinTop = Chkbox(pg, L"Закреплять окно «поверх всех окон»", 12, 16, 740, 401, g_cfg.pinTop);
    s->chPinAll = Chkbox(pg, L"Закреплять окно «на всех рабочих столах»", 12, 50, 740, 402, g_cfg.pinAll);
    s->chRaise = Chkbox(pg, L"После смены рабочего стола поднимать закреплённые окна наверх", 12, 84, 740, 403, g_cfg.raisePinned);
    s->chKeep = Chkbox(pg, L"Не сворачивать кнопкой окна «поверх всех»", 12, 118, 740, 404, g_cfg.keepTop);
    s->chLocal = Chkbox(pg, L"Выполнять клавиатурные действия на этом компьютере, если активен RDP/VNC (список программ — на вкладке «Исключения»)", 12, 152, 740, 405, g_cfg.localKeys);
}
static void PgExcl(SetDlg *s, HWND pg, int PW, int PH) {
    int eh = (PH - 190) / 2;
    if (eh < 60) eh = 60;
    int y = 10;
    Lbl(pg, L"Исключения: имена программ (по одному в строке, например vlc.exe). Когда окно такой программы активно, кнопка на том мониторе, где это окно, скрывается.", 12, y, PW - 24, 36);
    s->eExcl = Mk(pg, L"EDIT", L"", WS_TABSTOP | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN, WS_EX_CLIENTEDGE, 12, y + 40, PW - 190, eh, 501);
    Btn(pg, L"Добавить .exe…", PW - 166, y + 40, 150, 28, 502, 0);
    LinesToEdit(s->eExcl, g_excl, g_nexcl);
    y += 40 + eh + 12;
    Lbl(pg, L"Программы удалённого доступа (RDP/VNC): когда одна из них активна, клавиатурные действия выполняются на этом компьютере.", 12, y, PW - 24, 36);
    s->eRemote = Mk(pg, L"EDIT", L"", WS_TABSTOP | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN, WS_EX_CLIENTEDGE, 12, y + 40, PW - 190, eh, 503);
    Btn(pg, L"Добавить .exe…", PW - 166, y + 40, 150, 28, 504, 0);
    LinesToEdit(s->eRemote, g_remote, g_nremote);
}
static void PgGen(SetDlg *s, HWND pg) {
    s->chAuto = Chkbox(pg, L"Запускать вместе с Windows", 12, 16, 500, 901, AutoGet());
    s->chDebug = Chkbox(pg, L"Журнал диагностики (CornerBtn.log рядом с программой)", 12, 50, 600, 902, g_debug);
    Lbl(pg, L"Текстовый редактор для скриптов (пусто — Блокнот; можно с параметрами, например «notepad++.exe -multiInst»):", 12, 90, 740, 34);
    s->eEditor = Edt(pg, 12, 126, 540, 24, 906);
    SetWindowTextW(s->eEditor, g_cfg.editor);
    Btn(pg, L"Обзор…", 560, 126, 110, 24, 907, 0);
    Btn(pg, L"Открыть файл настроек (.ini)", 12, 176, 300, 30, 903, 0);
    Btn(pg, L"Перечитать настройки из .ini", 12, 214, 300, 30, 904, 0);
    Btn(pg, L"Скопировать журнал в буфер обмена", 12, 252, 300, 30, 905, 0);
    Lbl(pg, L"CornerBtn версия " APPVER L"  ·  github.com/thzszaund/FastButton-win", 12, 306, 700, 20);
}

static int SetApply(SetDlg *s) {
    wchar_t b[MAX_PATH];
    int any = 0;
    for (int i = 0; i < s->nm && i < g_nmb; i++) if (Chk(s->chMon[i])) any = 1;
    if (s->nm > 0 && !any) {
        MessageBoxW(s->d.h, L"Кнопка должна быть включена хотя бы на одном мониторе.", APPNAME, MB_ICONINFORMATION);
        return 0;
    }
    GetWindowTextW(s->eSize, b, 32);  g_cfg.size = Clamp((int)wcstol(b, NULL, 10), 2, 200);
    GetWindowTextW(s->eOp, b, 32);    g_cfg.opacity = Clamp((int)wcstol(b, NULL, 10), 5, 100);
    GetWindowTextW(s->eHov, b, 32);   g_cfg.hover = Clamp((int)wcstol(b, NULL, 10), 5, 100);
    GetWindowTextW(s->eCol, b, 32);   g_cfg.color = ParseColor(b, g_cfg.color);
    GetWindowTextW(s->eIcon, g_cfg.icon, MAX_PATH);
    Trim(g_cfg.icon);
    g_cfg.shape = (int)SendMessageW(s->cbShape, CB_GETCURSEL, 0, 0) == 1 ? 1 : 0;
    g_click = s->a[0]; g_dbl = s->a[1]; g_mid = s->a[2];
    g_cfg.show = Chk(s->chShow);
    for (int i = 0; i < s->nm && i < g_nmb; i++) {
        MonBtn *q = g_mb[i];
        q->enabled = Chk(s->chMon[i]);
        int c = (int)SendMessageW(s->cbCorner[i], CB_GETCURSEL, 0, 0);
        q->corner = c < 0 ? 0 : c & 3;
        q->dx = s->mdx[i];
        q->dy = s->mdy[i];
        MonSave(q);
    }
    g_cfg.pinTop = Chk(s->chPinTop);
    g_cfg.pinAll = Chk(s->chPinAll);
    g_cfg.raisePinned = Chk(s->chRaise);
    g_cfg.keepTop = Chk(s->chKeep);
    g_cfg.localKeys = Chk(s->chLocal);
    EditToLines(s->eExcl, g_excl, &g_nexcl);
    EditToLines(s->eRemote, g_remote, &g_nremote);
    GetWindowTextW(s->eEditor, g_cfg.editor, MAX_PATH * 2);
    Trim(g_cfg.editor);
    if (Chk(s->chAuto) != AutoGet()) AutoSet(Chk(s->chAuto));
    int dbg = Chk(s->chDebug);
    if (dbg != g_debug) { g_debug = dbg; if (dbg) { DeleteFileW(g_log); LogStartup(); } }
    SaveCfg();
    SaveActs();
    SaveList(L"Exclude", g_excl, g_nexcl);
    SaveList(L"Remote", g_remote, g_nremote);
    SaveSections();
    memcpy(g_bak, g_ent, sizeof g_ent);
    memcpy(g_bakn, g_cnt, sizeof g_cnt);
    FlushIconCache();
    ApplyAll();
    TrayUpdate();
    g_lastFg = NULL;
    return 1;
}
static void SetCmd(Dlg *d, int id, int code) {
    SetDlg *s = (SetDlg *)d;
    wchar_t f[MAX_PATH];
    (void)code;
    if (id >= 411 && id <= 419) { if (s->cur < NSEC) { g_cur = s->cur; TreeCmd(d->h, id); } return; }
    if (id >= 360 && id < 360 + MAXMON) { s->mdx[id - 360] = 0; s->mdy[id - 360] = 0; return; }
    switch (id) {
    case 205: {
        wchar_t b[32];
        GetWindowTextW(s->eCol, b, 32);
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
            SetWindowTextW(s->eCol, b);
        }
        break;
    }
    case 207:
        if (BrowseOpen(d->h, L"Иконки (*.ico;*.exe;*.dll)\0*.ico;*.exe;*.dll\0Все файлы\0*.*\0", f, MAX_PATH)) SetWindowTextW(s->eIcon, f);
        break;
    case 209: SetWindowTextW(s->eIcon, L"@app"); break;
    case 301: case 302: case 303: {
        int k = id - 301;
        wchar_t nm[1] = L"";
        if (EditActDlg(AM_ACTION, g_actTitle[k], nm, s->a[k].action, s->a[k].args, d->h)) ActText(s, k);
        break;
    }
    case 502: AddExeToEdit(d->h, s->eExcl); break;
    case 504: AddExeToEdit(d->h, s->eRemote); break;
    case 903: ShellExecuteW(NULL, L"open", L"notepad.exe", g_ini, NULL, SW_SHOWNORMAL); break;
    case 904:
        LoadAll();
        ReloadMonSettings();
        FlushIconCache();
        ApplyAll();
        TrayUpdate();
        s->noRestore = 1;
        MessageBoxW(d->h, L"Настройки перечитаны из файла. Окно настроек будет закрыто; откройте его снова.", APPNAME, MB_ICONINFORMATION);
        d->done = 1;
        break;
    case 905: CopyLog(); break;
    case 907:
        if (BrowseOpen(d->h, L"Программы\0*.exe;*.com;*.bat;*.cmd\0Все файлы\0*.*\0", f, MAX_PATH)) {
            wchar_t q[MAX_PATH + 4];
            if (wcschr(f, L' ')) _snwprintf(q, MAX_PATH + 4, L"\"%ls\"", f); else cp(q, WSZ(q), f);
            q[MAX_PATH + 3] = 0;
            SetWindowTextW(s->eEditor, q);
        }
        break;
    case IDOK:
        if (SetApply(s)) { d->result = 1; d->done = 1; }
        break;
    case 951: SetApply(s); break;
    case IDCANCEL: d->done = 1; break;
    }
}
static LRESULT SetNotify(Dlg *d, NMHDR *nm) {
    SetDlg *s = (SetDlg *)d;
    if (nm->idFrom == 700 && nm->code == TCN_SELCHANGE) {
        int sel = TabCtrl_GetCurSel(s->tab);
        if (sel < 0 || sel >= NTABS) return 0;
        ShowWindow(s->page[s->cur], SW_HIDE);
        s->cur = sel;
        ShowWindow(s->page[sel], SW_SHOW);
        if (sel < NSEC) g_cur = sel;
    } else if (nm->code == NM_DBLCLK && nm->idFrom >= 701 && nm->idFrom < 701 + NSEC) {
        g_cur = (int)nm->idFrom - 701;
        TreeCmd(d->h, 418);
    }
    return 0;
}
static void ShowSettings(void) {
    static int busy = 0;
    static SetDlg s;
    if (busy || g_modal) return;
    busy = 1;
    memcpy(g_bak, g_ent, sizeof g_ent);
    memcpy(g_bakn, g_cnt, sizeof g_cnt);
    memset(&s, 0, sizeof s);
    POINT pt;
    GetCursorPos(&pt);
    MONITORINFO mi;
    mi.cbSize = sizeof mi;
    GetMonitorInfoW(MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST), &mi);
    int cw = 830, ch = 640;
    int maxw = (mi.rcWork.right - mi.rcWork.left) * 96 / g_dpi - 20, maxh = (mi.rcWork.bottom - mi.rcWork.top) * 96 / g_dpi - 70;
    if (cw > maxw) cw = maxw;
    if (ch > maxh) ch = maxh;
    if (ch < 440) ch = 440;
    NewDlg(&s.d, L"CornerBtn — настройки", cw, ch);
    s.d.oncmd = SetCmd;
    s.d.onnotify = SetNotify;
    HWND h = s.d.h;
    RECT cr;
    GetClientRect(h, &cr);
    s.tab = CreateWindowExW(0, WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPSIBLINGS | TCS_MULTILINE,
                            S(8), S(8), cr.right - S(16), cr.bottom - S(58), h, (HMENU)700, g_hinst, NULL);
    SendMessageW(s.tab, WM_SETFONT, (WPARAM)g_font, TRUE);
    for (int i = 0; i < NTABS; i++) {
        TCITEMW ti;
        memset(&ti, 0, sizeof ti);
        ti.mask = TCIF_TEXT;
        ti.pszText = (LPWSTR)g_tabName[i];
        TabCtrl_InsertItem(s.tab, i, &ti);
    }
    RECT dr;
    GetClientRect(s.tab, &dr);
    TabCtrl_AdjustRect(s.tab, FALSE, &dr);
    MapWindowPoints(s.tab, h, (POINT *)&dr, 2);
    int pw = dr.right - dr.left, ph = dr.bottom - dr.top;
    int PW = pw * 96 / g_dpi, PH = ph * 96 / g_dpi;
    for (int i = 0; i < NTABS; i++)
        s.page[i] = CreateWindowExW(WS_EX_CONTROLPARENT, L"CBPage", L"", WS_CHILD | WS_CLIPCHILDREN | (i == 0 ? WS_VISIBLE : 0),
                                    dr.left, dr.top, pw, ph, h, NULL, g_hinst, NULL);
    for (int i = 0; i < NSEC; i++) PgTree(&s, i, s.page[i], PW, PH);
    PgApp(&s, s.page[TAB_APP]);
    PgAct(&s, s.page[TAB_ACT], PW);
    PgMon(&s, s.page[TAB_MON]);
    PgPin(&s, s.page[TAB_PIN]);
    PgExcl(&s, s.page[TAB_EXCL], PW, PH);
    PgGen(&s, s.page[TAB_GEN]);
    g_cur = 0;
    Btn(h, L"OK", cw - 296, ch - 40, 88, 30, IDOK, 1);
    Btn(h, L"Отмена", cw - 200, ch - 40, 88, 30, IDCANCEL, 0);
    Btn(h, L"Применить", cw - 104, ch - 40, 92, 30, 951, 0);
    int ok = RunModal(&s.d, g_ctl);
    if (!ok && !s.noRestore) {
        memcpy(g_ent, g_bak, sizeof g_ent);
        memcpy(g_cnt, g_bakn, sizeof g_cnt);
    }
    FlushIconCache();
    g_cur = 0;
    busy = 0;
}

/* ---------- главное меню (кнопка и трей одинаковые) ---------- */
enum { ID_TOGGLE = 100, ID_PIN, ID_CP1, ID_CP2, ID_CP3, ID_CP4, ID_CP5, ID_SETTINGS, ID_EXIT };
static HWND g_lastActive; /* последнее активное окно (не наше и не оболочка) */

static int BuildSub(HMENU m, int *i, int depth) {
    int count = 0, sec = g_cur;
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
            SetItemBmp(m, GetMenuItemCount(m) - 1, EntryBmp(e, sec));
            count++;
        } else {
            AppendMenuW(m, MF_STRING, (UINT_PTR)(ID_ENT0 + sec * MAXE + *i), e->name);
            SetItemBmp(m, GetMenuItemCount(m) - 1, EntryBmp(e, sec));
            (*i)++;
            count++;
        }
    }
    return count;
}
static HMENU BuildSection(int s) {
    HMENU m = CreatePopupMenu();
    int keep = g_cur, i = 0;
    g_cur = s;
    int c = BuildSub(m, &i, 0);
    g_cur = keep;
    if (!c) AppendMenuW(m, MF_GRAYED | MF_STRING, 0, L"(пусто — добавьте в «Настройки»)");
    return m;
}
static HMENU BuildMainMenu(void) {
    static const wchar_t *const titles[NSEC] = { L"Программы", L"Скрипты и команды", L"Папки", L"Файлы" };
    HMENU m = CreatePopupMenu();
    for (int s = 0; s < NSEC; s++) AppendMenuW(m, MF_POPUP | MF_STRING, (UINT_PTR)BuildSection(s), titles[s]);
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING | (g_cfg.show ? MF_CHECKED : 0), ID_TOGGLE, ShowLabel());
    int pinned = 0;
    if (g_prevFg && IsWindow(g_prevFg) && !BadClass(g_prevFg)) pinned = IsPinned(GetAncestor(g_prevFg, GA_ROOT));
    AppendMenuW(m, MF_STRING, ID_PIN, pinned ? L"Открепить активное окно" : L"Закрепить активное окно");
    {
        UINT gr = (g_prevFg && IsWindow(g_prevFg) && !BadClass(g_prevFg) && !IsOurs(g_prevFg)) ? 0 : MF_GRAYED;
        HMENU cm = CreatePopupMenu();
        AppendMenuW(cm, MF_STRING | gr, ID_CP1, L"Заголовок активного окна");
        AppendMenuW(cm, MF_STRING | gr, ID_CP2, L"Имя exe активного окна");
        AppendMenuW(cm, MF_STRING | gr, ID_CP3, L"Имя exe активного окна с аргументами");
        AppendMenuW(cm, MF_STRING | gr, ID_CP4, L"Полный путь к exe активного окна");
        AppendMenuW(cm, MF_SEPARATOR, 0, NULL);
        AppendMenuW(cm, MF_STRING | gr, ID_CP5, L"Открыть в проводнике расположение exe активного окна");
        AppendMenuW(m, MF_POPUP, (UINT_PTR)cm, L"Скопировать");
    }
    AppendMenuW(m, MF_STRING, ID_SETTINGS, L"Настройки…");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_GRAYED | MF_STRING, 0, L"CornerBtn версия " APPVER);
    AppendMenuW(m, MF_STRING, ID_EXIT, L"Выход");
    return m;
}
static void HandleCmd(int cmd, MonBtn *b) {
    if (cmd >= ID_ENT0 && cmd < ID_ENT0 + NSEC * MAXE) {
        int sec = (cmd - ID_ENT0) / MAXE, idx = (cmd - ID_ENT0) % MAXE;
        if (idx < g_cnt[sec]) RunEntry(sec, idx, b);
        return;
    }
    switch (cmd) {
    case ID_TOGGLE: ToggleShow(); break;
    case ID_PIN: PinToggle(g_prevFg); break;
    case ID_CP1: case ID_CP2: case ID_CP3: case ID_CP4: case ID_CP5: CopyInfo(cmd - ID_CP1 + 1); break;
    case ID_SETTINGS: ShowSettings(); break;
    case ID_EXIT: DestroyWindow(g_ctl); break;
    }
}
/* ---------- правая кнопка на пункте меню: свои действия ---------- */
enum { CX_SHELL = 3001, CX_REVEAL, CX_COPYPATH, CX_COPYFILE, CX_PROPS, CX_EDIT };
static IContextMenu2 *g_cm2;
static IContextMenu3 *g_cm3;
static HHOOK g_mhook;
static int g_ctxSec = -1, g_ctxIdx = -1;
static POINT g_ctxPt;

static int EntryTarget(const Entry *e, wchar_t *out, int n, int *exists) { /* путь к файлу/папке пункта (0 — это не файл) */
    wchar_t p[1100], full[1100];
    if (e->type != 0 || !e->action[0] || e->action[0] == L'@') return 0;
    ExpandEnvironmentStringsW(e->action, p, 1100);
    Trim(p);
    size_t l = wcslen(p);
    if (l > 1 && p[0] == L'"' && p[l - 1] == L'"') { p[l - 1] = 0; memmove(p, p + 1, l * sizeof(wchar_t)); }
    if (wcsstr(p, L"://") || !_wcsnicmp(p, L"mailto:", 7)) return 0;
    cp(full, WSZ(full), p);
    if (!wcschr(p, L'\\') && !wcschr(p, L':')) { /* просто имя программы: ищем в PATH */
        if (!SearchPathW(NULL, p, NULL, 1100, full, NULL) && !SearchPathW(NULL, p, L".exe", 1100, full, NULL)) { *exists = 0; cp(out, (size_t)n, p); return 1; }
    }
    cp(out, (size_t)n, full);
    *exists = GetFileAttributesW(full) != INVALID_FILE_ATTRIBUTES;
    return 1;
}
static void RevealPath(const wchar_t *path) { /* открыть папку в проводнике с выделением файла */
    PIDLIST_ABSOLUTE pidl = NULL;
    if (SUCCEEDED(SHParseDisplayName(path, NULL, &pidl, 0, NULL)) && pidl) {
        SHOpenFolderAndSelectItems(pidl, 0, NULL, 0);
        CoTaskMemFree(pidl);
    } else {
        wchar_t a[MAX_PATH * 2 + 16];
        _snwprintf(a, MAX_PATH * 2 + 16, L"/select,\"%ls\"", path);
        a[MAX_PATH * 2 + 15] = 0;
        ShellExecuteW(NULL, L"open", L"explorer.exe", a, NULL, SW_SHOWNORMAL);
    }
}
static int CopyFileToClipboard(const wchar_t *path) { /* как Ctrl+C на файле в проводнике */
    size_t n = wcslen(path) + 1, bytes = sizeof(DROPFILES) + (n + 1) * sizeof(wchar_t);
    HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, bytes);
    HGLOBAL ge = GlobalAlloc(GMEM_MOVEABLE, sizeof(DWORD));
    if (!g || !ge) { if (g) GlobalFree(g); if (ge) GlobalFree(ge); return 0; }
    DROPFILES *df = (DROPFILES *)GlobalLock(g);
    df->pFiles = sizeof(DROPFILES);
    df->fWide = TRUE;
    memcpy((BYTE *)df + sizeof(DROPFILES), path, n * sizeof(wchar_t));
    GlobalUnlock(g);
    *(DWORD *)GlobalLock(ge) = DROPEFFECT_COPY;
    GlobalUnlock(ge);
    if (!OpenClipboard(g_ctl)) { GlobalFree(g); GlobalFree(ge); return 0; }
    EmptyClipboard();
    SetClipboardData(CF_HDROP, g);
    SetClipboardData(RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT), ge);
    CloseClipboard();
    return 1;
}
static void ShowProps(const wchar_t *path) {
    SHELLEXECUTEINFOW sei;
    memset(&sei, 0, sizeof sei);
    sei.cbSize = sizeof sei;
    sei.fMask = SEE_MASK_INVOKEIDLIST;
    sei.hwnd = g_ctl;
    sei.lpVerb = L"properties";
    sei.lpFile = path;
    sei.nShow = SW_SHOW;
    ShellExecuteExW(&sei);
}
static void ShellContextMenu(const wchar_t *path, POINT pt) { /* обычное контекстное меню проводника */
    PIDLIST_ABSOLUTE pidl = NULL;
    IShellFolder *sf = NULL;
    PCUITEMID_CHILD child = NULL;
    if (FAILED(SHParseDisplayName(path, NULL, &pidl, 0, NULL)) || !pidl) return;
    if (SUCCEEDED(SHBindToParent(pidl, &IID_IShellFolder, (void **)&sf, &child)) && sf) {
        IContextMenu *cm = NULL;
        if (SUCCEEDED(sf->lpVtbl->GetUIObjectOf(sf, g_ctl, 1, &child, &IID_IContextMenu, NULL, (void **)&cm)) && cm) {
            HMENU hm = CreatePopupMenu();
            UINT fl = CMF_NORMAL | CMF_CANRENAME | ((GetKeyState(VK_SHIFT) < 0) ? CMF_EXTENDEDVERBS : 0);
            if (SUCCEEDED(cm->lpVtbl->QueryContextMenu(cm, hm, 0, 1, 0x7FFF, fl))) {
                cm->lpVtbl->QueryInterface(cm, &IID_IContextMenu3, (void **)&g_cm3);
                cm->lpVtbl->QueryInterface(cm, &IID_IContextMenu2, (void **)&g_cm2);
                g_inMenu = 1;
                SetForegroundWindow(g_ctl);
                int cmd = (int)TrackPopupMenu(hm, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, g_ctl, NULL);
                PostMessageW(g_ctl, WM_NULL, 0, 0);
                g_inMenu = 0;
                if (g_cm3) { g_cm3->lpVtbl->Release(g_cm3); g_cm3 = NULL; }
                if (g_cm2) { g_cm2->lpVtbl->Release(g_cm2); g_cm2 = NULL; }
                if (cmd > 0) {
                    CMINVOKECOMMANDINFOEX ci;
                    memset(&ci, 0, sizeof ci);
                    ci.cbSize = sizeof ci;
                    ci.fMask = CMIC_MASK_UNICODE | CMIC_MASK_PTINVOKE;
                    ci.hwnd = g_ctl;
                    ci.lpVerb = MAKEINTRESOURCEA(cmd - 1);
                    ci.lpVerbW = MAKEINTRESOURCEW(cmd - 1);
                    ci.nShow = SW_SHOWNORMAL;
                    ci.ptInvoke = pt;
                    cm->lpVtbl->InvokeCommand(cm, (CMINVOKECOMMANDINFO *)&ci);
                }
            }
            DestroyMenu(hm);
            cm->lpVtbl->Release(cm);
        }
        sf->lpVtbl->Release(sf);
    }
    CoTaskMemFree(pidl);
}
static void ShowEntryContext(void) {
    int sec = g_ctxSec, idx = g_ctxIdx, exists = 0;
    POINT pt = g_ctxPt;
    wchar_t path[1100];
    g_ctxSec = -1;
    if (sec < 0 || sec >= NSEC || idx < 0 || idx >= g_cnt[sec]) return;
    Entry *e = &g_ent[sec][idx];
    if (!EntryTarget(e, path, 1100, &exists)) return;
    int isDir = exists && IsDirPath(path);
    int script = exists && !isDir && (sec == SEC_SCRIPT || IsScriptExt(path));
    UINT gr = exists ? 0 : MF_GRAYED;
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING | gr, CX_SHELL, L"Контекстное меню Explorer");
    AppendMenuW(m, MF_STRING | gr, CX_REVEAL, L"Показать в папке");
    AppendMenuW(m, MF_STRING, CX_COPYPATH, L"Скопировать путь");
    AppendMenuW(m, MF_STRING | gr, CX_COPYFILE, L"Скопировать");
    AppendMenuW(m, MF_STRING | gr, CX_PROPS, L"Свойства");
    if (script) {
        AppendMenuW(m, MF_SEPARATOR, 0, NULL);
        AppendMenuW(m, MF_STRING, CX_EDIT, g_cfg.editor[0] ? L"Открыть в текстовом редакторе" : L"Открыть в блокноте");
    }
    g_inMenu = 1;
    SetForegroundWindow(g_ctl);
    int cmd = (int)TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, g_ctl, NULL);
    PostMessageW(g_ctl, WM_NULL, 0, 0);
    g_inMenu = 0;
    DestroyMenu(m);
    switch (cmd) {
    case CX_SHELL: ShellContextMenu(path, pt); break;
    case CX_REVEAL: RevealPath(path); break;
    case CX_COPYPATH: {
        static wchar_t buf[3000], args[1300];
        if (sec == SEC_PROG || sec == SEC_SCRIPT) { /* путь с аргументами запуска */
            ExpandEnvironmentStringsW(e->args, args, 1300);
            Trim(args);
            if (args[0]) _snwprintf(buf, 3000, L"\"%ls\" %ls", path, args); else _snwprintf(buf, 3000, L"\"%ls\"", path);
        } else cp(buf, WSZ(buf), path);
        buf[2999] = 0;
        if (CopyText(buf)) Balloon(L"Путь скопирован", buf);
        break;
    }
    case CX_COPYFILE:
        if (CopyFileToClipboard(path)) Balloon(L"Скопировано в буфер обмена (можно вставить в проводнике)", path);
        break;
    case CX_PROPS: ShowProps(path); break;
    case CX_EDIT: OpenInEditor(path); break;
    }
}
/* перехват правой кнопки в нашем всплывающем меню (пока оно открыто) */
static LRESULT CALLBACK MenuHookProc(int code, WPARAM w, LPARAM l) {
    if (code == MSGF_MENU) {
        MSG *m = (MSG *)l;
        if (m->message == WM_RBUTTONUP && g_selMenu) {
            int pos = MenuItemFromPoint(NULL, g_selMenu, m->pt);
            if (pos >= 0) {
                UINT id = GetMenuItemID(g_selMenu, pos);
                if (id >= ID_ENT0 && id < ID_ENT0 + NSEC * MAXE) {
                    int sec = (int)(id - ID_ENT0) / MAXE, idx = (int)(id - ID_ENT0) % MAXE;
                    wchar_t tp[1100];
                    int ex = 0;
                    if (idx < g_cnt[sec] && EntryTarget(&g_ent[sec][idx], tp, 1100, &ex)) {
                        g_ctxSec = sec;
                        g_ctxIdx = idx;
                        g_ctxPt = m->pt;
                        EndMenu();
                    }
                }
                return 1; /* правый клик не должен запускать пункт */
            }
        }
    }
    return CallNextHookEx(g_mhook, code, w, l);
}
static int TrackWithHook(HWND owner, HMENU m, POINT pt) {
    g_ctxSec = -1;
    g_selMenu = NULL;
    g_mhook = SetWindowsHookExW(WH_MSGFILTER, MenuHookProc, NULL, GetCurrentThreadId());
    int cmd = (int)TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, owner, NULL);
    if (g_mhook) { UnhookWindowsHookEx(g_mhook); g_mhook = NULL; }
    PostMessageW(owner, WM_NULL, 0, 0);
    return cmd;
}
static void ShowMenu(MonBtn *b) {
    HWND f = GetForegroundWindow();
    if (f && !IsOurs(f) && !BadClass(f)) g_prevFg = f;
    else g_prevFg = (g_lastActive && IsWindow(g_lastActive)) ? g_lastActive : NULL;
    HMENU m = BuildMainMenu();
    POINT pt;
    GetCursorPos(&pt);
    g_inMenu = 1;
    SetForegroundWindow(b->hwnd);
    int cmd = TrackWithHook(b->hwnd, m, pt);
    g_inMenu = 0;
    TipHide();
    DestroyMenu(m);
    if (cmd) HandleCmd(cmd, b);
    else if (g_ctxSec >= 0) ShowEntryContext();
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
    case WM_MENUSELECT:
        OnMenuSelect(w, l);
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
static void ShowTrayMenu(void) {
    g_prevFg = (g_lastActive && IsWindow(g_lastActive)) ? g_lastActive : NULL; /* активное окно до клика по значку в трее */
    HMENU m = BuildMainMenu();
    POINT pt;
    GetCursorPos(&pt);
    g_inMenu = 1;
    SetForegroundWindow(g_ctl);
    int cmd = TrackWithHook(g_ctl, m, pt);
    g_inMenu = 0;
    TipHide();
    DestroyMenu(m);
    if (cmd) HandleCmd(cmd, g_nmb ? g_mb[0] : NULL);
    else if (g_ctxSec >= 0) ShowEntryContext();
}
static void CALLBACK FgHook(HWINEVENTHOOK hk, DWORD ev, HWND w, LONG idObj, LONG idChild, DWORD th, DWORD ms) {
    (void)hk; (void)ev; (void)idChild; (void)th; (void)ms;
    if (idObj != OBJID_WINDOW || !w) return;
    HWND r = GetAncestor(w, GA_ROOT);
    if (r && !IsOurs(r) && !BadClass(r) && IsWindowVisible(r)) g_lastActive = r;
}
static LRESULT CALLBACK CtlProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (g_wmTaskbar && m == g_wmTaskbar) { TrayAdd(); return 0; } /* проводник перезапущен */
    if ((g_cm2 || g_cm3) && (m == WM_INITMENUPOPUP || m == WM_DRAWITEM || m == WM_MEASUREITEM || m == WM_MENUCHAR)) {
        LRESULT r = 0;
        if (g_cm3) { if (SUCCEEDED(g_cm3->lpVtbl->HandleMenuMsg2(g_cm3, m, w, l, &r))) return r; }
        else if (SUCCEEDED(g_cm2->lpVtbl->HandleMenuMsg(g_cm2, m, w, l))) return (m == WM_INITMENUPOPUP) ? 0 : TRUE;
    }
    switch (m) {
    case WM_TRAY:
        if (g_modal) return 0;
        if (l == WM_LBUTTONUP) ToggleShow();
        else if (l == WM_RBUTTONUP || l == WM_CONTEXTMENU) ShowTrayMenu();
        return 0;
    case WM_MENUSELECT: OnMenuSelect(w, l); return 0;
    case WM_APP + 5: ShowSettings(); return 0;
    case WM_DISPLAYCHANGE: SetTimer(h, 9, 700, NULL); return 0;
    case WM_TIMER:
        if (w == 9) { KillTimer(h, 9); SyncMonitors(); }
        else if (w == 2) Reassert();
        else if (w == 3) { UpdateExclusion(); CheckDesktopSwitch(); }
        return 0;
    case WM_DESTROY: TrayRemove(); PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE hp, PWSTR cl, int ns) {
    (void)hp; (void)cl; (void)ns;
    g_hinst = hi;
    HANDLE mtx = CreateMutexW(NULL, FALSE, L"Local\\CornerBtn_SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;
    (void)mtx;
    OleInitialize(NULL); /* OLE нужен для перетаскивания файлов (IDropTarget) */
    INITCOMMONCONTROLSEX icc = { sizeof icc, ICC_TREEVIEW_CLASSES | ICC_STANDARD_CLASSES | ICC_BAR_CLASSES | ICC_TAB_CLASSES };
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
    wc.lpfnWndProc = PageProc;
    wc.hInstance = hi;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"CBPage";
    RegisterClassExW(&wc);
    memset(&wc, 0, sizeof wc);
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = CtlProc;
    wc.hInstance = hi;
    wc.lpszClassName = L"CBCtl";
    RegisterClassExW(&wc);

    g_wmTaskbar = RegisterWindowMessageW(L"TaskbarCreated");
    g_ctl = CreateWindowExW(WS_EX_TOOLWINDOW, L"CBCtl", APPNAME, WS_POPUP, 0, 0, 0, 0, NULL, NULL, hi, NULL);
    if (!g_ctl) return 1;
    TipInit();
    SyncMonitors();
    if (g_nmb == 0) return 1;
    if (g_debug) { DeleteFileW(g_log); LogStartup(); }
    if (fresh) SaveAll();
    if (!TrayAdd() && !g_cfg.show) { /* без значка в трее скрытые кнопки не вернуть */
        g_cfg.show = 1;
        for (int i = 0; i < g_nmb; i++) UpdateVis(g_mb[i]);
    }
    g_lastActive = GetAncestor(GetForegroundWindow(), GA_ROOT);
    SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, NULL, FgHook, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    SetTimer(g_ctl, 2, 1000, NULL); /* держать кнопки поверх всех */
    SetTimer(g_ctl, 3, 300, NULL);  /* проверка исключений и смены стола */
    if (wcsstr(GetCommandLineW(), L"--settings")) PostMessageW(g_ctl, WM_APP + 5, 0, 0);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    OleUninitialize();
    return 0;
}
