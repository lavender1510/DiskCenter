#include "ui.hpp"
#include "ui_ids.hpp"

#include <algorithm>
#include <array>
#include <commctrl.h>
#include <cstdio>
#include <cwchar>
#include <shellapi.h>
#include <shobjidl.h>
#include <system_error>

namespace dc {
namespace {

ComPtr<IFileDialog> file_dialog(bool save) {
    ComPtr<IFileDialog> dialog;
    check_hr(CoCreateInstance(save ? CLSID_FileSaveDialog : CLSID_FileOpenDialog, nullptr,
                              CLSCTX_INPROC_SERVER, IID_IFileDialog, reinterpret_cast<void**>(dialog.put())), Error::Open);
    return dialog;
}

std::wstring dialog_path(IFileDialog* dialog, HWND owner) {
    const auto result = dialog->Show(owner);
    if (result == HRESULT_FROM_WIN32(ERROR_CANCELLED)) return {};
    check_hr(result, Error::Open);
    ComPtr<IShellItem> item;
    check_hr(dialog->GetResult(item.put()), Error::Open);
    struct ShellPath { PWSTR value = nullptr; ~ShellPath() { CoTaskMemFree(value); } } path;
    check_hr(item->GetDisplayName(SIGDN_FILESYSPATH, &path.value), Error::Open);
    return path.value;
}

void open_with_shell(HWND owner, const std::wstring& path) {
    const auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(owner, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
    if (result <= 32) throw Failure(Error::Open, static_cast<std::uint32_t>(result), path);
}

bool checked(HWND box) { return SendMessageW(box, BM_GETCHECK, 0, 0) == BST_CHECKED; }

bool contains(const std::wstring& candidate, const std::wstring& filter) {
    if (filter.empty()) return true;
    for (std::size_t i = 0; i + filter.size() <= candidate.size(); ++i) {
        if (CompareStringOrdinal(candidate.data() + i, static_cast<int>(filter.size()), filter.c_str(),
                                 static_cast<int>(filter.size()), TRUE) == CSTR_EQUAL) return true;
    }
    return false;
}

const wchar_t* media_name(int type) {
    // IMAPI_MEDIA_PHYSICAL_TYPE values.
    static constexpr const wchar_t* names[] = {L"", L"CD-ROM", L"CD-R", L"CD-RW", L"DVD-ROM", L"DVD-RAM",
        L"DVD+R", L"DVD+RW", L"DVD+R DL", L"DVD-R", L"DVD-RW", L"DVD-R DL", L"",
        L"DVD+RW DL", L"HD DVD-ROM", L"HD DVD-R", L"HD DVD-RAM", L"BD-ROM", L"BD-R", L"BD-RE"};
    return type >= 0 && static_cast<unsigned>(type) < std::size(names) ? names[type] : L"";
}

std::wstring technical(const Failure& failure) {
    wchar_t code[72]{};
    swprintf(code, std::size(code), L"DC-%02u / 0x%08lX", static_cast<unsigned>(failure.code()),
             static_cast<unsigned long>(failure.system_code()));
    auto result = std::wstring(code);
    if (!failure.detail().empty()) result += L"  " + failure.detail();
    const auto system = system_message(failure.system_code());
    if (!system.empty()) result += L"\n" + system;
    return result;
}

}

std::wstring App::choose_file(bool save, const wchar_t* category, const wchar_t* filter,
                              const wchar_t* extension, const std::wstring& name) const {
    auto dialog = file_dialog(save);
    const COMDLG_FILTERSPEC types[] = {{category, filter}};
    check_hr(dialog->SetFileTypes(1, types), Error::Open);
    if (extension && *extension) check_hr(dialog->SetDefaultExtension(extension), Error::Open);
    FILEOPENDIALOGOPTIONS options{};
    check_hr(dialog->GetOptions(&options), Error::Open);
    check_hr(dialog->SetOptions((options | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST |
                                 (save ? FOS_NOREADONLYRETURN : FOS_FILEMUSTEXIST)) & ~FOS_OVERWRITEPROMPT), Error::Open);
    if (!name.empty()) check_hr(dialog->SetFileName(name.c_str()), Error::Open);
    return dialog_path(dialog.get(), window_);
}

std::wstring App::choose_folder() const {
    auto dialog = file_dialog(false);
    check_hr(dialog->SetOptions(FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST), Error::Open);
    return dialog_path(dialog.get(), window_);
}

void App::command(WORD id, WORD event) {
    if (id == HomeDrive || id == Burner) {
        if (event == CBN_SELCHANGE) select_drive(selected(id));
    } else if (id == ExploreFilter && event == EN_CHANGE) {
        filter_at_ = GetTickCount64() + 200;
    } else if (id >= NavHome && id < LastControl && (event == BN_CLICKED || event == 1)) {
        button(id);
    }
}

LRESULT App::notify(NMHDR* header) {
    if (header->idFrom != ExploreList) return 0;
    if (header->code == LVN_GETDISPINFOW) {
        auto& row = reinterpret_cast<NMLVDISPINFOW*>(header)->item;
        if (!(row.mask & LVIF_TEXT) || !row.pszText || row.cchTextMax < 1 || !listing_ ||
            row.iItem < 0 || static_cast<std::size_t>(row.iItem) >= visible_.size()) return 0;
        const auto& entry = listing_->entries[visible_[row.iItem]];
        std::wstring value;
        if (row.iSubItem == 0) value = entry.name;
        else if (row.iSubItem == 1) {
            if (!entry.safe) value = tr(L"Nicht lesbar", L"Unavailable");
            else value = entry.directory ? tr(L"Ordner", L"Folder") : tr(L"Datei", L"File");
        } else if (row.iSubItem == 2 && !entry.directory) value = format_size(entry.size, settings_.language);
        wcsncpy(row.pszText, value.c_str(), static_cast<std::size_t>(row.cchTextMax - 1));
        row.pszText[row.cchTextMax - 1] = L'\0';
    } else if (header->code == NM_DBLCLK || header->code == NM_RETURN) {
        if (!busy_) open_selected();
    } else if (header->code == LVN_ITEMCHANGED || header->code == LVN_ODSTATECHANGED) {
        update_controls();
    }
    return 0;
}

void App::button(int id) {
    if (id >= NavHome && id <= NavSettings) { change_page(static_cast<Page>(id - NavHome)); return; }
    if (id == Apply) { save_preferences(); return; }
    if (id == OpenLog) { open_log(); return; }
    if (id == Cancel) { request_cancel(false); return; }
    if (id == Refresh) {
        if (busy_) refresh_pending_ = true;
        else refresh_drives();
        return;
    }
    if (busy_) return;
    if (id == TileExplore) { change_page(Page::Explorer); id = ExploreDrive; }
    if (id == TileImage) { change_page(Page::Images); id = ImageCreate; }
    if (id == TileData) { change_page(Page::Burn); id = BurnFolder; }
    if (id == TileCopy) { change_page(Page::Images); id = ImageSaveDisc; }
    if (id == TileBurn) { change_page(Page::Burn); id = BurnIso; }
    if (id == TileHash) { change_page(Page::Images); id = ImageHash; }
    if (id == ImageBrowse) { change_page(Page::Explorer); id = ExploreIso; }
    switch (id) {
    case ExploreOpen: open_selected(); break;
    case ExploreCopy: copy_selected(); break;
    case ExploreCatalog: export_selected(); break;
    case ExploreDrive: open_location({Source::Directory, chosen_drive(), {}, {}}); break;
    case ExploreUp: {
        if (!listing_) break;
        auto location = listing_->location;
        if (location.source == Source::Image) {
            if (location.folders.empty()) break;
            location.folders.pop_back();
        } else {
            const auto parent = parent_path(location.path);
            if (parent.empty() || parent == location.path) break;
            location.path = parent;
        }
        open_location(std::move(location));
        break;
    }
    case Analyze: {
        const auto drive = chosen_drive();
        const auto success = tr(L"Medium geprüft.", L"Media inspection finished.");
        start(tr(L"Medium prüfen", L"Inspecting media"), false, false, [drive, success](const Report&, const IsCancelled& stop) {
            check_cancel(stop);
            Outcome outcome;
            outcome.media = inspect_media(drive);
            outcome.message = success;
            return outcome;
        });
        break;
    }
    case Eject: {
        const auto drive = chosen_drive();
        const auto success = tr(L"Auswurf angefordert.", L"Eject requested.");
        start(tr(L"Laufwerk auswerfen", L"Ejecting drive"), false, false, [drive, success](const Report&, const IsCancelled& stop) {
            check_cancel(stop);
            eject(drive);
            Outcome outcome;
            outcome.message = success;
            outcome.drives = optical_drives();
            return outcome;
        });
        break;
    }
    default: path_action(id); break;
    }
}

void App::path_action(int id) {
    auto iso = [&](bool save, const std::wstring& name = std::wstring{}) {
        return choose_file(save, tr(L"Disc-Image", L"Disc image"), L"*.iso", L"iso", name);
    };
    const auto buffer = policy_.buffer_bytes;
    if (id == ExploreFolder) {
        auto folder = choose_folder();
        if (!folder.empty()) open_location({Source::Directory, std::move(folder), {}, {}});
    } else if (id == ExploreIso) {
        auto input = iso(false);
        if (!input.empty()) open_location({Source::Image, std::move(input), {}, {}});
    } else if (id == ImageHash) {
        const auto expected = normalized_sha256(get(HashExpected));
        auto input = iso(false);
        if (input.empty()) return;
        set(Hash, L"");
        const auto success = tr(L"Dateisystem erkannt und SHA-256 berechnet.", L"File system recognized and SHA-256 computed.");
        const auto match = tr(L"SHA-256 stimmt überein.", L"SHA-256 matches.");
        const auto mismatch = tr(L"SHA-256 weicht vom Vergleichswert ab.", L"SHA-256 differs from the expected value.");
        start(tr(L"Image prüfen", L"Checking image"), false, false, [=](const Report& report, const IsCancelled& stop) {
            InputFile file(input);
            validate_data_image(file.size(), file.reader());
            Outcome outcome;
            outcome.digest = sha256_file(input, buffer, report, stop);
            outcome.message = expected.empty() ? success : expected == outcome.digest ? match : mismatch;
            return outcome;
        });
    } else if (id == ImageCreate) {
        const auto source = choose_folder();
        if (source.empty()) return;
        const auto output = iso(true, L"data-disc.iso");
        if (output.empty()) return;
        const auto options = image_options();
        const auto success = std::wstring(tr(L"Image erstellt. ", L"Image created. ")) + file_name(output);
        start(tr(L"ISO erstellen", L"Creating ISO"), false, false, [=](const Report& report, const IsCancelled& stop) {
            create_image(source, output, options, buffer, report, stop);
            Outcome outcome;
            outcome.message = success;
            return outcome;
        });
    } else if (id == ImageSaveDisc) {
        const auto drive = chosen_drive();
        const auto output = iso(true, L"disc-copy.iso");
        if (output.empty()) return;
        const auto success = std::wstring(tr(L"Disc als ISO gesichert. ", L"Disc saved as ISO. ")) + file_name(output);
        start(tr(L"Disc lesen", L"Reading disc"), false, false, [=](const Report& report, const IsCancelled& stop) {
            save_data_disc(drive, output, buffer, report, stop);
            Outcome outcome;
            outcome.message = success;
            return outcome;
        });
    } else if (id == BurnFolder || id == BurnIso) {
        const auto options = burn_options();
        const auto source = id == BurnFolder ? choose_folder() : iso(false);
        if (source.empty()) return;
        const auto question = options.drive + L"\n" + source + L"\n\n" +
            tr(L"Auf das eingelegte, leere Medium schreiben und die Disc abschließen?",
               L"Write to the loaded blank medium and finalize the disc?");
        if (!confirm(question.c_str())) return;
        const auto image = image_options();
        const auto success = options.verify
            ? tr(L"Disc geschrieben und vollständig geprüft.", L"Disc written and fully verified.")
            : tr(L"Disc geschrieben.", L"Disc written.");
        start(tr(L"Disc brennen", L"Burning disc"), true, false, [=](const Report& report, const IsCancelled& stop) {
            if (id == BurnFolder) burn_folder(source, image, options, report, stop);
            else burn_image(source, options, report, stop);
            Outcome outcome;
            outcome.message = success;
            return outcome;
        });
    }
}

std::wstring App::chosen_drive() const {
    if (drive_index_ < 0 || static_cast<std::size_t>(drive_index_) >= drives_.size()) throw Failure(Error::RecorderMissing);
    return drives_[drive_index_].root;
}

void App::select_drive(int index) {
    if (index != drive_index_) media_.reset();
    drive_index_ = index;
    SendMessageW(widget(HomeDrive), CB_SETCURSEL, drive_index_, 0);
    SendMessageW(widget(Burner), CB_SETCURSEL, drive_index_, 0);
    update_media();
    update_controls();
}

void App::populate_drives() {
    for (const auto id : {HomeDrive, Burner}) {
        SendMessageW(widget(id), CB_RESETCONTENT, 0, 0);
        for (const auto& drive : drives_) {
            const auto name = drive.root.substr(0, 2) + (drive.label.empty() ? L"" : L"  " + drive.label);
            SendMessageW(widget(id), CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.c_str()));
        }
    }
    select_drive(drive_index_);
}

void App::update_media() {
    std::wstring state;
    if (drive_index_ < 0 || static_cast<std::size_t>(drive_index_) >= drives_.size()) {
        state = tr(L"Kein optisches Laufwerk erkannt", L"No optical drive detected");
    } else {
        const auto& drive = drives_[drive_index_];
        if (drive.state == MediaState::Mounted) {
            state = drive.filesystem;
            if (drive.capacity) state += L"  ·  " + format_size(drive.capacity, settings_.language);
        } else if (drive.state == MediaState::NoMedia) state = tr(L"Kein Medium eingelegt", L"No medium loaded");
        else state = tr(L"Kein eingebundenes Dateisystem", L"No mounted file system");
    }
    set(HomeDriveState, state);
    std::wstring burn_state = state;
    const auto old_speed = selected(Speed);
    combo(Speed, {tr(L"Vom Laufwerk wählen lassen", L"Use drive default")}, 0);
    if (media_) {
        burn_state = media_name(media_->type);
        if (!burn_state.empty()) burn_state += L"  ·  ";
        burn_state += media_->blank ? tr(L"Leer", L"Blank") : tr(L"Nicht leer", L"Not blank");
        burn_state += L"  ·  " + format_size(media_->free_sectors * sector_size, settings_.language) + tr(L" frei", L" free");
        for (const auto speed : media_->write_speeds) {
            const auto text = format_size(std::uint64_t(speed) * sector_size, settings_.language) + L"/s";
            SendMessageW(widget(Speed), CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
        }
        if (old_speed >= 0 && static_cast<std::size_t>(old_speed) <= media_->write_speeds.size())
            SendMessageW(widget(Speed), CB_SETCURSEL, old_speed, 0);
    }
    set(BurnMedia, burn_state);
}

void App::update_controls() {
    const bool drive = drive_index_ >= 0 && static_cast<std::size_t>(drive_index_) < drives_.size();
    const bool can_write = drive && (!media_ || (media_->blank && media_->free_sectors > 0));
    const int active_row = ListView_GetNextItem(widget(ExploreList), -1, LVNI_SELECTED);
    const Entry* entry = listing_ && active_row >= 0 && static_cast<std::size_t>(active_row) < visible_.size()
        ? &listing_->entries[visible_[active_row]] : nullptr;
    for (const auto id : {HomeDrive, Burner}) EnableWindow(widget(id), drive && !busy_);
    for (const auto id : {TileExplore, TileCopy, ExploreDrive, Analyze, Eject, ImageSaveDisc})
        EnableWindow(widget(id), drive && !busy_);
    for (const auto id : {TileData, TileBurn, BurnFolder, BurnIso}) EnableWindow(widget(id), can_write && !busy_);
    for (const auto id : {TileImage, TileHash, ImageCreate, ImageBrowse, ImageHash, ExploreFolder, ExploreIso, Refresh})
        EnableWindow(widget(id), !busy_);
    const bool can_go_up = listing_ && (listing_->location.source == Source::Image
        ? !listing_->location.folders.empty() : !parent_path(listing_->location.path).empty());
    EnableWindow(widget(ExploreUp), can_go_up && !busy_);
    EnableWindow(widget(ExploreOpen), entry && entry->safe && !busy_ && (entry->directory || listing_->location.source == Source::Directory));
    EnableWindow(widget(ExploreCopy), entry && entry->safe && !entry->directory && !busy_);
    EnableWindow(widget(ExploreCatalog), listing_ && !busy_);
    EnableWindow(widget(Cancel), busy_ && !automatic_ && !cancel_.load());
}

void App::update_list() {
    const auto filter = get(ExploreFilter);
    ListView_SetItemState(widget(ExploreList), -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    visible_.clear();
    if (listing_) {
        visible_.reserve(listing_->entries.size());
        for (std::size_t i = 0; i < listing_->entries.size(); ++i) {
            if (contains(listing_->entries[i].name, filter)) visible_.push_back(static_cast<int>(i));
        }
    }
    ListView_SetItemCountEx(widget(ExploreList), static_cast<int>(visible_.size()), 0);
    InvalidateRect(widget(ExploreList), nullptr, FALSE);
    set(ExploreCount, std::to_wstring(visible_.size()) + tr(L" Einträge", L" items"));
    auto path = listing_ ? listing_->location.path : std::wstring{};
    if (listing_) for (const auto& folder : listing_->location.folders) path += L" / " + folder.name;
    set(ExplorePath, path);
    update_controls();
}

void App::open_location(Location location) {
    const auto success = tr(L"Inhalt geladen.", L"Contents loaded.");
    start(tr(L"Inhalt lesen", L"Reading contents"), false, false,
        [location = std::move(location), success](const Report&, const IsCancelled& stop) mutable {
            auto listing = std::make_shared<Listing>();
            listing->location = std::move(location);
            auto& loc = listing->location;
            if (loc.source == Source::Directory) {
                listing->entries = read_directory(loc.path, stop);
            } else {
                InputFile file(loc.path);
                if (loc.identity && !(*loc.identity == file.identity())) throw Failure(Error::Read, 0, loc.path);
                loc.identity = file.identity();
                const IsoVolume disc(file.size(), file.reader());
                const auto& parent = loc.folders.empty() ? disc.root() : loc.folders.back();
                if (!loc.folders.empty() && parent.offset == disc.root().offset) throw Failure(Error::InvalidImage);
                auto items = disc.directory(parent, file.reader(), stop);
                listing->entries.reserve(items.size());
                for (auto& item : items) {
                    listing->entries.push_back({std::move(item.name), item.size, item.directory, item.contiguous, std::move(item)});
                }
            }
            Outcome outcome;
            outcome.listing = std::move(listing);
            outcome.message = success;
            return outcome;
        });
}

void App::open_selected() {
    const auto row = ListView_GetNextItem(widget(ExploreList), -1, LVNI_SELECTED);
    if (!listing_ || row < 0 || static_cast<std::size_t>(row) >= visible_.size()) return;
    const auto& item = listing_->entries[visible_[row]];
    if (!item.safe) throw Failure(Error::UnsafePath, 0, item.name);
    auto loc = listing_->location;
    if (loc.source == Source::Directory) {
        const auto path = combine(loc.path, item.name);
        if (item.directory) {
            loc.path = path;
            open_location(std::move(loc));
        } else {
            InputFile check(path);
            open_with_shell(window_, path);
        }
    } else if (item.directory) {
        if (loc.folders.size() >= 64 || std::any_of(loc.folders.begin(), loc.folders.end(),
            [&](const IsoEntry& folder) { return folder.offset == item.iso.offset; }))
            throw Failure(Error::InvalidImage, 0, item.name);
        auto dir = item.iso;
        dir.name = item.name;
        loc.folders.push_back(std::move(dir));
        open_location(std::move(loc));
    }
}

void App::copy_selected() {
    const auto row = ListView_GetNextItem(widget(ExploreList), -1, LVNI_SELECTED);
    if (!listing_ || row < 0 || static_cast<std::size_t>(row) >= visible_.size()) return;
    const auto item = listing_->entries[visible_[row]];
    if (!item.safe || item.directory) throw Failure(Error::UnsafePath, 0, item.name);
    const auto output = choose_file(true, tr(L"Alle Dateien", L"All files"), L"*.*", L"", item.name);
    if (output.empty()) return;
    const auto location = listing_->location;
    const auto buffer = policy_.buffer_bytes;
    const auto success = std::wstring(tr(L"Datei gespeichert. ", L"File saved. ")) + file_name(output);
    start(tr(L"Datei kopieren", L"Copying file"), false, false, [=](const Report& report, const IsCancelled& stop) {
        if (location.source == Source::Image) {
            const InputFile image(location.path);
            if (!location.identity || !(*location.identity == image.identity())) throw Failure(Error::Read, 0, location.path);
            copy_range(image, item.iso.offset, item.iso.size, output, buffer, report, stop);
        } else {
            const InputFile source(combine(location.path, item.name));
            copy_range(source, 0, source.size(), output, buffer, report, stop);
        }
        Outcome outcome;
        outcome.message = success;
        return outcome;
    });
}

void App::export_selected() {
    if (!listing_) return;
    const auto output = choose_file(true, tr(L"CSV-Datei", L"CSV file"), L"*.csv", L"csv", L"disc-catalog.csv");
    if (output.empty()) return;
    const auto listing = listing_;
    const auto success = std::wstring(tr(L"Inhaltsverzeichnis exportiert. ", L"Catalog exported. ")) + file_name(output);
    start(tr(L"Inhaltsverzeichnis exportieren", L"Exporting catalog"), false, false,
        [=](const Report&, const IsCancelled& stop) {
            write_catalog(output, listing->entries, stop);
            Outcome outcome;
            outcome.message = success;
            return outcome;
        });
}

ImageOptions App::image_options() const {
    return {get(VolumeName), static_cast<FileSystem>(std::clamp(selected(Format), 0, 2))};
}

BurnOptions App::burn_options() const {
    const int speed = selected(Speed);
    const auto selected_speed = media_ && speed > 0 && static_cast<std::size_t>(speed) <= media_->write_speeds.size()
        ? media_->write_speeds[speed - 1] : -1L;
    return {chosen_drive(), checked(widget(Verify)), selected_speed};
}

void App::save_preferences() {
    Settings next = settings_;
    const unsigned font_sizes[] = {90, 100, 110, 125};
    const unsigned buffers[] = {0, 1, 2, 4, 8, 16};
    const unsigned thresholds[] = {4, 6, 8, 12, 16};
    next.language = static_cast<Language>(std::clamp(selected(PreferencesLanguage), 0, 1));
    next.profile = static_cast<ProfileChoice>(std::clamp(selected(PreferencesProfile), 0, 2));
    next.theme = static_cast<Theme>(std::clamp(selected(PreferencesTheme), 0, 5));
    next.accent = static_cast<unsigned>(std::clamp(selected(PreferencesAccent), 0, 3));
    next.font_percent = font_sizes[std::clamp(selected(PreferencesFont), 0, 3)];
    next.buffer_mib = buffers[std::clamp(selected(PreferencesBuffer), 0, 5)];
    next.modern_ram_gib = thresholds[std::clamp(selected(PreferencesThreshold), 0, 4)];
    next.compact = checked(widget(PreferencesCompact));
    next.verify = checked(widget(PreferencesVerify));
    save_settings(storage_, next);
    settings_ = next;
    policy_ = performance_policy(settings_, system_resources(accelerated_));
    update_theme();
    update_text();
    layout();
    set_status(tr(L"Einstellungen gespeichert.", L"Settings saved."));
    logging_failed_ = !log_event(storage_, status_);
}

void App::open_log() const {
    const auto log = storage_.log();
    if (GetFileAttributesW(win_path(log).c_str()) == INVALID_FILE_ATTRIBUTES) win_failure(Error::Open, log);
    const auto argument = L"\"" + log + L"\"";
    const auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(window_, L"open", L"notepad.exe", argument.c_str(), nullptr, SW_SHOWNORMAL));
    if (result <= 32) throw Failure(Error::Open, static_cast<std::uint32_t>(result), log);
}

bool App::confirm(const wchar_t* message) const {
    return MessageBoxW(window_, message, product_name, MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES;
}

void App::show_failure(const Failure& failure, bool dialog) {
    const auto label = describe(failure.code(), settings_.language);
    auto detail = technical(failure);
    set_status(label);
    logging_failed_ = !log_event(storage_, std::wstring(label) + L"  " + detail);
    if (dialog) {
        auto message = std::wstring(label);
        if (failure.code() == Error::Imapi || failure.code() == Error::Verification) {
            message += tr(L"\nPrüfen Sie das Medium und den Laufwerkszustand vor einem neuen Versuch.",
                          L"Check the medium and drive before trying again.");
        }
        message += L"\n\n" + detail;
        MessageBoxW(window_, message.c_str(), product_name, MB_OK | MB_ICONERROR);
    }
}

void App::refresh_drives() {
    refresh_pending_ = false;
    start(tr(L"Laufwerke erkennen", L"Detecting drives"), false, true,
          [](const Report&, const IsCancelled& stop) {
        Outcome outcome;
        outcome.drives = optical_drives(stop);
        return outcome;
    });
}

void App::start(std::wstring label, bool burning, bool automatic,
                std::function<Outcome(const Report&, const IsCancelled&)> work) {
    if (busy_ || finishing_) return;
    cancel_ = false;
    completed_ = false;
    progress_ = {};
    outcome_ = {};
    error_ = nullptr;
    task_ = std::move(label);
    burning_ = burning;
    automatic_ = automatic;
    try {
        worker_ = std::thread([this, work = std::move(work)] {
            try {
                ComApartment com;
                DWORD previous = 0;
                SetThreadErrorMode(SEM_FAILCRITICALERRORS, &previous);
                auto report = [this](Progress progress) {
                    const std::lock_guard<std::mutex> lock(progress_mutex_);
                    progress_ = progress;
                };
                const auto cancelled = [this] { return cancel_.load(std::memory_order_relaxed); };
                outcome_ = work(report, cancelled);
            } catch (...) { error_ = std::current_exception(); }
            completed_.store(true, std::memory_order_release);
            PostMessageW(window_, job_finished_message, 0, 0);
        });
        busy_ = true;
    } catch (const std::system_error&) {
        throw Failure(Error::Unexpected, 0, L"Worker thread");
    }
    if (!automatic_ || !saw_result_) set_status(task_);
    update_controls();
}

void App::request_cancel(bool close_after) {
    if (!busy_) return;
    if (burning_ && !cancel_ &&
        !confirm(tr(L"Schreibvorgang abbrechen? Die Disc kann danach unbrauchbar sein.",
                    L"Cancel writing? The disc may be unusable afterward."))) return;
    cancel_ = true;
    close_after_ = close_after_ || close_after;
    EnableWindow(widget(Cancel), FALSE);
    set_status(tr(L"Abbruch angefordert", L"Cancellation requested"));
}

void App::done() {
    if (!busy_ || !completed_.load(std::memory_order_acquire)) return;
    worker_.join();
    busy_ = false;
    finishing_ = true;
    struct FinishGuard {
        bool& flag;
        ~FinishGuard() { flag = false; }
    } guard{finishing_};
    const bool burned = burning_;
    burning_ = false;
    if (close_after_) { DestroyWindow(window_); return; }
    if (error_) {
        try { std::rethrow_exception(error_); }
        catch (const Failure& error) { show_failure(error, !automatic_ && error.code() != Error::Cancelled); }
        catch (const std::bad_alloc&) { show_failure(Failure(Error::NoMemory), !automatic_); }
        catch (...) { show_failure(Failure(Error::Unexpected), !automatic_); }
    } else {
        if (outcome_.drives) {
            const auto current = drive_index_ >= 0 && static_cast<std::size_t>(drive_index_) < drives_.size()
                ? drives_[drive_index_].root : std::wstring{};
            drives_ = std::move(*outcome_.drives);
            const auto match = std::find_if(drives_.begin(), drives_.end(), [&](const Drive& drive) { return drive.root == current; });
            drive_index_ = drives_.empty() ? -1 : match == drives_.end() ? 0 : static_cast<int>(match - drives_.begin());
            media_.reset();
            populate_drives();
        }
        if (outcome_.media) {
            media_ = std::move(outcome_.media);
            update_media();
        }
        if (outcome_.listing) {
            listing_ = std::move(outcome_.listing);
            set(ExploreFilter, L"");
            update_list();
        }
        if (!outcome_.digest.empty()) set(Hash, outcome_.digest);
        if (!outcome_.message.empty()) {
            set_status(outcome_.message);
            saw_result_ = true;
            logging_failed_ = !log_event(storage_, outcome_.message);
        } else if (!saw_result_)
            set_status(drives_.empty() ? tr(L"Bereit · Kein optisches Laufwerk", L"Ready · No optical drive") : tr(L"Bereit", L"Ready"));
    }
    outcome_ = {};
    error_ = nullptr;
    SendMessageW(widget(ProgressBar), PBM_SETMARQUEE, FALSE, 0);
    SendMessageW(widget(ProgressBar), PBM_SETPOS, 0, 0);
    if (burned) refresh_pending_ = true;
    update_controls();
}

void App::tick() {
    if (finishing_) return;
    const auto now = GetTickCount64();
    done();
    if (close_after_ && !busy_) return;
    if (!busy_ && refresh_pending_) refresh_drives();
    if (filter_at_ && now >= filter_at_) { filter_at_ = 0; update_list(); }
    if (now >= memory_at_) { memory_at_ = now + 2500; update_memory(); }
    if (!smoke_.empty() && !busy_ && now >= smoke_at_) {
        auto path = std::move(smoke_);
        smoke_.clear();
        smoke_test(path);
        return;
    }
    if (!busy_ || cancel_ || automatic_) return;
    Progress current;
    { const std::lock_guard<std::mutex> lock(progress_mutex_); current = progress_; }
    const auto marquee = current.total == 0;
    SendMessageW(widget(ProgressBar), PBM_SETMARQUEE, marquee, 45);
    if (marquee) {
        if (current.stage == Stage::Verifying) set_status(tr(L"Disc wird gelesen und geprüft", L"Reading and verifying disc"));
        if (current.stage == Stage::Finalizing) set_status(tr(L"Disc wird abgeschlossen", L"Finalizing disc"));
        return;
    }
    const auto fraction = std::clamp(double(current.done) / double(current.total), 0.0, 1.0);
    const auto stage = current.stage == Stage::Verifying ? tr(L"Prüfen", L"Verifying") :
        current.stage == Stage::Writing ? tr(L"Schreiben", L"Writing") : tr(L"Lesen", L"Reading");
    auto line = std::wstring(stage) + L"   " + std::to_wstring(static_cast<unsigned>(fraction * 100)) + L" %  ·  " +
        format_size(current.done, settings_.language) + L" / " + format_size(current.total, settings_.language);
    if (current.bytes_per_second) line += L"  ·  " + format_size(current.bytes_per_second, settings_.language) + L"/s";
    if (current.seconds_remaining) line += L"  ·  " + std::to_wstring(current.seconds_remaining) + L" s";
    set_status(std::move(line));
    SendMessageW(widget(ProgressBar), PBM_SETPOS, static_cast<WPARAM>(fraction * 1000), 0);
}

void App::set_status(std::wstring value) {
    status_ = std::move(value);
    set(Status, logging_failed_ ? status_ + tr(L"  ·  Protokoll nicht beschreibbar", L"  ·  Log is not writable") : status_);
}

void App::update_memory() {
    const auto resources = system_resources(accelerated_);
    const auto previous = policy_;
    policy_ = performance_policy(settings_, resources);
    if (previous.active != policy_.active || previous.gradients != policy_.gradients)
        InvalidateRect(window_, nullptr, FALSE);
    const auto memory = process_memory();
    const auto language = settings_.language;
    set(Memory, L"Working Set " + format_size(memory.working_set, language) + L"  ·  Private Bytes " +
        format_size(memory.private_bytes, language) + L"  ·  " + tr(L"Puffer ", L"Buffer ") +
        format_size(policy_.buffer_bytes, language));
}

void App::smoke_test(const std::wstring& path) {
    const auto memory = process_memory(true);
    if (!IsWindowVisible(window_) || !IsWindow(widget(NavHome)) || !IsWindow(widget(ExploreList)))
        throw Failure(Error::Unexpected, 0, L"Window smoke test");
    const auto result = std::string("version=") + std::string(std::begin(version), std::end(version) - 1) +
        "\narch=" + (sizeof(void*) == 8 ? "x64" : "x86") +
        "\nworking_set=" + std::to_string(memory.working_set) + "\nprivate_bytes=" + std::to_string(memory.private_bytes) +
        "\nvirtual_bytes=" + std::to_string(memory.virtual_bytes) + "\noptical_drives=" + std::to_string(drives_.size()) + "\n";
    AtomicOutput output(path);
    output.write(result.data(), result.size());
    output.commit();
    DestroyWindow(window_);
}

}
