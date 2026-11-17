// Реализация UI. Скины — следующая итерация; сейчас встроенный вид на
// стандартных контролах (дефолтный скин по DESIGN.ru.md раздел 4).
#include "ui.h"

#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>

#include <cstring>
#include <fstream>
#include <thread>

namespace host {

namespace {

constexpr UINT kMsgTray = WM_APP + 1;      // колбэк лотка
// конец трека — host::kChipMsgTrackEnd из player.h

constexpr UINT kIdNext = 1001;
constexpr UINT kIdPrev = 1002;
constexpr UINT kIdToggle = 1003;
constexpr UINT kIdStop = 1004;
constexpr UINT kIdOpen = 1005;
constexpr UINT kIdPlaylist = 1006;
constexpr UINT kIdQuit = 1007;
constexpr UINT kIdShowPlayer = 1008;
constexpr UINT kIdList = 1009;
constexpr UINT kIdAdd = 1010;
constexpr UINT kIdRemove = 1011;
constexpr UINT kIdStaticTitle = 1012;
constexpr UINT kIdStaticSub = 1013;
constexpr UINT kIdStaticPos = 1014;
constexpr UINT kIdShowLibrary = 1015;
constexpr UINT kIdLibCombo = 1016;
constexpr UINT kIdLibEdit = 1017;
constexpr UINT kIdLibGo = 1018;
constexpr UINT kIdLibList = 1019;
constexpr UINT kIdLibStatus = 1020;

constexpr UINT kMsgLibResult = WM_APP + 3;   // ответ Query (heap vector)
constexpr UINT kMsgLibFetched = WM_APP + 4;  // загрузка завершена (heap)

// определены ниже (раздел Library)
struct FetchResultPayload;
static void lib_set_status(const wchar_t* t);
static void lib_fill_list();
static void lib_handle_fetched(FetchResultPayload* p);

// --- глобальное состояние UI-потока ---
struct UiState {
    EngineManager* em = nullptr;
    LibraryManager* lm = nullptr;
    Player* player = nullptr;
    HWND main_wnd = nullptr;
    HWND player_wnd = nullptr;
    HWND list_wnd = nullptr;
    HWND list_box = nullptr;
    HWND lib_wnd = nullptr;
    HWND lib_combo = nullptr;
    HWND lib_edit = nullptr;
    HWND lib_list = nullptr;
    HWND lib_status = nullptr;
    NOTIFYICONDATAW tray{};
    bool tray_added = false;
    std::vector<ChipLibEntry> lib_results;
    volatile int lib_cancel = 0;
};
UiState g;

// --- общий тред-пейлоад для PostMessage ---
struct AsyncPayload {
    virtual ~AsyncPayload() {}
};

struct QueryResultPayload : AsyncPayload {
    std::vector<ChipLibEntry> entries; // успешный ответ
    std::string error;                 // или ошибка
};

struct FetchResultPayload : AsyncPayload {
    std::wstring path;        // файл в кеше (успех)
    ChipLibEntry entry;       // что скачивали
    std::string error;        // или ошибка
};

// Кеш: exe_dir/cache/<lib_id>/.
std::wstring cache_dir_for(const char* lib_id) {
    const std::wstring dir =
        exe_dir() + L"\\cache\\" + utf8_to_wide(lib_id);
    CreateDirectoryW((exe_dir() + L"\\cache").c_str(), nullptr);
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

void refresh_list() {
    SendMessageW(g.list_box, LB_RESETCONTENT, 0, 0);
    for (size_t i = 0; i < g.player->item_count(); ++i) {
        const PlaylistItem& it = g.player->item(i);
        const size_t kMax = 200;
        char row[kMax];
        std::snprintf(row, sizeof(row), "%.3d. %s%s", i + 1,
                      it.title.empty() ? "" : it.title.c_str(),
                      it.author.empty() ? "" : (" - " + it.author).c_str());
        const int idx = SendMessageA(g.list_box, LB_ADDSTRING, 0,
                                     LPARAM(row));
        SendMessageA(g.list_box, LB_SETITEMDATA, WPARAM(idx), LPARAM(i));
    }
}

void add_files(HWND owner) {
    const std::wstring filter = g.em->open_dialog_filter();
    wchar_t buf[8192];
    buf[0] = L'\0';
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = filter.c_str();
    ofn.lpstrFile = buf;
    ofn.nMaxFile = 8192;
    ofn.Flags = OFN_ALLOWMULTISELECT | OFN_EXPLORER | OFN_FILEMUSTEXIST |
                OFN_HIDEREADONLY;
    if (!GetOpenFileNameW(&ofn)) return;

    // Мультивыбор: каталог + имена через \0.
    const std::wstring dir = buf;
    wchar_t* p = buf + dir.size() + 1;
    if (*p == L'\0') {
        // один файл
        const int idx = g.player->add_file(dir);
        if (idx >= 0) g.player->play(idx);
    } else {
        int first = -1;
        while (*p) {
            const int idx = g.player->add_file(dir + L"\\" + p);
            if (first < 0 && idx >= 0) first = idx;
            p += wcslen(p) + 1;
        }
        if (first >= 0) g.player->play(first);
    }
    refresh_list();
}

void handle_drop(HDROP drop) {
    UINT n = DragQueryFileW(drop, 0xFFFFFFFFu, nullptr, 0);
    int first = -1;
    for (UINT i = 0; i < n; ++i) {
        wchar_t path[MAX_PATH];
        if (DragQueryFileW(drop, i, path, MAX_PATH)) {
            const int idx = g.player->add_file(path);
            if (first < 0 && idx >= 0) first = idx;
        }
    }
    DragFinish(drop);
    if (first >= 0) g.player->play(first);
    refresh_list();
}

void update_player_statics() {
    const PlayState st = g.player->state();
    const PlaylistItem* cur = g.player->current();

    wchar_t title[256] = L"—";
    wchar_t sub[256] = L"";
    wchar_t pos[64] = L"";
    if (cur) {
        const std::wstring t = utf8_to_wide(
            cur->title.empty() ? wide_to_utf8(cur->path) : cur->title);
        wcsncpy(title, t.c_str(), 255);
        const std::string pos_s = format_ms(g.player->position_ms());
        const std::string dur_s =
            cur->duration_ms ? format_ms(cur->duration_ms) : "";
        char sb[128];
        std::snprintf(sb, sizeof(sb), "%s%s  [%s]  %s",
                      cur->author.empty() ? "" : cur->author.c_str(),
                      cur->author.empty() ? "" : "  |  ",
                      cur->format.c_str(),
                      g.player->state() == PlayState::Paused ? "PAUSED"
                      : g.player->state() == PlayState::Stopped ? "STOPPED"
                                                                : "");
        const std::string p = std::string(pos_s) +
                              (dur_s.empty() ? "" : " / " + dur_s);
        const std::wstring pw = utf8_to_wide(p);
        wcsncpy(pos, pw.c_str(), 63);
        const std::wstring sw = utf8_to_wide(sb);
        wcsncpy(sub, sw.c_str(), 255);
    }
    SetDlgItemTextW(g.player_wnd, kIdStaticTitle, title);
    SetDlgItemTextW(g.player_wnd, kIdStaticSub, sub);
    SetDlgItemTextW(g.player_wnd, kIdStaticPos, pos);
    (void)st;
}

void update_tray() {
    if (!g.tray_added) return;
    g.tray.uFlags = NIF_TIP;
    const PlayState st = g.player->state();
    wcsncpy(g.tray.szTip,
            st == PlayState::Playing ? L"ChipDeck — playing"
                : st == PlayState::Paused ? L"ChipDeck — paused"
                                          : L"ChipDeck",
            127);
    Shell_NotifyIconW(NIM_MODIFY, &g.tray);
}

LRESULT CALLBACK player_wnd_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_DROPFILES:
        handle_drop(HDROP(wp));
        return 0;
    case WM_TIMER:
        update_player_statics();
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case kIdToggle: g.player->toggle_pause(); update_tray(); return 0;
        case kIdStop: g.player->stop(); update_tray(); update_player_statics(); return 0;
        case kIdNext: g.player->next(); update_tray(); return 0;
        case kIdPrev: g.player->prev(); update_tray(); return 0;
        case kIdOpen: add_files(w); update_player_statics(); return 0;
        case kIdPlaylist: ShowWindow(g.list_wnd, SW_SHOW); SetForegroundWindow(g.list_wnd); return 0;
        case kIdQuit: PostQuitMessage(0); return 0;
        }
        break;
    case WM_CLOSE:
        ShowWindow(w, SW_HIDE); // закрытие окна = сворачивание в лоток
        return 0;
    }
    return DefWindowProcW(w, msg, wp, lp);
}

LRESULT CALLBACK list_wnd_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_DROPFILES:
        handle_drop(HDROP(wp));
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case kIdAdd: add_files(w); return 0;
        case kIdRemove: {
            int sel = int(SendMessageW(g.list_box, LB_GETCURSEL, 0, 0));
            if (sel != LB_ERR) {
                const size_t real =
                    size_t(SendMessageW(g.list_box, LB_GETITEMDATA,
                                        WPARAM(sel), 0));
                g.player->remove(real);
                refresh_list();
            }
            return 0;
        }
        case kIdList:
            if (HIWORD(wp) == LBN_DBLCLK) {
                int sel = int(SendMessageW(g.list_box, LB_GETCURSEL, 0, 0));
                if (sel != LB_ERR) {
                    const size_t real = size_t(
                        SendMessageW(g.list_box, LB_GETITEMDATA,
                                     WPARAM(sel), 0));
                    g.player->play(int(real));
                    update_tray();
                }
            }
            return 0;
        }
        break;
    case WM_CLOSE:
        ShowWindow(w, SW_HIDE);
        return 0;
    }
    return DefWindowProcW(w, msg, wp, lp);
}

LRESULT CALLBACK main_wnd_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case kMsgTray:
        if (lp == WM_LBUTTONUP) { // одинарный клик = play/pause
            g.player->toggle_pause();
            update_tray();
        } else if (lp == WM_RBUTTONUP) {
            POINT pt;
            GetCursorPos(&pt);
            HMENU menu = CreatePopupMenu();
            AppendMenuW(menu, MF_STRING, kIdToggle, L"Play/Pause");
            AppendMenuW(menu, MF_STRING, kIdStop, L"Stop");
            AppendMenuW(menu, MF_STRING, kIdNext, L"Next");
            AppendMenuW(menu, MF_STRING, kIdPrev, L"Prev");
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING, kIdShowPlayer, L"Player");
            AppendMenuW(menu, MF_STRING, kIdPlaylist, L"Playlist");
            if (g.lm->count() > 0)
                AppendMenuW(menu, MF_STRING, kIdShowLibrary, L"Library");
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING, kIdQuit, L"Quit");
            SetForegroundWindow(w);
            TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, w, nullptr);
            DestroyMenu(menu);
        }
        return 0;
    case kChipMsgTrackEnd: // из audio-потока: авто-переход к следующему
        g.player->next();
        update_tray();
        return 0;
    case WM_COMMAND: {
        // команды из меню лотка
        switch (LOWORD(wp)) {
        case kIdToggle: g.player->toggle_pause(); update_tray(); return 0;
        case kIdStop: g.player->stop(); update_tray(); return 0;
        case kIdNext: g.player->next(); update_tray(); return 0;
        case kIdPrev: g.player->prev(); update_tray(); return 0;
        case kIdShowPlayer:
            ShowWindow(g.player_wnd, SW_SHOW);
            SetForegroundWindow(g.player_wnd);
            return 0;
        case kIdPlaylist:
            ShowWindow(g.list_wnd, SW_SHOW);
            SetForegroundWindow(g.list_wnd);
            return 0;
        case kIdShowLibrary:
            if (g.lib_wnd) {
                ShowWindow(g.lib_wnd, SW_SHOW);
                SetForegroundWindow(g.lib_wnd);
            }
            return 0;
        case kIdQuit: PostQuitMessage(0); return 0;
        }
        break;
    }
    case kMsgLibResult: {
        // Query завершился в фоне
        auto* p = reinterpret_cast<QueryResultPayload*>(lp);
        g.lib_results = std::move(p->entries);
        lib_fill_list();
        if (!p->error.empty()) {
            const std::wstring msg = utf8_to_wide(p->error);
            lib_set_status(msg.c_str());
        } else {
            wchar_t st[64];
            swprintf(st, 64, L"%u results", unsigned(g.lib_results.size()));
            lib_set_status(st);
        }
        delete p;
        return 0;
    }
    case kMsgLibFetched:
        lib_handle_fetched(reinterpret_cast<FetchResultPayload*>(lp));
        return 0;
    case WM_DESTROY:
        if (g.tray_added) Shell_NotifyIconW(NIM_DELETE, &g.tray);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(w, msg, wp, lp);
}

HWND make_control(HWND parent, const wchar_t* cls, const wchar_t* text,
                  DWORD style, int x, int y, int w, int h, UINT id) {
    HWND c = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, x,
                             y, w, h, parent,
                             reinterpret_cast<HMENU>(UINT_PTR(id)),
                             nullptr, nullptr);
    SendMessageW(c, WM_SETFONT,
                 WPARAM(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    return c;
}

namespace {

// Внешний размер окна из желаемого размера КЛИЕНТСКОЙ области.
SIZE outer_from_client(DWORD style, int cw, int ch) {
    RECT r{0, 0, cw, ch};
    AdjustWindowRectEx(&r, style, FALSE, 0);
    return {r.right - r.left, r.bottom - r.top};
}

} // namespace

void build_player_window(HINSTANCE inst) {
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = player_wnd_proc;
    wc.hInstance = inst;
    wc.lpszClassName = L"ChipDeckPlayer";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassExW(&wc);

    // Координаты — в клиентских пикселях; клиент 360x150.
    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU |
                        WS_MINIMIZEBOX;
    const SIZE outer = outer_from_client(style, 360, 150);
    g.player_wnd = CreateWindowExW(
        0, L"ChipDeckPlayer", L"ChipDeck", style,
        CW_USEDEFAULT, CW_USEDEFAULT, outer.cx, outer.cy, nullptr, nullptr,
        inst, nullptr);
    DragAcceptFiles(g.player_wnd, TRUE);

    make_control(g.player_wnd, WC_STATICW, L"", SS_LEFTNOWORDWRAP, 10, 6,
                 340, 20, kIdStaticTitle);
    make_control(g.player_wnd, WC_STATICW, L"", SS_LEFTNOWORDWRAP, 10, 28,
                 340, 18, kIdStaticSub);
    make_control(g.player_wnd, WC_STATICW, L"0:00", SS_LEFTNOWORDWRAP, 10,
                 48, 340, 18, kIdStaticPos);
    // транспорт
    make_control(g.player_wnd, WC_BUTTONW, L"|<", 0, 10, 74, 44, 26, kIdPrev);
    make_control(g.player_wnd, WC_BUTTONW, L"Play/Pause", 0, 60, 74, 100, 26,
                 kIdToggle);
    make_control(g.player_wnd, WC_BUTTONW, L"Stop", 0, 166, 74, 70, 26,
                 kIdStop);
    make_control(g.player_wnd, WC_BUTTONW, L">|", 0, 242, 74, 44, 26,
                 kIdNext);
    // нижний ряд — до 138, клиент 150, запас есть
    make_control(g.player_wnd, WC_BUTTONW, L"Open...", 0, 10, 108, 100, 26,
                 kIdOpen);
    make_control(g.player_wnd, WC_BUTTONW, L"Playlist", 0, 116, 108, 100, 26,
                 kIdPlaylist);
    make_control(g.player_wnd, WC_BUTTONW, L"Quit", 0, 222, 108, 90, 26,
                 kIdQuit);
}

void build_list_window(HINSTANCE inst) {
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = list_wnd_proc;
    wc.hInstance = inst;
    wc.lpszClassName = L"ChipDeckPlaylist";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassExW(&wc);

    const int cw = 460, ch = 320; // клиентская область
    const DWORD style =
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_SIZEBOX;
    const SIZE outer = outer_from_client(style, cw, ch);
    g.list_wnd = CreateWindowExW(
        0, L"ChipDeckPlaylist", L"ChipDeck — Playlist", style,
        CW_USEDEFAULT, CW_USEDEFAULT, outer.cx, outer.cy, nullptr, nullptr,
        inst, nullptr);
    DragAcceptFiles(g.list_wnd, TRUE);

    g.list_box = CreateWindowExW(
        WS_EX_CLIENTEDGE, WC_LISTBOXW, nullptr,
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_HASSTRINGS |
            LBS_NOINTEGRALHEIGHT,
        8, 8, cw - 16, ch - 66, g.list_wnd,
        reinterpret_cast<HMENU>(UINT_PTR(kIdList)), nullptr, nullptr);
    SendMessageW(g.list_box, WM_SETFONT,
                 WPARAM(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    make_control(g.list_wnd, WC_BUTTONW, L"Add...", 0, 8, ch - 50, 90, 26,
                 kIdAdd);
    make_control(g.list_wnd, WC_BUTTONW, L"Remove", 0, 104, ch - 50, 90, 26,
                 kIdRemove);
}

// ---------- Library: окно онлайн-библиотек ----------

static void lib_set_status(const wchar_t* t) {
    SetWindowTextW(g.lib_status, t);
}

// Query в отдельном потоке (блокирующий контракт).
static void lib_start_query(uint32_t lib_idx, uint32_t qtype,
                            const std::string& text) {
    LibraryManager* lm = g.lm;
    volatile int* cancel = &g.lib_cancel;
    *cancel = 0;
    std::thread([lm, lib_idx, qtype, text, cancel]() {
        auto* payload = new QueryResultPayload();
        ChipLibRequest req{};
        req.struct_size = sizeof(req);
        req.query_type = qtype;
        req.limit = 25;
        req.text = text.c_str();
        ChipError err{};
        LibModule& m = lm->mutable_at(lib_idx);
        if (!m.session) lm->connect_all();
        const uint32_t ok =
            m.query_fn(m.session, &req,
                       [](void* user, const ChipLibEntry* e) -> uint32_t {
                           auto* p = static_cast<QueryResultPayload*>(user);
                           p->entries.push_back(*e);
                           return 0; // собираем все
                       },
                       payload, cancel, &err);
        if (!ok && payload->entries.empty()) {
            payload->error = err.message;
        }
        PostMessageW(g.main_wnd, kMsgLibResult, 0,
                     reinterpret_cast<LPARAM>(payload));
    }).detach();
    lib_set_status(L"querying...");
}

// Fetch в отдельном потоке: скачать -> кеш -> плейлист -> играть.
static void lib_start_fetch(uint32_t lib_idx, const ChipLibEntry& entry) {
    LibraryManager* lm = g.lm;
    volatile int* cancel = &g.lib_cancel;
    std::thread([lm, lib_idx, entry, cancel]() {
        auto* payload = new FetchResultPayload();
        payload->entry = entry;

        LibModule& m = lm->mutable_at(lib_idx);
        if (!m.session) lm->connect_all();

        uint8_t* data = nullptr;
        size_t size = 0;
        ChipError err{};
        lib_set_status(L"downloading...");
        if (m.fetch_fn(m.session, &entry, nullptr, nullptr, cancel, &data,
                       &size, &err)) {
            // сохраняем в кеш с расширением формата
            const std::wstring dir = cache_dir_for(m.info.id);
            std::string fname = entry.entry_id;
            for (char& c : fname)
                if (!std::isalnum((unsigned char)c) && c != '-' && c != '_')
                    c = '_';
            const std::wstring path = dir + L"\\" +
                utf8_to_wide(fname) + L"." + utf8_to_wide(entry.format);
            std::ofstream f(path.c_str(), std::ios::binary);
            if (f) {
                f.write(reinterpret_cast<const char*>(data),
                        std::streamsize(size));
                f.close();
                if (f.good()) payload->path = path;
            }
            m.free_buffer(m.session, data);
            if (payload->path.empty()) payload->error = "cache write failed";
        } else {
            payload->error = err.message;
        }
        PostMessageW(g.main_wnd, kMsgLibFetched, 0,
                     reinterpret_cast<LPARAM>(payload));
    }).detach();
}

static void lib_fill_list() {
    SendMessageW(g.lib_list, LB_RESETCONTENT, 0, 0);
    for (size_t i = 0; i < g.lib_results.size(); ++i) {
        const ChipLibEntry& e = g.lib_results[i];
        char row[512];
        std::snprintf(row, sizeof(row), "%.3u. %-28s - %.24s  [%s]",
                      unsigned(i + 1), e.title, e.author, e.format);
        const int idx = SendMessageA(g.lib_list, LB_ADDSTRING, 0,
                                     LPARAM(row));
        SendMessageA(g.lib_list, LB_SETITEMDATA, WPARAM(idx), LPARAM(i));
    }
}

static void lib_handle_fetched(FetchResultPayload* p) {
    if (p->path.empty()) {
        const std::wstring msg = utf8_to_wide(p->error);
        MessageBoxW(g.lib_wnd, msg.c_str(), L"ChipDeck - download",
                    MB_ICONWARNING);
        lib_set_status(L"download failed");
    } else {
        lib_set_status(L"adding to playlist...");
        const int idx = g.player->add_file(p->path);
        if (idx >= 0) {
            g.player->play(idx);
            refresh_list();
            update_tray();
            lib_set_status(L"playing");
        } else {
            lib_set_status(L"no engine for this format");
        }
    }
    delete p;
}

LRESULT CALLBACK lib_wnd_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case kIdLibGo: {
            wchar_t qtext[128] = {};
            GetWindowTextW(g.lib_edit, qtext, 128);
            const int sel = int(SendMessageW(g.lib_combo, CB_GETCURSEL, 0, 0));
            const uint32_t qtype =
                sel <= 0 ? CHP_QUERY_TOP
                         : (sel == 1 ? CHP_QUERY_SEARCH : CHP_QUERY_RANDOM);
            lib_start_query(0, qtype,
                            sel == 1 ? wide_to_utf8(qtext) : std::string());
            return 0;
        }
        case kIdLibList:
            if (HIWORD(wp) == LBN_DBLCLK) {
                int sel = int(SendMessageW(g.lib_list, LB_GETCURSEL, 0, 0));
                if (sel != LB_ERR && sel < int(g.lib_results.size())) {
                    lib_start_fetch(0, g.lib_results[size_t(sel)]);
                }
            }
            return 0;
        }
        break;
    case WM_CLOSE:
        ShowWindow(w, SW_HIDE);
        return 0;
    }
    return DefWindowProcW(w, msg, wp, lp);
}

void build_library_window(HINSTANCE inst) {
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = lib_wnd_proc;
    wc.hInstance = inst;
    wc.lpszClassName = L"ChipDeckLibrary";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassExW(&wc);

    const int cw = 480, ch = 360;
    const DWORD style =
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    const SIZE outer = outer_from_client(style, cw, ch);
    g.lib_wnd = CreateWindowExW(
        0, L"ChipDeckLibrary", L"ChipDeck - Library", style,
        CW_USEDEFAULT, CW_USEDEFAULT, outer.cx, outer.cy, nullptr, nullptr,
        inst, nullptr);

    g.lib_combo = CreateWindowExW(
        0, WC_COMBOBOXW, nullptr, WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
        8, 8, 110, 200, g.lib_wnd,
        reinterpret_cast<HMENU>(UINT_PTR(kIdLibCombo)), nullptr, nullptr);
    SendMessageW(g.lib_combo, CB_ADDSTRING, 0, LPARAM(L"Top rated"));
    SendMessageW(g.lib_combo, CB_ADDSTRING, 0, LPARAM(L"Search"));
    SendMessageW(g.lib_combo, CB_ADDSTRING, 0, LPARAM(L"Random"));
    SendMessageW(g.lib_combo, CB_SETCURSEL, 0, 0);
    SendMessageW(g.lib_combo, WM_SETFONT,
                 WPARAM(GetStockObject(DEFAULT_GUI_FONT)), TRUE);

    g.lib_edit = CreateWindowExW(WS_EX_CLIENTEDGE, WC_EDITW, nullptr,
                                 WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                 124, 9, 250, 22, g.lib_wnd,
                                 reinterpret_cast<HMENU>(UINT_PTR(kIdLibEdit)),
                                 nullptr, nullptr);
    SendMessageW(g.lib_edit, WM_SETFONT,
                 WPARAM(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    make_control(g.lib_wnd, WC_BUTTONW, L"Go", 0, 380, 8, 60, 24, kIdLibGo);

    g.lib_list = CreateWindowExW(
        WS_EX_CLIENTEDGE, WC_LISTBOXW, nullptr,
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_HASSTRINGS,
        8, 36, cw - 16, ch - 92, g.lib_wnd,
        reinterpret_cast<HMENU>(UINT_PTR(kIdLibList)), nullptr, nullptr);
    SendMessageW(g.lib_list, WM_SETFONT,
                 WPARAM(GetStockObject(DEFAULT_GUI_FONT)), TRUE);

    g.lib_status = CreateWindowExW(0, WC_STATICW, L"", WS_CHILD | WS_VISIBLE,
                                   8, ch - 48, cw - 16, 20, g.lib_wnd,
                                   nullptr, nullptr, nullptr);
    SendMessageW(g.lib_status, WM_SETFONT,
                 WPARAM(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
}

} // namespace

int run_ui(EngineManager& em, LibraryManager& lm, Player& player,
           bool start_minimized) {
    g.em = &em;
    g.lm = &lm;
    g.player = &player;

    const HINSTANCE inst = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = main_wnd_proc;
    wc.hInstance = inst;
    wc.lpszClassName = L"ChipDeckMain";
    RegisterClassExW(&wc);
    g.main_wnd = CreateWindowExW(0, L"ChipDeckMain", L"ChipDeck", WS_OVERLAPPED,
                                 CW_USEDEFAULT, CW_USEDEFAULT, 0, 0, nullptr,
                                 nullptr, inst, nullptr);

    // Лоток.
    g.tray.cbSize = sizeof(g.tray);
    g.tray.hWnd = g.main_wnd;
    g.tray.uID = 1;
    g.tray.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g.tray.uCallbackMessage = kMsgTray;
    g.tray.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcsncpy(g.tray.szTip, L"ChipDeck", 127);
    g.tray_added = Shell_NotifyIconW(NIM_ADD, &g.tray) != FALSE;

    build_player_window(inst);
    build_list_window(inst);
    if (lm.count() > 0) build_library_window(inst);

    if (!player.init(g.main_wnd, &em)) {
        MessageBoxW(nullptr, L"WASAPI init failed", L"ChipDeck", MB_ICONERROR);
        return 2;
    }

    if (!start_minimized) ShowWindow(g.player_wnd, SW_SHOW);

    SetTimer(g.player_wnd, 1, 100, nullptr); // обновление позиции
    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    player.shutdown();
    lm.disconnect_all();
    return 0;
}

} // namespace host
