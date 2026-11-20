#pragma once
#include "core.hpp"
#include <vector>

namespace dc {

using ReadAt = std::function<void(std::uint64_t offset, void* buffer, std::size_t bytes)>;

struct IsoEntry {
    std::wstring name;
    std::uint64_t offset = 0;
    std::uint64_t size = 0;
    bool directory = false;
    bool contiguous = true;
};

class IsoVolume {
public:
    IsoVolume(std::uint64_t file_size, const ReadAt& read);
    const IsoEntry& root() const noexcept { return root_; }
    bool is_joliet() const noexcept { return joliet_; }
    std::vector<IsoEntry> directory(const IsoEntry& entry, const ReadAt& read,
                                    const IsCancelled& cancelled = {}) const;
private:
    std::uint64_t size_ = 0;
    bool joliet_ = false;
    IsoEntry root_;
};

void validate_data_image(std::uint64_t size, const ReadAt& read);

}
