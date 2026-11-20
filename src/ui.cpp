#include "ui.hpp"
#include "ui_ids.hpp"

#include <algorithm>
#include <array>
#include <commctrl.h>
#include <cwchar>
#include <dbt.h>
#include <shellapi.h>
#include <windowsx.h>

namespace dc {
namespace {

constexpr wchar_t application_class[] = L"DiscCenter.MainWindow";
constexpr UINT update_timer = 1;

COLORREF blend(COLORREF a, COLORREF b, unsigned percent) {
    const auto channel = [percent](unsigned x, unsigned y) { return (x * (100 - percent) + y * percent) / 100; };
    return RGB(channel(GetRValue(a), GetRValue(b)), channel(GetGValue(a), GetGValue(b)), channel(GetBValue(a), GetBValue(b)));
}

Palette colors(Theme theme, unsigned accent, bool high_contrast) {
    const COLORREF accents[] = {RGB(17, 113, 103), RGB(44, 91, 160), RGB(111, 64, 147), RGB(156, 80, 26)};
    const auto tint = accents[std::min<unsigned>(accent, 3)];
    if (high_contrast) return {GetSysColor(COLOR_WINDOW), GetSysColor(COLOR_WINDOW), GetSysColor(COLOR_WINDOWTEXT),
        GetSysColor(COLOR_WINDOWTEXT), GetSysColor(COLOR_WINDOWTEXT), GetSysColor(COLOR_WINDOW), GetSysColor(COLOR_WINDOWTEXT),
        GetSysColor(COLOR_WINDOW), GetSysColor(COLOR_HIGHLIGHT), GetSysColor(COLOR_WINDOW), GetSysColor(COLOR_WINDOW),
        GetSysColor(COLOR_WINDOW)};
    if (theme == Theme::Dark) return {RGB(19, 26, 32), RGB(29, 39, 47), RGB(235, 242, 246), RGB(170, 187, 194),
        RGB(55, 70, 80), RGB(11, 18, 25), RGB(196, 210, 218), RGB(37, 53, 64), blend(tint, RGB(213, 255, 247), 55),
        RGB(43, 60, 66), RGB(21, 40, 50), RGB(20, 69, 66)};
    const auto bg = theme == Theme::Classic ? RGB(232, 238, 246) : theme == Theme::Aero ? RGB(234, 246, 250) :
        theme == Theme::LowMemory ? RGB(248, 248, 246) : RGB(243, 247, 248);
    return {bg, RGB(255, 255, 255), RGB(25, 44, 53), RGB(77, 100, 110), RGB(216, 229, 232),
        RGB(14, 31, 39), RGB(186, 209, 214), RGB(37, 61, 70), tint, blend(tint, RGB(255, 255, 255), 89),
        theme == Theme::Classic ? RGB(27, 53, 87) : RGB(15, 49, 54),
        theme == Theme::Aero ? RGB(31, 105, 150) : blend(tint, RGB(15, 49, 54), 30)};
}

void fill(HDC dc, RECT rect, COLORREF color) {
    SetDCBrushColor(dc, color);
    FillRect(dc, &rect, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
}

void outline(HDC dc, RECT rect, COLORREF color) {
    SetDCBrushColor(dc, color);
    FrameRect(dc, &rect, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
}

void text(HDC dc, const wchar_t* label, RECT rect, HFONT font, COLORREF color, UINT alignment = DT_LEFT) {
    const auto previous = SelectObject(dc, font);
    SetTextColor(dc, color);
    SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, label, -1, &rect, alignment | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    SelectObject(dc, previous);
}

void circle(HDC dc, const RECT& rect, COLORREF line, COLORREF surface, int width = 1) {
    const auto pen = CreatePen(PS_SOLID, width, line);
    if (!pen) return;
    const auto old_pen = SelectObject(dc, pen);
    const auto old_brush = SelectObject(dc, GetStockObject(DC_BRUSH));
    SetDCBrushColor(dc, surface);
    Ellipse(dc, rect.left, rect.top, rect.right, rect.bottom);
    SelectObject(dc, old_brush);
    SelectObject(dc, old_pen);
    DeleteObject(pen);
}

bool system_dark() {
    DWORD light = 1;
    DWORD bytes = sizeof(light);
    return RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                       L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &light, &bytes) == ERROR_SUCCESS && light == 0;
}

struct TileText {
    const wchar_t* mark;
    const wchar_t* title_de;
    const wchar_t* title_en;
    const wchar_t* description_de;
    const wchar_t* description_en;
};

TileText tile_copy(int id) {
    switch (id) {
    case TileExplore: return {L"↗", L"Disc öffnen", L"Open disc", L"Dateien durchsuchen", L"Browse disc files"};
    case TileImage: return {L"+", L"ISO erstellen", L"Create ISO", L"Aus einem lokalen Ordner", L"From a local folder"};
    case TileData: return {L"•", L"Daten brennen", L"Burn data", L"Ordner auf eine Disc schreiben", L"Write a folder to disc"};
    case TileCopy: return {L"↓", L"Disc sichern", L"Save disc", L"Eine Datenspur als ISO speichern", L"Save a data track as ISO"};
    case TileBurn: return {L"→", L"ISO brennen", L"Burn ISO", L"Image auf eine Disc schreiben", L"Write an image to disc"};
    case TileHash: return {L"#", L"Image prüfen", L"Check image", L"ISO-Struktur und SHA-256", L"ISO structure and SHA-256"};
    default: return {L"", L"", L"", L"", L""};
    }
}

bool custom_button(int id) {
    return (id >= NavHome && id <= NavSettings) || (id >= TileExplore && id <= TileHash) || id == Apply;
}

Page owner(int id) {
    if (id <= TileHash) return Page::Home;
    if (id <= ExploreCount) return Page::Explorer;
    if (id <= BurnIso) return Page::Burn;
    if (id <= Hash) return Page::Images;
    return Page::Settings;
}

}

App::App(HINSTANCE instance, Storage storage, Settings settings)
    : instance_(instance), storage_(std::move(storage)), settings_(settings), accelerated_(display_accelerated()),
      policy_(performance_policy(settings_, system_resources(accelerated_))) {}

App::~App() {
    cancel_ = true;
    if (worker_.joinable()) worker_.join();
    if (IsWindow(window_)) DestroyWindow(window_);
}

const wchar_t* App::tr(const wchar_t* de, const wchar_t* en) const noexcept {
    return settings_.language == Language::German ? de : en;
}

int App::dp(int n) const noexcept { return MulDiv(n, static_cast<int>(dpi_), 96); }
HWND App::widget(int id) const { return widgets_.at(static_cast<std::size_t>(id)); }

int App::run(int show, const std::wstring& initial_image, const std::wstring& smoke) {
    const auto screen = GetDC(nullptr);
    if (screen) { dpi_ = std::clamp(GetDeviceCaps(screen, LOGPIXELSX), 96, 288); ReleaseDC(nullptr, screen); }
    WNDCLASSEXW cls{};
    cls.cbSize = sizeof(cls);
    cls.hInstance = instance_;
    cls.lpfnWndProc = procedure;
    cls.lpszClassName = application_class;
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    cls.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(101));
    cls.hIconSm = cls.hIcon;
    if (!RegisterClassExW(&cls)) win_failure(Error::Unexpected, application_class);

    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int width = std::min(dp(1120), static_cast<int>(work.right - work.left));
    const int height = std::min(dp(768), static_cast<int>(work.bottom - work.top));
    const auto window = CreateWindowExW(WS_EX_CONTROLPARENT, application_class, product_name,
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, (work.right + work.left - width) / 2, (work.bottom + work.top - height) / 2,
        width, height, nullptr, nullptr, instance_, this);
    if (!window) win_failure(Error::Unexpected, application_class);
    ShowWindow(window, show);
    UpdateWindow(window);
    SetTimer(window, update_timer, 200, nullptr);
    logging_failed_ = !log_event(storage_, std::wstring(product_name) + L" " + version + L" | " + profile_name());
    smoke_ = smoke;
    if (!smoke_.empty()) smoke_at_ = GetTickCount64() + 900;
    if (!initial_image.empty()) {
        change_page(Page::Explorer);
        open_location({Source::Image, initial_image, {}, {}});
        refresh_pending_ = true;
    } else refresh_drives();

    ACCEL shortcuts[] = {
        {FVIRTKEY | FCONTROL, '1', NavHome}, {FVIRTKEY | FCONTROL, '2', NavExplorer},
        {FVIRTKEY | FCONTROL, '3', NavBurn}, {FVIRTKEY | FCONTROL, '4', NavImages},
        {FVIRTKEY | FCONTROL, '5', NavSettings}, {FVIRTKEY, VK_F5, Refresh}
    };
    const auto keys = CreateAcceleratorTableW(shortcuts, static_cast<int>(std::size(shortcuts)));
    MSG msg{};
    int state = 0;
    try {
        while ((state = GetMessageW(&msg, nullptr, 0, 0)) > 0) {
            if (keys && TranslateAcceleratorW(window, keys, &msg)) continue;
            if (IsDialogMessageW(window, &msg)) continue;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    } catch (...) {
        cancel_ = true;
        if (worker_.joinable()) worker_.join();
        if (keys) DestroyAcceleratorTable(keys);
        throw;
    }
    cancel_ = true;
    if (worker_.joinable()) worker_.join();
    if (keys) DestroyAcceleratorTable(keys);
    return state < 0 ? 1 : static_cast<int>(msg.wParam);
}

LRESULT CALLBACK App::procedure(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    auto app = reinterpret_cast<App*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        app = static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        app->window_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    }
    if (!app) return DefWindowProcW(window, message, wp, lp);
    try {
        return app->handle(message, wp, lp);
    } catch (const std::bad_alloc&) {
        MessageBoxW(window, describe(Error::NoMemory, app->settings_.language), product_name, MB_OK | MB_ICONERROR);
    } catch (const Failure& failure) {
        app->show_failure(failure, true);
    } catch (...) {
        MessageBoxW(window, describe(Error::Unexpected, app->settings_.language), product_name, MB_OK | MB_ICONERROR);
    }
    return message == WM_CREATE ? -1 : 0;
}

LRESULT App::handle(UINT message, WPARAM wp, LPARAM lp) {
    switch (message) {
    case WM_CREATE: initialize(); return 0;
    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lp);
        info->ptMinTrackSize.x = dp(795);
        info->ptMinTrackSize.y = dp(584);
        return 0;
    }
    case WM_SIZE: if (wp != SIZE_MINIMIZED) layout(); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: paint(); return 0;
    case WM_DRAWITEM: draw_button(*reinterpret_cast<DRAWITEMSTRUCT*>(lp)); return TRUE;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT: {
        const auto dc = reinterpret_cast<HDC>(wp);
        SetTextColor(dc, palette_.ink);
        const int id = GetDlgCtrlID(reinterpret_cast<HWND>(lp));
        const bool card = id == HomeDriveLabel || id == HomeDriveState || id == BurnerLabel || id == BurnMedia ||
            id == ImageLabel || id == FormatLabel || id == SpeedLabel || id == VolumeName ||
            id == HashExpected || id == HashExpectedLabel || id == Hash || id == HashLabel ||
            id == LanguageLabel || id == ProfileLabel || id == ThemeLabel || id == AccentLabel || id == FontLabel ||
            id == BufferLabel || id == ThresholdLabel || id == PreferencesCompact || id == PreferencesVerify || id == Verify;
        SetBkColor(dc, card ? palette_.card : palette_.canvas);
        return reinterpret_cast<LRESULT>(card ? card_brush_ : canvas_brush_);
    }
    case WM_COMMAND: command(LOWORD(wp), HIWORD(wp)); return 0;
    case WM_NOTIFY: return notify(reinterpret_cast<NMHDR*>(lp));
    case WM_TIMER: if (wp == update_timer) tick(); return 0;
    case job_finished_message: done(); return 0;
    case WM_DROPFILES: {
        struct DroppedFile { HDROP value; ~DroppedFile() { DragFinish(value); } } file{reinterpret_cast<HDROP>(wp)};
        if (busy_) return 0;
        if (DragQueryFileW(file.value, 0xffffffff, nullptr, 0) != 1) throw Failure(Error::Input);
        const auto length = DragQueryFileW(file.value, 0, nullptr, 0);
        std::wstring path(length + 1, L'\0');
        if (!length || DragQueryFileW(file.value, 0, path.data(), length + 1) != length) throw Failure(Error::Input);
        path.resize(length);
        const auto attrs = GetFileAttributesW(win_path(path).c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES) win_failure(Error::Open, path);
        const bool folder = (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
        if (!folder && (path.size() < 4 || CompareStringOrdinal(path.c_str() + path.size() - 4, 4, L".iso", 4, TRUE) != CSTR_EQUAL))
            throw Failure(Error::UnsupportedImage, 0, path);
        change_page(Page::Explorer);
        open_location({folder ? Source::Directory : Source::Image, std::move(path), {}, {}});
        return 0;
    }
    case WM_DEVICECHANGE:
        if (wp == DBT_DEVICEARRIVAL || wp == DBT_DEVICEREMOVECOMPLETE || wp == DBT_DEVNODES_CHANGED) {
            refresh_pending_ = true;
            media_.reset();
        }
        return TRUE;
    case WM_SETTINGCHANGE:
    case WM_SYSCOLORCHANGE: update_theme(); layout(); return 0;
    case WM_CLOSE:
        if (busy_) request_cancel(true);
        else DestroyWindow(window_);
        return 0;
    case WM_DESTROY:
        KillTimer(window_, update_timer);
        for (auto handle : {font_, card_title_, heading_, hero_title_}) if (handle) DeleteObject(handle);
        if (canvas_brush_) DeleteObject(canvas_brush_);
        if (card_brush_) DeleteObject(card_brush_);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window_, message, wp, lp);
}

void App::initialize() {
    const auto child = [&](int id, const wchar_t* type, DWORD style, DWORD ex = 0) {
        auto window = CreateWindowExW(ex, type, L"", WS_CHILD | style, 0, 0, 1, 1, window_,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance_, nullptr);
        if (!window) win_failure(Error::Unexpected, type);
        widgets_.at(static_cast<std::size_t>(id)) = window;
    };
    for (int id = NavHome; id < LastControl; ++id) {
        if (id == Status || id == HomeDriveLabel || id == HomeDriveState || id == ActionLabel ||
            id == ExploreFilterLabel || id == ExploreCount || id == BurnerLabel || id == BurnMedia ||
            id == ImageLabel || id == FormatLabel || id == SpeedLabel || id == HashExpectedLabel || id == HashLabel ||
            id == LanguageLabel || id == ProfileLabel || id == ThemeLabel || id == AccentLabel ||
            id == FontLabel || id == BufferLabel || id == ThresholdLabel || id == Memory) {
            child(id, L"STATIC", SS_LEFTNOWORDWRAP | SS_NOPREFIX);
        } else if (id == ProgressBar) {
            child(id, PROGRESS_CLASSW, PBS_SMOOTH | PBS_MARQUEE);
            SendMessageW(widget(id), PBM_SETRANGE32, 0, 1000);
        } else if (id == HomeDrive || id == Burner || id == Format || id == Speed ||
                   id == PreferencesLanguage || id == PreferencesProfile || id == PreferencesTheme ||
                   id == PreferencesAccent || id == PreferencesFont || id == PreferencesBuffer || id == PreferencesThreshold) {
            child(id, L"COMBOBOX", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL | CBS_HASSTRINGS);
        } else if (id == ExplorePath || id == ExploreFilter || id == VolumeName || id == HashExpected || id == Hash) {
            child(id, L"EDIT", WS_TABSTOP | ES_AUTOHSCROLL | ((id == Hash || id == ExplorePath) ? ES_READONLY : 0), WS_EX_CLIENTEDGE);
        } else if (id == ExploreList) {
            child(id, WC_LISTVIEWW, WS_TABSTOP | LVS_REPORT | LVS_OWNERDATA | LVS_SHOWSELALWAYS | LVS_SINGLESEL, WS_EX_CLIENTEDGE);
            ListView_SetExtendedListViewStyle(widget(id), LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
            for (int n = 0; n < 3; ++n) {
                LVCOLUMNW column{};
                column.mask = LVCF_WIDTH | LVCF_TEXT | LVCF_FMT;
                column.cx = dp(100);
                column.pszText = const_cast<wchar_t*>(L"");
                column.fmt = n == 2 ? LVCFMT_RIGHT : LVCFMT_LEFT;
                ListView_InsertColumn(widget(id), n, &column);
            }
        } else if (id == Verify || id == PreferencesCompact || id == PreferencesVerify) {
            child(id, L"BUTTON", WS_TABSTOP | BS_AUTOCHECKBOX);
        } else {
            child(id, L"BUTTON", WS_TABSTOP | (custom_button(id) ? BS_OWNERDRAW : BS_PUSHBUTTON));
        }
    }
    DragAcceptFiles(window_, TRUE);
    set(VolumeName, L"DATA_DISC");
    SendMessageW(widget(HashExpected), EM_SETLIMITTEXT, 192, 0);
    SendMessageW(widget(VolumeName), EM_SETLIMITTEXT, 32, 0);
    SendMessageW(widget(ExploreFilter), EM_SETLIMITTEXT, 160, 0);
    update_theme();
    update_text();
    change_page(Page::Home);
    SetFocus(widget(NavHome));
}

void App::set(int id, const std::wstring& value) { SetWindowTextW(widget(id), value.c_str()); }

std::wstring App::get(int id) const {
    const auto size = GetWindowTextLengthW(widget(id));
    std::wstring text(size + 1, L'\0');
    const auto count = GetWindowTextW(widget(id), text.data(), size + 1);
    text.resize(count);
    return text;
}

int App::selected(int id) const { return static_cast<int>(SendMessageW(widget(id), CB_GETCURSEL, 0, 0)); }

void App::combo(int id, std::initializer_list<const wchar_t*> items, int index) {
    SendMessageW(widget(id), CB_RESETCONTENT, 0, 0);
    for (const auto* label : items) SendMessageW(widget(id), CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
    SendMessageW(widget(id), CB_SETCURSEL, index, 0);
}

void App::update_text() {
    set(NavHome, tr(L"Übersicht", L"Overview")); set(NavExplorer, L"Disc-Explorer");
    set(NavBurn, tr(L"Brennstudio", L"Burn Studio")); set(NavImages, L"Image-Manager");
    set(NavSettings, tr(L"Einstellungen", L"Settings"));
    set(Cancel, tr(L"Abbrechen", L"Cancel"));
    set(HomeDriveLabel, tr(L"Optisches Laufwerk", L"Optical drive"));
    set(Refresh, tr(L"Aktualisieren", L"Refresh"));
    set(ActionLabel, tr(L"Disc-Werkzeuge", L"Disc tools"));
    for (int id = TileExplore; id <= TileHash; ++id) {
        const auto tile = tile_copy(id);
        set(id, tr(tile.title_de, tile.title_en));
    }
    set(ExploreDrive, tr(L"Laufwerk", L"Drive")); set(ExploreFolder, tr(L"Ordner öffnen", L"Open folder"));
    set(ExploreIso, tr(L"ISO öffnen", L"Open ISO")); set(ExploreUp, tr(L"Nach oben", L"Up"));
    set(ExploreFilterLabel, tr(L"Filtern", L"Filter"));
    set(ExploreOpen, tr(L"Öffnen", L"Open")); set(ExploreCopy, tr(L"Datei speichern", L"Save file"));
    set(ExploreCatalog, tr(L"CSV exportieren", L"Export CSV"));
    const wchar_t* columns[] = {L"Name", tr(L"Typ", L"Type"), tr(L"Größe", L"Size")};
    for (int i = 0; i < 3; ++i) {
        LVCOLUMNW column{};
        column.mask = LVCF_TEXT;
        column.pszText = const_cast<wchar_t*>(columns[i]);
        ListView_SetColumn(widget(ExploreList), i, &column);
    }
    set(BurnerLabel, tr(L"Ziellaufwerk", L"Target drive")); set(Analyze, tr(L"Medium prüfen", L"Inspect media"));
    set(Eject, tr(L"Auswerfen", L"Eject")); set(ImageLabel, tr(L"Disc-Name", L"Disc label"));
    set(FormatLabel, tr(L"Dateisystem", L"File system")); set(SpeedLabel, tr(L"Schreibgeschwindigkeit", L"Write speed"));
    set(Verify, tr(L"Disc nach dem Schreiben vollständig prüfen", L"Fully verify disc after writing"));
    set(BurnFolder, tr(L"Ordner brennen…", L"Burn folder…")); set(BurnIso, tr(L"ISO brennen…", L"Burn ISO…"));
    set(ImageCreate, tr(L"ISO aus Ordner erstellen…", L"Create ISO from folder…"));
    set(ImageSaveDisc, tr(L"Disc als ISO sichern…", L"Save disc as ISO…"));
    set(ImageBrowse, tr(L"ISO durchsuchen…", L"Browse ISO…"));
    set(ImageHash, tr(L"Image prüfen und SHA-256 berechnen…", L"Check image and compute SHA-256…"));
    set(HashExpectedLabel, tr(L"SHA-256 zum Abgleichen (optional)", L"Expected SHA-256 (optional)"));
    set(HashLabel, tr(L"SHA-256 des zuletzt geprüften Images", L"SHA-256 of the last checked image"));
    set(LanguageLabel, tr(L"Sprache", L"Language")); set(ProfileLabel, tr(L"Leistungsprofil", L"Performance profile"));
    set(ThemeLabel, tr(L"Darstellung", L"Appearance")); set(AccentLabel, tr(L"Akzentfarbe", L"Accent color"));
    set(FontLabel, tr(L"Schriftgröße", L"Text size")); set(BufferLabel, tr(L"Transferpuffer", L"Transfer buffer"));
    set(ThresholdLabel, tr(L"Modern ab installiertem RAM", L"Installed RAM for Modern"));
    set(PreferencesCompact, tr(L"Kompakte Liste", L"Compact list"));
    set(PreferencesVerify, tr(L"Brennprüfung standardmäßig aktivieren", L"Enable burn verification by default"));
    set(Apply, tr(L"Speichern", L"Save")); set(OpenLog, tr(L"Protokoll öffnen", L"Open log"));
    combo(PreferencesLanguage, {L"Deutsch", L"English"}, static_cast<int>(settings_.language));
    combo(PreferencesProfile, {tr(L"Automatisch", L"Automatic"), L"Classic / Low-Memory", L"Modern / Performance"},
          static_cast<int>(settings_.profile));
    combo(PreferencesTheme, {L"System", tr(L"Hell", L"Light"), tr(L"Dunkel", L"Dark"), L"Classic",
          L"Frutiger Aero", L"Low-Memory"}, static_cast<int>(settings_.theme));
    combo(PreferencesAccent, {tr(L"Petrol", L"Teal"), tr(L"Blau", L"Blue"), tr(L"Violett", L"Violet"),
          tr(L"Kupfer", L"Copper")}, static_cast<int>(settings_.accent));
    combo(PreferencesFont, {L"90 %", L"100 %", L"110 %", L"125 %"},
          settings_.font_percent <= 90 ? 0 : settings_.font_percent <= 100 ? 1 : settings_.font_percent <= 110 ? 2 : 3);
    combo(PreferencesBuffer, {tr(L"Profilstandard", L"Profile default"), L"1 MiB", L"2 MiB", L"4 MiB", L"8 MiB", L"16 MiB"},
          settings_.buffer_mib == 0 ? 0 : settings_.buffer_mib <= 1 ? 1 : settings_.buffer_mib <= 2 ? 2 :
          settings_.buffer_mib <= 4 ? 3 : settings_.buffer_mib <= 8 ? 4 : 5);
    combo(PreferencesThreshold, {L"4 GiB", L"6 GiB", L"8 GiB", L"12 GiB", L"16 GiB"},
          settings_.modern_ram_gib <= 4 ? 0 : settings_.modern_ram_gib <= 6 ? 1 : settings_.modern_ram_gib <= 8 ? 2 :
          settings_.modern_ram_gib <= 12 ? 3 : 4);
    SendMessageW(widget(PreferencesCompact), BM_SETCHECK, settings_.compact, 0);
    SendMessageW(widget(PreferencesVerify), BM_SETCHECK, settings_.verify, 0);
    SendMessageW(widget(Verify), BM_SETCHECK, settings_.verify, 0);
    combo(Format, {L"ISO 9660 + Joliet", L"UDF", L"ISO 9660 + Joliet + UDF"}, std::max(0, selected(Format)));
    update_media();
    update_memory();
    update_list();
    InvalidateRect(window_, nullptr, FALSE);
}

void App::update_theme() {
    HIGHCONTRASTW contrast{};
    contrast.cbSize = sizeof(contrast);
    high_contrast_ = SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0) &&
                     (contrast.dwFlags & HCF_HIGHCONTRASTON);
    auto theme = settings_.theme;
    if (theme == Theme::System) theme = system_dark() ? Theme::Dark : Theme::Light;
    palette_ = colors(theme, settings_.accent, high_contrast_);
    if (canvas_brush_) DeleteObject(canvas_brush_);
    canvas_brush_ = CreateSolidBrush(palette_.canvas);
    if (card_brush_) DeleteObject(card_brush_);
    card_brush_ = CreateSolidBrush(palette_.card);
    const auto make_font = [&](int size, int weight) {
        return CreateFontW(-dp(MulDiv(size, static_cast<int>(settings_.font_percent), 100)), 0, 0, 0, weight, FALSE, FALSE,
            FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    };
    auto body = make_font(settings_.compact ? 13 : 15, FW_NORMAL);
    if (!body) body = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    for (int i = NavHome; i < LastControl; ++i) SendMessageW(widget(i), WM_SETFONT, reinterpret_cast<WPARAM>(body), TRUE);
    if (font_) DeleteObject(font_);
    font_ = body;
    if (card_title_) DeleteObject(card_title_);
    if (heading_) DeleteObject(heading_);
    if (hero_title_) DeleteObject(hero_title_);
    card_title_ = make_font(16, FW_SEMIBOLD);
    heading_ = make_font(30, FW_SEMIBOLD);
    hero_title_ = make_font(33, FW_SEMIBOLD);
    ListView_SetBkColor(widget(ExploreList), palette_.card);
    ListView_SetTextBkColor(widget(ExploreList), palette_.card);
    ListView_SetTextColor(widget(ExploreList), palette_.ink);
    InvalidateRect(window_, nullptr, FALSE);
}

void App::put(int id, int x, int y, int width, int height) {
    SetWindowPos(widget(id), nullptr, dp(x), dp(y), dp(std::max(width, 1)), dp(std::max(height, 1)),
                 SWP_NOZORDER | SWP_NOACTIVATE);
}

void App::layout() {
    RECT rect{};
    GetClientRect(window_, &rect);
    const auto width = MulDiv(rect.right, 96, static_cast<int>(dpi_));
    const auto height = MulDiv(rect.bottom, 96, static_cast<int>(dpi_));
    const int side = width < 900 ? 181 : 211;
    const int left = side + 27;
    const int space = width - left - 27;
    const int footer = height - 54;
    const int small = height < 625 ? 1 : 0;
    for (int id = NavHome; id <= NavSettings; ++id) put(id, 14, 112 + (id - NavHome) * 45, side - 28, 40);
    put(Status, left, footer + 8, space - 112, 20);
    put(ProgressBar, left, footer + 33, space - 112, 6);
    put(Cancel, left + space - 106, footer + 10, 106, 29);
    if (page_ == Page::Home) {
        const int hero = small ? 126 : 157;
        const int drive = 103 + hero + 13;
        const int tiles = drive + (small ? 92 : 103);
        const int tile_height = small ? 61 : 81;
        const int tile_width = (space - 22) / 3;
        const int selector = space > 650 ? 215 : 166;
        put(HomeDriveLabel, left + 13, drive + 7, space - 25, 22);
        put(HomeDrive, left + 13, drive + 33, selector, 160);
        put(HomeDriveState, left + 24 + selector, drive + 37, space - 150 - selector, 19);
        put(Refresh, left + space - 121, drive + 31, 108, 27);
        put(ActionLabel, left, tiles - 25, space, 20);
        for (int i = 0; i < 6; ++i) put(TileExplore + i, left + (i % 3) * (tile_width + 11),
            tiles + (i / 3) * (tile_height + 10), i % 3 == 2 ? space - 2 * (tile_width + 11) : tile_width, tile_height);
    } else if (page_ == Page::Explorer) {
        put(ExploreDrive, left, 104, 95, 30);
        put(ExploreFolder, left + 102, 104, 122, 30);
        put(ExploreIso, left + 231, 104, 105, 30);
        put(ExploreUp, left + space - 100, 104, 100, 30);
        const int filter = 155;
        put(ExplorePath, left, 146, space - filter - 69, 27);
        put(ExploreFilterLabel, left + space - filter - 60, 151, 52, 23);
        put(ExploreFilter, left + space - filter, 146, filter, 27);
        put(ExploreList, left, 184, space, footer - 231);
        ListView_SetColumnWidth(widget(ExploreList), 0, dp(space - 247));
        ListView_SetColumnWidth(widget(ExploreList), 1, dp(100));
        ListView_SetColumnWidth(widget(ExploreList), 2, dp(120));
        put(ExploreOpen, left, footer - 39, 89, 28);
        put(ExploreCopy, left + 97, footer - 39, 127, 28);
        put(ExploreCatalog, left + 232, footer - 39, 135, 28);
        put(ExploreCount, left + 380, footer - 34, space - 380, 22);
    } else if (page_ == Page::Burn) {
        put(BurnerLabel, left + 17, 119, space - 30, 21);
        put(Burner, left + 17, 144, space - 274, 155);
        put(Analyze, left + space - 248, 144, 132, 28);
        put(Eject, left + space - 107, 144, 90, 28);
        put(BurnMedia, left + 17, 183, space - 34, 24);
        const int column = (space - 50) / 2;
        put(ImageLabel, left + 17, 243, column, 24);
        put(FormatLabel, left + 33 + column, 243, column, 24);
        put(VolumeName, left + 17, 271, column, 28);
        put(Format, left + 33 + column, 271, column, 150);
        put(SpeedLabel, left + 17, 313, column, 24);
        put(Speed, left + 17, 338, column, 178);
        put(Verify, left + 17, 376, space - 34, 30);
        put(BurnFolder, left, 432, 156, 33);
        put(BurnIso, left + 167, 432, 136, 33);
    } else if (page_ == Page::Images) {
        const int column = (space - 53) / 2;
        put(ImageCreate, left + 18, 123, column, 44);
        put(ImageSaveDisc, left + 35 + column, 123, column, 44);
        put(ImageBrowse, left + 18, 181, column, 44);
        put(ImageHash, left + 35 + column, 181, column, 44);
        put(HashExpectedLabel, left + 18, 260, space - 36, 24);
        put(HashExpected, left + 18, 289, space - 36, 30);
        put(HashLabel, left + 18, 334, space - 36, 24);
        put(Hash, left + 18, 362, space - 36, 30);
    } else if (page_ == Page::Settings) {
        const int column = (space - 55) / 2;
        const int ids[][2] = {{LanguageLabel, PreferencesLanguage}, {ProfileLabel, PreferencesProfile},
            {ThemeLabel, PreferencesTheme}, {AccentLabel, PreferencesAccent}, {FontLabel, PreferencesFont},
            {BufferLabel, PreferencesBuffer}, {ThresholdLabel, PreferencesThreshold}};
        for (int i = 0; i < 7; ++i) {
            const int x = left + 17 + (i % 2) * (column + 21);
            const int y = 116 + (i / 2) * 61;
            put(ids[i][0], x, y, column, 21);
            put(ids[i][1], x, y + 23, column, 172);
        }
        put(PreferencesCompact, left + 17, 365, column, 28);
        put(PreferencesVerify, left + 38 + column, 365, column, 28);
        put(Memory, left + 17, 412, space - 34, 24);
        put(Apply, left, 453, 110, 32); put(OpenLog, left + 119, 453, 139, 32);
    }
    InvalidateRect(window_, nullptr, FALSE);
}

void App::change_page(Page page) {
    page_ = page;
    layout();
    for (int i = NavHome; i < LastControl; ++i) {
        const bool visible = i <= Cancel || owner(i) == page_;
        ShowWindow(widget(i), visible ? SW_SHOWNA : SW_HIDE);
        if (i <= NavSettings) InvalidateRect(widget(i), nullptr, FALSE);
    }
    update_controls();
}

std::wstring App::profile_name() const {
    return policy_.active == ProfileChoice::Modern ? L"Modern / Performance" : L"Classic / Low-Memory";
}

void App::paint() {
    PAINTSTRUCT paint{};
    const auto dc = BeginPaint(window_, &paint);
    RECT full{};
    GetClientRect(window_, &full);
    const int w = MulDiv(full.right, 96, static_cast<int>(dpi_));
    const int h = MulDiv(full.bottom, 96, static_cast<int>(dpi_));
    const int side = w < 900 ? 181 : 211;
    const int left = side + 27, right = w - 27;
    const auto r = [&](int x, int y, int end_x, int end_y) { return RECT{dp(x), dp(y), dp(end_x), dp(end_y)}; };
    fill(dc, full, palette_.canvas);
    fill(dc, r(0, 0, side, h), palette_.sidebar);
    fill(dc, r(side - 1, 0, side, h), palette_.border);
    circle(dc, r(18, 25, 56, 63), palette_.side_text, palette_.side_active);
    circle(dc, r(28, 35, 46, 53), palette_.side_text, palette_.sidebar);
    circle(dc, r(34, 41, 40, 47), palette_.side_text, palette_.side_text);
    text(dc, product_name, r(64, 23, side - 8, 54), card_title_, palette_.side_text);
    text(dc, tr(L"Optische Medien", L"Optical media"), r(64, 51, side - 8, 77), font_, palette_.side_text);
    fill(dc, r(15, h - 73, side - 15, h - 72), palette_.side_active);
    text(dc, (std::wstring(L"v") + version).c_str(), r(18, h - 64, side - 12, h - 43), font_, palette_.side_text);
    text(dc, sizeof(void*) == 8 ? L"x64" : L"x86", r(18, h - 42, side - 12, h - 17), font_, palette_.side_text);

    const auto heading = get(static_cast<int>(NavHome) + static_cast<int>(page_));
    text(dc, heading.c_str(), r(left, 18, right, 64), heading_, palette_.ink);
    const wchar_t* subtitle = L"";
    switch (page_) {
    case Page::Home: subtitle = tr(L"Laufwerke und Aufgaben im Überblick", L"Drives and tasks at a glance"); break;
    case Page::Explorer: subtitle = tr(L"Dateien auf Discs, in ISO-Images und in Ordnern", L"Files on discs, in ISO images, and in folders"); break;
    case Page::Burn: subtitle = tr(L"Daten-Discs erstellen und den Schreibvorgang prüfen", L"Create data discs and verify the write"); break;
    case Page::Images: subtitle = tr(L"Disc-Images erstellen, sichern und prüfen", L"Create, save, and check disc images"); break;
    case Page::Settings: subtitle = tr(L"Darstellung und Ressourcenverbrauch", L"Appearance and resource usage"); break;
    }
    text(dc, subtitle, r(left, 65, right, 90), font_, palette_.muted);
    if (page_ == Page::Home) {
        const auto hero = h < 625 ? 126 : 157;
        const auto hero_rect = r(left, 103, right, 103 + hero);
        fill(dc, hero_rect, palette_.hero_a);
        if (!high_contrast_ && (policy_.gradients || settings_.theme == Theme::Aero) &&
            settings_.theme != Theme::LowMemory && settings_.theme != Theme::Classic) {
            const auto color = [](COLORREF c, LONG x, LONG y) {
                return TRIVERTEX{x, y, static_cast<COLOR16>(GetRValue(c) << 8), static_cast<COLOR16>(GetGValue(c) << 8),
                                  static_cast<COLOR16>(GetBValue(c) << 8), 0};
            };
            TRIVERTEX vertices[] = {color(palette_.hero_a, hero_rect.left, hero_rect.top), color(palette_.hero_b, hero_rect.right, hero_rect.bottom)};
            GRADIENT_RECT gradient{0, 1};
            GradientFill(dc, vertices, 2, &gradient, 1, GRADIENT_FILL_RECT_H);
        }
        const auto light = high_contrast_ ? palette_.ink : RGB(241, 251, 248);
        const auto subdued = high_contrast_ ? palette_.ink : RGB(187, 218, 219);
        const auto edge = high_contrast_ ? palette_.ink : blend(palette_.hero_b, RGB(174, 235, 230), 40);
        const int cx = right - 100, cy = 103 + hero / 2, radius = std::min(58, hero / 2 - 9);
        circle(dc, r(cx - radius, cy - radius, cx + radius, cy + radius), edge, palette_.hero_b);
        circle(dc, r(cx - radius + 9, cy - radius + 9, cx + radius - 9, cy + radius - 9), edge, palette_.hero_b);
        circle(dc, r(cx - 23, cy - 23, cx + 23, cy + 23), edge, palette_.hero_a);
        circle(dc, r(cx - 9, cy - 9, cx + 9, cy + 9), edge, palette_.hero_b);
        text(dc, tr(L"Aktives Leistungsprofil", L"Active performance profile"), r(left + 22, 115, right - 172, 139), font_, subdued);
        text(dc, profile_name().c_str(), r(left + 22, 143, right - 155, 189), hero_title_, light);
        const auto reason = settings_.profile == ProfileChoice::Automatic
            ? tr(L"Automatisch anhand der Systemressourcen gewählt", L"Selected automatically from system resources")
            : tr(L"Manuell in den Einstellungen gewählt", L"Selected manually in Settings");
        text(dc, reason, r(left + 22, 191, right - 155, 217), font_, subdued);
        const auto block = r(left, 116 + hero, right, 185 + hero);
        fill(dc, block, palette_.card); outline(dc, block, palette_.border);
    } else {
        const auto card = [&](int top, int bottom) { const auto a = r(left, top, right, bottom); fill(dc, a, palette_.card); outline(dc, a, palette_.border); };
        if (page_ == Page::Burn) { card(106, 221); card(232, 415); }
        if (page_ == Page::Images) { card(106, 242); card(250, 409); }
        if (page_ == Page::Settings) { card(106, 400); }
    }
    fill(dc, r(left, h - 55, right, h - 54), palette_.border);
    EndPaint(window_, &paint);
}

void App::draw_button(const DRAWITEMSTRUCT& draw) {
    const auto id = static_cast<int>(draw.CtlID);
    const auto dc = draw.hDC;
    auto bounds = draw.rcItem;
    const auto pressed = (draw.itemState & ODS_SELECTED) != 0;
    const auto disabled = (draw.itemState & ODS_DISABLED) != 0;
    const auto caption = get(id);
    if (id >= NavHome && id <= NavSettings) {
        const auto active = id == static_cast<int>(NavHome) + static_cast<int>(page_);
        fill(dc, bounds, active || pressed ? palette_.side_active : palette_.sidebar);
        if (active) {
            auto mark = bounds;
            mark.right = mark.left + dp(3);
            fill(dc, mark, high_contrast_ ? palette_.side_text : RGB(89, 203, 183));
        }
        bounds.left += dp(15); bounds.right -= dp(8);
        text(dc, caption.c_str(), bounds, active ? card_title_ : font_, palette_.side_text);
    } else if (id >= TileExplore && id <= TileHash) {
        fill(dc, bounds, pressed ? palette_.accent_soft : palette_.card);
        outline(dc, bounds, palette_.border);
        const auto copy = tile_copy(id);
        const auto mark_size = dp(29);
        const auto y = (bounds.top + bounds.bottom - mark_size) / 2;
        const RECT mark{bounds.left + dp(11), y, bounds.left + dp(11) + mark_size, y + mark_size};
        fill(dc, mark, palette_.accent_soft);
        text(dc, copy.mark, mark, card_title_, palette_.accent, DT_CENTER);
        const auto line = (bounds.bottom + bounds.top) / 2;
        const int x = bounds.left + dp(49);
        text(dc, caption.c_str(), {x, line - dp(26), bounds.right - dp(8), line + dp(1)}, card_title_,
             disabled ? palette_.muted : palette_.ink);
        text(dc, tr(copy.description_de, copy.description_en), {x, line, bounds.right - dp(8), line + dp(23)},
             font_, palette_.muted);
    } else {
        const auto bg = pressed ? blend(palette_.accent, palette_.ink, 23) : palette_.accent;
        fill(dc, bounds, bg);
        text(dc, caption.c_str(), bounds, card_title_, GetRValue(bg) > 120 && GetGValue(bg) > 130
             ? RGB(12, 22, 25) : RGB(255, 255, 255), DT_CENTER);
    }
    if ((draw.itemState & ODS_FOCUS) && !(draw.itemState & ODS_NOFOCUSRECT)) {
        auto focus = draw.rcItem;
        InflateRect(&focus, -dp(4), -dp(4));
        DrawFocusRect(dc, &focus);
    }
}

}
