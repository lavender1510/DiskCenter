#pragma once

#include "identity.hpp"
#include "optical.hpp"

#include <array>
#include <atomic>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

namespace dc {

enum class Page : unsigned { Home, Explorer, Burn, Images, Settings };
enum class Source { Directory, Image };

struct Location {
    Source source = Source::Directory;
    std::wstring path;
    std::optional<FileIdentity> identity;
    std::vector<IsoEntry> folders;
};

struct Listing {
    Location location;
    std::vector<Entry> entries;
};

struct Outcome {
    std::wstring message;
    std::optional<std::vector<Drive>> drives;
    std::shared_ptr<Listing> listing;
    std::optional<MediaInfo> media;
    std::wstring digest;
};

struct Palette {
    COLORREF canvas, card, ink, muted, border, sidebar, side_text, side_active, accent, accent_soft, hero_a, hero_b;
};

class App {
public:
    App(HINSTANCE instance, Storage storage, Settings settings);
    ~App();
    int run(int show, const std::wstring& initial_image, const std::wstring& smoke);
    const wchar_t* tr(const wchar_t* de, const wchar_t* en) const noexcept;
private:
    static LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wp, LPARAM lp);
    LRESULT handle(UINT message, WPARAM wp, LPARAM lp);
    void initialize();
    void layout();
    void update_text();
    void update_theme();
    void update_memory();
    void paint();
    void draw_button(const DRAWITEMSTRUCT& draw);
    void change_page(Page page);
    void command(WORD id, WORD event);
    LRESULT notify(NMHDR* header);
    void button(int id);
    void select_drive(int index);
    void populate_drives();
    void update_media();
    void update_controls();
    void update_list();
    void open_location(Location location);
    void open_selected();
    void export_selected();
    void copy_selected();
    void open_log() const;
    void smoke_test(const std::wstring& path);
    void path_action(int id);
    void save_preferences();
    void show_failure(const Failure& failure, bool dialog);
    bool confirm(const wchar_t* message) const;
    void refresh_drives();
    void tick();
    void done();
    void set_status(std::wstring value);
    void request_cancel(bool close_after);
    void start(std::wstring label, bool burning, bool automatic,
               std::function<Outcome(const Report&, const IsCancelled&)> work);
    std::wstring chosen_drive() const;
    std::wstring choose_file(bool save, const wchar_t* category, const wchar_t* filter,
                             const wchar_t* extension, const std::wstring& name = {}) const;
    std::wstring choose_folder() const;
    std::wstring profile_name() const;
    ImageOptions image_options() const;
    BurnOptions burn_options() const;
    int dp(int value) const noexcept;
    int selected(int id) const;
    void set(int id, const std::wstring& value);
    void put(int id, int x, int y, int width, int height);
    void combo(int id, std::initializer_list<const wchar_t*> texts, int index);
    HWND widget(int id) const;
    std::wstring get(int id) const;

    HINSTANCE instance_;
    HWND window_ = nullptr;
    std::array<HWND, 280> widgets_{};
    HBRUSH canvas_brush_ = nullptr;
    HBRUSH card_brush_ = nullptr;
    HFONT font_ = nullptr;
    HFONT card_title_ = nullptr;
    HFONT heading_ = nullptr;
    HFONT hero_title_ = nullptr;
    unsigned dpi_ = 96;
    Storage storage_;
    Settings settings_;
    bool accelerated_ = false;
    bool high_contrast_ = false;
    PerformancePolicy policy_;
    Palette palette_{};
    Page page_ = Page::Home;
    std::vector<Drive> drives_;
    int drive_index_ = -1;
    std::optional<MediaInfo> media_;
    std::shared_ptr<Listing> listing_;
    std::vector<int> visible_;
    std::wstring status_;
    std::wstring task_;
    std::wstring smoke_;
    ULONGLONG smoke_at_ = 0;
    ULONGLONG memory_at_ = 0;
    ULONGLONG filter_at_ = 0;
    bool refresh_pending_ = false;
    bool busy_ = false;
    bool burning_ = false;
    bool automatic_ = false;
    bool close_after_ = false;
    bool finishing_ = false;
    bool logging_failed_ = false;
    bool saw_result_ = false;
    std::thread worker_;
    std::atomic<bool> cancel_{false};
    std::atomic<bool> completed_{false};
    std::mutex progress_mutex_;
    Progress progress_;
    Outcome outcome_;
    std::exception_ptr error_;
};

}
