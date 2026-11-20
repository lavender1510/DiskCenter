#include "core.hpp"

#include <algorithm>
#include <charconv>
#include <cwctype>
#include <iomanip>
#include <sstream>
#include <utility>

namespace dc {

Failure::Failure(Error code, std::uint32_t system_code, std::wstring detail)
    : std::runtime_error("DiscCenter operation failed"), code_(code),
      system_code_(system_code), detail_(std::move(detail)) {}

PerformancePolicy performance_policy(const Settings& settings, const SystemResources& resources) noexcept {
    constexpr std::uint64_t gib = 1024ULL * 1024 * 1024;
    const auto threshold = std::clamp(settings.modern_ram_gib, 4u, 64u) * gib;
    const bool pressure = resources.available < gib / 2 || resources.memory_load >= 92;
    const bool suitable = resources.installed >= threshold - gib / 8 && resources.available >= 2 * gib &&
        resources.process_64_bit && resources.processors >= 4 && resources.windows_major >= 10 &&
        resources.display_accelerated && resources.memory_load < 80;
    const auto active = settings.profile == ProfileChoice::Automatic
        ? (suitable ? ProfileChoice::Modern : ProfileChoice::Classic) : settings.profile;
    const auto fallback = active == ProfileChoice::Modern ? 4u : 1u;
    auto limit = settings.buffer_mib ? std::clamp(settings.buffer_mib, 1u, 16u) : fallback;
    if (pressure || !resources.process_64_bit) limit = std::min(limit, pressure ? 1u : 4u);
    return {active, limit * std::size_t(1024 * 1024), active == ProfileChoice::Modern && !pressure, pressure};
}

std::string serialize_settings(const Settings& s) {
    std::ostringstream out;
    out << "[DiscCenter]\n"
        << "language=" << static_cast<unsigned>(s.language) << "\n"
        << "profile=" << static_cast<unsigned>(s.profile) << "\n"
        << "theme=" << static_cast<unsigned>(s.theme) << "\n"
        << "compact=" << s.compact << "\n"
        << "verify=" << s.verify << "\n"
        << "buffer_mib=" << s.buffer_mib << "\n"
        << "modern_ram_gib=" << s.modern_ram_gib << "\n"
        << "font_percent=" << s.font_percent << "\n"
        << "accent=" << s.accent << "\n";
    return out.str();
}

Settings parse_settings(std::string_view text, Settings s) {
    while (!text.empty()) {
        const auto end = text.find('\n');
        auto line = text.substr(0, end);
        text.remove_prefix(end == std::string_view::npos ? text.size() : end + 1);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        const auto equal = line.find('=');
        if (equal == std::string_view::npos) continue;
        const auto key = line.substr(0, equal);
        const auto value = line.substr(equal + 1);
        unsigned number = 0;
        const auto parsed = std::from_chars(value.data(), value.data() + value.size(), number);
        if (parsed.ec != std::errc() || parsed.ptr != value.data() + value.size()) continue;
        if (key == "language" && number <= 1) s.language = static_cast<Language>(number);
        if (key == "profile" && number <= 2) s.profile = static_cast<ProfileChoice>(number);
        if (key == "theme" && number <= 5) s.theme = static_cast<Theme>(number);
        if (key == "compact" && number <= 1) s.compact = number == 1;
        if (key == "verify" && number <= 1) s.verify = number == 1;
        if (key == "buffer_mib" && number <= 16) s.buffer_mib = number;
        if (key == "modern_ram_gib" && number >= 4 && number <= 64) s.modern_ram_gib = number;
        if (key == "font_percent" && number >= 90 && number <= 125) s.font_percent = number;
        if (key == "accent" && number <= 3) s.accent = number;
    }
    return s;
}

const wchar_t* describe(Error error, Language l) noexcept {
    const auto tr = [l](const wchar_t* de, const wchar_t* en) { return l == Language::German ? de : en; };
    switch (error) {
    case Error::Cancelled: return tr(L"Der Vorgang wurde abgebrochen.", L"The operation was cancelled.");
    case Error::Input: return tr(L"Die Eingabe ist ungültig.", L"The input is invalid.");
    case Error::Open: return tr(L"Die Quelle konnte nicht geöffnet werden.", L"The source could not be opened.");
    case Error::Read: return tr(L"Die Quelle konnte nicht vollständig gelesen werden.", L"The source could not be read in full.");
    case Error::Write: return tr(L"Die Ausgabe konnte nicht geschrieben werden.", L"The output could not be written.");
    case Error::Exists: return tr(L"Am Ziel existiert bereits eine Datei.", L"A file already exists at the destination.");
    case Error::UnsafePath: return tr(L"Der Pfad liegt außerhalb des erlaubten Bereichs oder enthält eine Umleitung.",
                                     L"The path escapes the allowed location or contains a redirect.");
    case Error::TooManyEntries: return tr(L"Die Quelle überschreitet die Eintragsgrenze.", L"The source exceeds the entry limit.");
    case Error::InvalidImage: return tr(L"Das Image ist beschädigt oder kein unterstütztes ISO-Image.",
                                       L"The image is malformed or is not a supported ISO image.");
    case Error::UnsupportedImage: return tr(L"Dieses Layout kann hier nicht verarbeitet werden.", L"This layout is unsupported here.");
    case Error::Imapi: return tr(L"Die Windows-Brennengine (IMAPI2) hat einen Fehler gemeldet.", L"The Windows burning engine (IMAPI2) reported an error.");
    case Error::RecorderMissing: return tr(L"Für das gewählte Laufwerk wurde kein Brenner gefunden.",
                                           L"No recorder was found for the selected drive.");
    case Error::UnsupportedMedia: return tr(L"Das Medium wird vom Brenner für diesen Vorgang nicht unterstützt.",
                                             L"The recorder does not support this medium for the requested operation.");
    case Error::MediaNotBlank: return tr(L"Für diesen Vorgang ist ein leeres Medium erforderlich.", L"This operation requires blank media.");
    case Error::Capacity: return tr(L"Die Daten passen nicht auf das Medium.", L"The data does not fit on the medium.");
    case Error::Verification: return tr(L"Die Brennprüfung konnte nicht erfolgreich abgeschlossen werden.",
                                         L"Burn verification did not complete successfully.");
    case Error::DiscLayout: return tr(L"Nur Daten-Discs mit einer Session und einer Datenspur lassen sich als ISO sichern.",
                                      L"Only single-session, single-data-track discs can be saved as ISO here.");
    case Error::Settings: return tr(L"Die Einstellungen konnten nicht gespeichert werden.", L"Settings could not be saved.");
    case Error::NoMemory: return tr(L"Nicht genügend Arbeitsspeicher verfügbar.", L"Not enough memory is available.");
    case Error::Unexpected: return tr(L"Ein unerwarteter Fehler ist aufgetreten.", L"An unexpected error occurred.");
    }
    return L"";
}

std::wstring format_size(std::uint64_t bytes, Language language) {
    static constexpr const wchar_t* units[] = {L"B", L"KiB", L"MiB", L"GiB", L"TiB"};
    double amount = static_cast<double>(bytes);
    unsigned unit = 0;
    while (amount >= 1024 && unit < 4) { amount /= 1024; ++unit; }
    std::wostringstream out;
    out << std::fixed << std::setprecision(unit == 0 || amount >= 100 ? 0 : 1) << amount;
    auto result = out.str();
    if (language == Language::German) std::replace(result.begin(), result.end(), L'.', L',');
    return result + L" " + units[unit];
}

std::wstring normalized_sha256(std::wstring_view value) {
    const auto first = value.find_first_not_of(L" \t\r\n");
    if (first == std::wstring_view::npos) return {};
    value = value.substr(first, value.find_last_not_of(L" \t\r\n") - first + 1);
    if (value.size() != 64) throw Failure(Error::Input, 0, L"SHA-256");
    std::wstring result;
    result.reserve(value.size());
    for (auto ch : value) {
        if (ch >= L'A' && ch <= L'F') ch = static_cast<wchar_t>(ch - L'A' + L'a');
        if (!((ch >= L'a' && ch <= L'f') || (ch >= L'0' && ch <= L'9'))) throw Failure(Error::Input, 0, L"SHA-256");
        result += ch;
    }
    return result;
}

bool safe_component(std::wstring_view name) noexcept {
    if (name.empty() || name == L"." || name == L".." || name.size() > 255 ||
        name.back() == L'.' || name.back() == L' ') return false;
    for (const wchar_t ch : name) {
        if (ch < 32 || std::wstring_view(L"<>:\"/\\|?*").find(ch) != std::wstring_view::npos) return false;
    }
    auto base = name.substr(0, name.find(L'.'));
    while (!base.empty() && base.back() == L' ') base.remove_suffix(1);
    wchar_t upper[8]{};
    if (base.size() >= std::size(upper)) return true;
    for (std::size_t i = 0; i < base.size(); ++i) upper[i] = static_cast<wchar_t>(std::towupper(base[i]));
    const std::wstring_view key(upper, base.size());
    if (key == L"CON" || key == L"PRN" || key == L"AUX" || key == L"NUL" || key == L"CONIN$" || key == L"CONOUT$") return false;
    if (key.size() == 4 && (key.substr(0, 3) == L"COM" || key.substr(0, 3) == L"LPT")) {
        return std::wstring_view(L"0123456789¹²³").find(key[3]) == std::wstring_view::npos;
    }
    return true;
}

bool fits_capacity(std::uint64_t bytes, std::uint64_t available_sectors) noexcept {
    return bytes / sector_size + (bytes % sector_size != 0) <= available_sectors;
}

void check_cancel(const IsCancelled& cancelled) {
    if (cancelled && cancelled()) throw Failure(Error::Cancelled);
}

}
