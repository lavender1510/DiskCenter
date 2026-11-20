#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace dc {

enum class Error {
    Cancelled = 1, Input, Open, Read, Write, Exists, UnsafePath, TooManyEntries, InvalidImage,
    UnsupportedImage, Imapi, RecorderMissing, UnsupportedMedia, MediaNotBlank, Capacity,
    Verification, DiscLayout, Settings, NoMemory, Unexpected
};

class Failure final : public std::runtime_error {
public:
    explicit Failure(Error code, std::uint32_t system_code = 0, std::wstring detail = {});
    Error code() const noexcept { return code_; }
    std::uint32_t system_code() const noexcept { return system_code_; }
    const std::wstring& detail() const noexcept { return detail_; }
private:
    Error code_;
    std::uint32_t system_code_;
    std::wstring detail_;
};

enum class Language { German, English };
enum class ProfileChoice { Automatic, Classic, Modern };
enum class Theme { System, Light, Dark, Classic, Aero, LowMemory };
enum class FileSystem { IsoJoliet, Udf, Hybrid };

struct Settings {
    Language language = Language::German;
    ProfileChoice profile = ProfileChoice::Automatic;
    Theme theme = Theme::System;
    bool compact = false;
    bool verify = true;
    unsigned buffer_mib = 0;
    unsigned modern_ram_gib = 8;
    unsigned font_percent = 100;
    unsigned accent = 0;
};

struct SystemResources {
    std::uint64_t installed = 0;
    std::uint64_t available = 0;
    unsigned processors = 1;
    unsigned windows_major = 6;
    unsigned windows_minor = 1;
    bool process_64_bit = false;
    bool display_accelerated = false;
    unsigned memory_load = 100;
};

struct PerformancePolicy {
    ProfileChoice active = ProfileChoice::Classic;
    std::size_t buffer_bytes = 1024 * 1024;
    bool gradients = false;
    bool pressure = false;
};

PerformancePolicy performance_policy(const Settings& settings, const SystemResources& resources) noexcept;
std::string serialize_settings(const Settings& settings);
Settings parse_settings(std::string_view text, Settings defaults = {});
const wchar_t* describe(Error error, Language language) noexcept;
std::wstring format_size(std::uint64_t bytes, Language language);
std::wstring normalized_sha256(std::wstring_view text);
bool safe_component(std::wstring_view name) noexcept;
bool fits_capacity(std::uint64_t bytes, std::uint64_t available_sectors) noexcept;

enum class Stage { Preparing, Reading, Writing, Finalizing, Verifying };
struct Progress {
    Stage stage = Stage::Preparing;
    std::uint64_t done = 0;
    std::uint64_t total = 0;
    std::uint64_t bytes_per_second = 0;
    unsigned seconds_remaining = 0;
};

using Report = std::function<void(Progress)>;
using IsCancelled = std::function<bool()>;
void check_cancel(const IsCancelled& cancelled);

inline constexpr std::uint64_t sector_size = 2048;
inline constexpr std::size_t entry_limit = 100000;

}
