#include "ui.hpp"

#include <commctrl.h>
#include <shellapi.h>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    try {
        dc::ComApartment com;
        INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_LISTVIEW_CLASSES | ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES};
        if (!InitCommonControlsEx(&controls)) return 1;
        const auto storage = dc::resolve_storage();
        auto settings = dc::Settings{};
        if (PRIMARYLANGID(GetUserDefaultUILanguage()) != LANG_GERMAN) settings.language = dc::Language::English;
        try { settings = dc::load_settings(storage); }
        catch (const dc::Failure& failure) {
            const auto message = std::wstring(dc::describe(failure.code(), settings.language)) + L"\n" + failure.detail();
            MessageBoxW(nullptr, message.c_str(), dc::product_name, MB_OK | MB_ICONWARNING);
        }
        int argc = 0;
        wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        struct Arguments { wchar_t** p; ~Arguments() { if (p) LocalFree(p); } } arguments{argv};
        std::wstring smoke;
        std::wstring image;
        if (argc == 3 && wcscmp(argv[1], L"--smoke-test") == 0) smoke = argv[2];
        else if (argc == 2) image = argv[1];
        dc::App app(instance, storage, settings);
        return app.run(show, image, smoke);
    } catch (const dc::Failure& failure) {
        const auto language = PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_GERMAN
            ? dc::Language::German : dc::Language::English;
        const auto message = std::wstring(dc::describe(failure.code(), language)) + L"\n" + failure.detail() +
            L"\n" + dc::system_message(failure.system_code());
        MessageBoxW(nullptr, message.c_str(), dc::product_name, MB_OK | MB_ICONERROR);
        return 1;
    } catch (...) {
        MessageBoxW(nullptr, L"Application startup failed.", dc::product_name, MB_OK | MB_ICONERROR);
        return 1;
    }
}
