#pragma once
#include "platform.hpp"

namespace dc {

enum class MediaState { Mounted, NoMedia, Unavailable };
struct Drive {
    std::wstring root;
    std::wstring label;
    std::wstring filesystem;
    std::uint64_t capacity = 0;
    MediaState state = MediaState::Unavailable;
};
std::vector<Drive> optical_drives(const IsCancelled& cancelled = {});
void eject(const std::wstring& root);

struct MediaInfo {
    int type = 0;
    std::uint64_t free_sectors = 0;
    bool blank = false;
    std::vector<long> write_speeds;
};

struct BurnOptions {
    std::wstring drive;
    bool verify = true;
    long sectors_per_second = -1;
};

struct ImageOptions {
    std::wstring label = L"DATA_DISC";
    FileSystem filesystem = FileSystem::IsoJoliet;
};

MediaInfo inspect_media(const std::wstring& drive);
void create_image(const std::wstring& folder, const std::wstring& destination, const ImageOptions& options,
                  std::size_t buffer_size, const Report& report, const IsCancelled& cancelled);
void burn_image(const std::wstring& path, const BurnOptions& options, const Report& report, const IsCancelled& cancelled);
void burn_folder(const std::wstring& folder, const ImageOptions& image, const BurnOptions& options,
                 const Report& report, const IsCancelled& cancelled);
void save_data_disc(const std::wstring& drive, const std::wstring& output, std::size_t buffer_size,
                    const Report& report, const IsCancelled& cancelled);

}
