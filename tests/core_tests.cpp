#include "iso9660.hpp"
#include "test_common.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <string>

namespace {

void both(unsigned char* data, unsigned value, unsigned width) {
    for (unsigned i = 0; i < width; ++i) data[i] = data[width * 2 - 1 - i] = (value >> (8 * i)) & 255;
}

std::size_t record(unsigned char* destination, unsigned extent, unsigned size, bool dir, std::string_view name) {
    const auto total = 33 + name.size() + (name.size() % 2 == 0);
    destination[0] = static_cast<unsigned char>(total);
    both(destination + 2, extent, 4);
    both(destination + 10, size, 4);
    destination[25] = dir ? 2 : 0;
    both(destination + 28, 1, 2);
    destination[32] = static_cast<unsigned char>(name.size());
    memcpy(destination + 33, name.data(), name.size());
    return total;
}

struct Image {
    std::vector<unsigned char> data = std::vector<unsigned char>(dc::sector_size * 28);

    Image(std::string_view filename, bool joliet = false) {
        descriptor(16, 1, 20);
        unsigned root = 20;
        if (joliet) {
            descriptor(17, 2, 21);
            auto* supplementary = data.data() + 17 * dc::sector_size;
            memcpy(supplementary + 88, "%/E", 3);
            root = 21;
        }
        descriptor(joliet ? 18 : 17, 255, 0);
        auto* position = data.data() + root * dc::sector_size;
        position += record(position, root, 2048, true, std::string_view("\0", 1));
        position += record(position, root, 2048, true, std::string_view("\1", 1));
        record(position, 22, 5, false, filename);
        memcpy(data.data() + 22 * dc::sector_size, "hello", 5);
    }

    void descriptor(unsigned block, unsigned type, unsigned root) {
        auto* target = data.data() + block * dc::sector_size;
        target[0] = static_cast<unsigned char>(type);
        memcpy(target + 1, "CD001", 5);
        target[6] = 1;
        if (type == 255) return;
        both(target + 80, 28, 4);
        both(target + 120, 1, 2);
        both(target + 124, 1, 2);
        both(target + 128, dc::sector_size, 2);
        record(target + 156, root, 2048, true, std::string_view("\0", 1));
    }

    dc::ReadAt reader() const {
        return [this](std::uint64_t offset, void* target, std::size_t count) {
            if (offset > data.size() || count > data.size() - offset) throw dc::Failure(dc::Error::Read);
            memcpy(target, data.data() + offset, count);
        };
    }
};

}

int main() {
    using namespace dc;
    Settings settings;
    const auto gib = std::uint64_t(1) << 30;
    SystemResources resources{8 * gib, 6 * gib, 8, 10, 0, true, true, 25};
    expect(performance_policy(settings, resources).active == ProfileChoice::Modern, "automatic Modern");
    resources.process_64_bit = false;
    expect(performance_policy(settings, resources).active == ProfileChoice::Classic, "x86 Classic");
    settings.profile = ProfileChoice::Modern;
    settings.buffer_mib = 16;
    expect(performance_policy(settings, resources).buffer_bytes == 4 * 1024 * 1024, "x86 buffer cap");
    resources.available = gib / 4;
    expect(performance_policy(settings, resources).buffer_bytes == 1024 * 1024, "pressure buffer cap");
    settings.language = Language::English;
    settings.theme = Theme::Dark;
    settings.compact = true;
    settings.verify = false;
    settings.font_percent = 125;
    const auto loaded = parse_settings(serialize_settings(settings) + "profile=99\r\nverify=false\nfont_percent=2\n");
    expect(loaded.language == Language::English && loaded.profile == ProfileChoice::Modern &&
           loaded.theme == Theme::Dark && loaded.compact && !loaded.verify && loaded.buffer_mib == 16 &&
           loaded.font_percent == 125, "settings roundtrip and invalid data");

    for (const auto* path : {L"..", L".", L"test/../a", L"file:ads", L"file\\x", L"COM1.txt", L"LPT²",
                            L"CON .txt", L"NUL", L"CONOUT$", L"a. ", L"file|a"})
        expect(!safe_component(path), "reject unsafe path name");
    expect(safe_component(L"Bericht 2026.txt") && safe_component(L"archive.001"), "valid name");
    expect(normalized_sha256(L" \t" + std::wstring(64, L'A') + L"\r\n") == std::wstring(64, L'a'), "digest normalization");
    expect_error(Error::Input, [] { normalized_sha256(std::wstring(64, L'g')); }, "invalid digest characters");
    expect_error(Error::Input, [] { normalized_sha256(L"1234"); }, "invalid digest length");
    expect(fits_capacity(2048, 1) && !fits_capacity(2049, 1) && !fits_capacity(UINT64_MAX, UINT64_MAX / 2048),
           "sector capacity, aligned and overflow");

    const Image image("README.TXT;1");
    const IsoVolume iso(image.data.size(), image.reader());
    const auto entries = iso.directory(iso.root(), image.reader());
    expect(entries.size() == 1 && entries[0].name == L"README.TXT" && entries[0].size == 5 &&
           entries[0].offset == 22 * sector_size, "ISO-9660 directory");
    validate_data_image(image.data.size(), image.reader());
    expect_error(Error::Cancelled, [&] { iso.directory(iso.root(), image.reader(), [] { return true; }); }, "cancel directory");
    const Image joliet(std::string("\0" "H\0" "i\0.\0t\0x\0t\0;\0" "1", 16), true);
    const IsoVolume ji(joliet.data.size(), joliet.reader());
    expect(ji.is_joliet() && ji.directory(ji.root(), joliet.reader()).at(0).name == L"Hi.txt", "Joliet UCS-2");
    const Image unsafe("../OUT.TXT;1");
    const IsoVolume ui(unsafe.data.size(), unsafe.reader());
    expect_error(Error::UnsafePath, [&] { ui.directory(ui.root(), unsafe.reader()); }, "ISO path traversal");
    expect_error(Error::InvalidImage, [&] { IsoVolume invalid(image.data.size() / 2, image.reader()); }, "truncated volume");
    auto corrupt = image;
    corrupt.data[16 * sector_size + 80] = 28 ^ 1;
    expect_error(Error::InvalidImage, [&] { IsoVolume invalid(corrupt.data.size(), corrupt.reader()); }, "endian mismatch");
    auto malformed = image;
    malformed.data[20 * sector_size + 68] = 10;
    expect_error(Error::InvalidImage, [&] { iso.directory(iso.root(), malformed.reader()); }, "short directory entry");
    std::cout << "core tests passed\n";
}
