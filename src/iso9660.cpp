#include "iso9660.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <optional>

namespace dc {
namespace {

using Sector = std::array<unsigned char, sector_size>;

std::uint32_t little(const unsigned char* bytes, unsigned width) {
    std::uint32_t number = 0;
    for (unsigned i = 0; i < width; ++i) number |= std::uint32_t(bytes[i]) << (i * 8);
    return number;
}

std::uint32_t both(const unsigned char* bytes, unsigned width) {
    const auto value = little(bytes, width);
    for (unsigned i = 0; i < width; ++i)
        if (bytes[i] != bytes[2 * width - i - 1]) throw Failure(Error::InvalidImage);
    return value;
}

IsoEntry record(const unsigned char* rec, std::size_t available, std::uint64_t volume_size, bool joliet) {
    const std::size_t length = rec[0];
    if (length < 34 || length > available) throw Failure(Error::InvalidImage);
    const unsigned name_size = rec[32];
    if (name_size == 0 || 33 + name_size > length || both(rec + 28, 2) != 1)
        throw Failure(Error::InvalidImage);

    const auto block = std::uint64_t(both(rec + 2, 4)) + rec[1];
    const auto offset = block * sector_size;
    const auto size = both(rec + 10, 4);
    if (offset > volume_size || size > volume_size - offset) throw Failure(Error::InvalidImage);
    IsoEntry entry{L"", offset, size, (rec[25] & 2U) != 0, (rec[25] & 128U) == 0 && rec[26] == 0 && rec[27] == 0};
    if (name_size == 1 && rec[33] < 2) return entry;
    if (joliet && (name_size % 2)) throw Failure(Error::InvalidImage);

    for (unsigned i = 0; i < name_size; i += joliet ? 2 : 1) {
        const auto ch = joliet ? (unsigned(rec[33 + i]) << 8) | rec[34 + i] : rec[33 + i];
        if (ch >= 0xd800 && ch <= 0xdfff) throw Failure(Error::InvalidImage);
        entry.name.push_back(static_cast<wchar_t>(ch));
    }
    if (!entry.directory) {
        const auto version = entry.name.rfind(L';');
        if (version != std::wstring::npos && version + 1 < entry.name.size() &&
            entry.name.find_first_not_of(L"0123456789", version + 1) == std::wstring::npos) {
            entry.name.resize(version);
            if (!entry.name.empty() && entry.name.back() == L'.') entry.name.pop_back();
        }
    }
    if (!safe_component(entry.name)) throw Failure(Error::UnsafePath);
    return entry;
}

struct Description {
    std::uint64_t bytes;
    IsoEntry root;
};

Description read_description(const Sector& sector, std::uint64_t file_size, bool joliet) {
    if (both(sector.data() + 128, 2) != sector_size ||
        both(sector.data() + 120, 2) != 1 || both(sector.data() + 124, 2) != 1)
        throw Failure(Error::UnsupportedImage);
    const auto size = std::uint64_t(both(sector.data() + 80, 4)) * sector_size;
    if (size < 18 * sector_size || size > file_size) throw Failure(Error::InvalidImage);
    auto root = record(sector.data() + 156, sector.size() - 156, size, joliet);
    if (!root.directory || !root.name.empty() || !root.contiguous || root.size == 0)
        throw Failure(Error::InvalidImage);
    return {size, std::move(root)};
}

bool udf_image(std::uint64_t size, const ReadAt& read) {
    if (size < 257 * sector_size) return false;
    Sector sector{};
    bool extended_area = false;
    bool namespace_recorded = false;
    bool terminated = false;
    for (unsigned i = 16; i < 64; ++i) {
        read(i * sector_size, sector.data(), sector.size());
        const char* id = reinterpret_cast<const char*>(sector.data() + 1);
        if (std::memcmp(id, "BEA01", 5) == 0) extended_area = true;
        if (extended_area && (std::memcmp(id, "NSR02", 5) == 0 || std::memcmp(id, "NSR03", 5) == 0))
            namespace_recorded = true;
        if (namespace_recorded && std::memcmp(id, "TEA01", 5) == 0) { terminated = true; break; }
    }
    if (!terminated) return false;
    read(256 * sector_size, sector.data(), sector.size());
    unsigned checksum = 0;
    for (unsigned i = 0; i < 16; ++i) if (i != 4) checksum += sector[i];
    return little(sector.data(), 2) == 2 && little(sector.data() + 12, 4) == 256 && (checksum & 255) == sector[4];
}

}

IsoVolume::IsoVolume(std::uint64_t file_size, const ReadAt& read) {
    if (file_size < 18 * sector_size) throw Failure(Error::InvalidImage);
    Sector sector{};
    std::optional<Description> primary;
    std::optional<Description> supplementary;
    bool terminated = false;
    for (unsigned block = 16; block < 80 && (block + 1) * sector_size <= file_size; ++block) {
        read(block * sector_size, sector.data(), sector.size());
        if (std::memcmp(sector.data() + 1, "CD001", 5) != 0 || sector[6] != 1)
            throw Failure(Error::InvalidImage);
        if (sector[0] == 255) { terminated = true; break; }
        if (sector[0] == 1) primary = read_description(sector, file_size, false);
        if (sector[0] == 2 && sector[88] == '%' && sector[89] == '/' &&
            (sector[90] == '@' || sector[90] == 'C' || sector[90] == 'E'))
            supplementary = read_description(sector, file_size, true);
    }
    if (!primary || !terminated) throw Failure(Error::InvalidImage);
    joliet_ = supplementary.has_value();
    auto& chosen = joliet_ ? *supplementary : *primary;
    size_ = chosen.bytes;
    root_ = std::move(chosen.root);
}

std::vector<IsoEntry> IsoVolume::directory(const IsoEntry& entry, const ReadAt& read,
                                           const IsCancelled& cancelled) const {
    if (!entry.directory || !entry.contiguous || entry.offset > size_ || entry.size > size_ - entry.offset)
        throw Failure(Error::InvalidImage);
    std::vector<IsoEntry> result;
    Sector sector{};
    for (std::uint64_t done = 0; done < entry.size; done += sector_size) {
        check_cancel(cancelled);
        const auto count = static_cast<std::size_t>(std::min(sector_size, entry.size - done));
        read(entry.offset + done, sector.data(), count);
        for (std::size_t i = 0; i < count && sector[i] != 0;) {
            auto child = record(sector.data() + i, count - i, size_, joliet_);
            if (!child.name.empty()) {
                if (result.size() == entry_limit) throw Failure(Error::TooManyEntries);
                result.push_back(std::move(child));
            }
            i += sector[i];
        }
    }
    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
        if (left.directory != right.directory) return left.directory;
        return left.name < right.name;
    });
    return result;
}

void validate_data_image(std::uint64_t size, const ReadAt& read) {
    if (size % sector_size != 0) throw Failure(Error::InvalidImage);
    try {
        IsoVolume volume(size, read);
    } catch (const Failure& error) {
        if (error.code() != Error::InvalidImage && error.code() != Error::UnsupportedImage) throw;
        if (!udf_image(size, read)) throw;
    }
}

}
