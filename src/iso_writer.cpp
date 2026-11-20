#include "iso_writer.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string_view>
#include <unordered_set>

namespace dc {
namespace {

using Sector = std::array<unsigned char, sector_size>;

std::uint64_t blocks(std::uint64_t bytes) {
    return bytes / sector_size + (bytes % sector_size != 0);
}

void endian(unsigned char* data, std::uint32_t value, unsigned width, bool big) {
    for (unsigned i = 0; i < width; ++i) data[big ? width - 1 - i : i] = static_cast<unsigned char>(value >> (8 * i));
}

void dual(unsigned char* data, std::uint32_t value, unsigned width) {
    endian(data, value, width, false);
    endian(data + width, value, width, true);
}

std::string ascii_name(std::wstring_view name, std::size_t limit) {
    std::string result;
    for (const auto ch : name.substr(0, limit)) {
        if (ch >= L'a' && ch <= L'z') result += static_cast<char>(ch - L'a' + 'A');
        else if ((ch >= L'A' && ch <= L'Z') || (ch >= L'0' && ch <= L'9')) result += static_cast<char>(ch);
        else result += '_';
    }
    return result;
}

std::string joliet_name(std::wstring_view name, std::size_t length) {
    if (name.size() > length) throw Failure(Error::UnsupportedImage, 0, std::wstring(name));
    std::string result;
    result.reserve(name.size() * 2);
    for (const auto ch : name) {
        const auto code = static_cast<unsigned>(ch);
        if (code > 0xffff || (code >= 0xd800 && code <= 0xdfff) || code == 0xfffe || code == 0xffff)
            throw Failure(Error::UnsupportedImage, 0, std::wstring(name));
        result += static_cast<char>(code >> 8);
        result += static_cast<char>(code & 255);
    }
    return result;
}

struct Node {
    std::wstring name;
    std::array<std::string, 2> identifiers;
    std::vector<std::size_t> children;
    std::size_t parent = 0;
    std::uint64_t size = 0;
    std::uint64_t modified = 0;
    bool directory = true;
    std::array<std::uint32_t, 2> extent{};
    std::array<std::uint32_t, 2> directory_bytes{};
    std::array<std::uint16_t, 2> dir_number{};
};

void scan(std::vector<Node>& nodes, std::size_t index, const std::wstring& location, unsigned depth,
          const IsCancelled& cancelled) {
    auto entries = read_directory(location, cancelled);
    std::unordered_set<std::string> taken;
    const auto start = nodes.size();
    if (entries.size() > entry_limit - start) throw Failure(Error::TooManyEntries, 0, location);
    for (auto& entry : entries) {
        check_cancel(cancelled);
        if (!entry.safe) throw Failure(Error::UnsafePath, 0, entry.name);
        if (entry.size > std::numeric_limits<std::uint32_t>::max() || (entry.directory && depth >= 8))
            throw Failure(Error::UnsupportedImage, 0, entry.name);
        Node node;
        node.parent = index;
        node.name = std::move(entry.name);
        node.size = entry.size;
        node.directory = entry.directory;
        node.modified = entry.modified;
        node.identifiers[1] = joliet_name(node.name + (node.directory ? L"" : L";1"), 64);
        const auto dot = node.directory ? std::wstring::npos : node.name.find_last_of(L'.');
        const auto stem = ascii_name(std::wstring_view(node.name).substr(0, dot), 16);
        const auto base = stem.empty() ? std::string("_") : stem;
        const auto extension = dot == std::wstring::npos ? std::string{} : ascii_name(std::wstring_view(node.name).substr(dot + 1), 5);
        auto alias = base;
        unsigned suffix = 1;
        while (!taken.insert(alias + (extension.empty() ? "" : "." + extension)).second)
            alias = base + "_" + std::to_string(suffix++);
        node.identifiers[0] = node.directory ? alias : alias + "." + extension + ";1";
        nodes[index].children.push_back(nodes.size());
        nodes.push_back(std::move(node));
    }
    for (auto child = start; child < start + entries.size(); ++child) {
        if (nodes[child].directory) {
            const auto path = combine(location, nodes[child].name);
            scan(nodes, child, path, depth + 1, cancelled);
        }
    }
}

std::size_t record_length(std::size_t identifier_length) {
    return 33 + identifier_length + (identifier_length % 2 == 0);
}

struct Tree {
    std::vector<std::size_t> directories{0};
    std::uint32_t table_bytes = 0;
    std::uint32_t table_l = 0, table_m = 0;
};

Tree layout_tree(std::vector<Node>& nodes, unsigned set) {
    Tree result;
    for (std::size_t dir = 0; dir < result.directories.size(); ++dir) {
        const auto index = result.directories[dir];
        auto& node = nodes[index];
        node.dir_number[set] = static_cast<std::uint16_t>(dir + 1);
        const auto bytes = node.identifiers[set].size();
        result.table_bytes += static_cast<std::uint32_t>(8 + bytes + bytes % 2);
        auto sorted = node.children;
        std::sort(sorted.begin(), sorted.end(), [&](std::size_t a, std::size_t b) {
            return nodes[a].identifiers[set] < nodes[b].identifiers[set];
        });
        std::size_t written = 68;
        for (const auto child : sorted) {
            const auto& item = nodes[child];
            const auto length = record_length(item.identifiers[set].size());
            if (written % sector_size + length > sector_size) written = static_cast<std::size_t>(blocks(written) * sector_size);
            written += length;
            if (item.directory) {
                if (result.directories.size() == std::numeric_limits<std::uint16_t>::max())
                    throw Failure(Error::TooManyEntries);
                result.directories.push_back(child);
            }
        }
        node.directory_bytes[set] = static_cast<std::uint32_t>(blocks(written) * sector_size);
    }
    return result;
}

std::size_t record(unsigned char* to, const Node& node, unsigned set, std::string_view identifier) {
    const auto length = record_length(identifier.size());
    std::fill_n(to, length, 0);
    to[0] = static_cast<unsigned char>(length);
    dual(to + 2, node.extent[set], 4);
    dual(to + 10, node.directory ? node.directory_bytes[set] : static_cast<std::uint32_t>(node.size), 4);
    const FILETIME filetime{static_cast<DWORD>(node.modified), static_cast<DWORD>(node.modified >> 32)};
    SYSTEMTIME date{};
    if (!FileTimeToSystemTime(&filetime, &date) || date.wYear < 1900 || date.wYear > 2155) {
        date.wYear = 1970;
        date.wMonth = date.wDay = 1;
    }
    to[18] = static_cast<unsigned char>(date.wYear - 1900);
    to[19] = static_cast<unsigned char>(date.wMonth);
    to[20] = static_cast<unsigned char>(date.wDay);
    to[21] = static_cast<unsigned char>(date.wHour);
    to[22] = static_cast<unsigned char>(date.wMinute);
    to[23] = static_cast<unsigned char>(date.wSecond);
    to[25] = node.directory ? 2 : 0;
    dual(to + 28, 1, 2);
    to[32] = static_cast<unsigned char>(identifier.size());
    std::memcpy(to + 33, identifier.data(), identifier.size());
    return length;
}

void descriptor(AtomicOutput& output, unsigned set, const Tree& layout, const Node& root,
                std::uint32_t capacity, const std::wstring& label) {
    Sector sector{};
    const bool joliet = set == 1;
    const auto field = [&](std::size_t offset, std::size_t length, std::string_view data) {
        for (std::size_t i = 0; i < length; ++i) sector[offset + i] = (!joliet || i % 2 == 1) ? ' ' : 0;
        if (!data.empty()) std::memcpy(sector.data() + offset, data.data(), data.size());
    };
    sector[0] = joliet ? 2 : 1;
    std::memcpy(sector.data() + 1, "CD001", 5);
    sector[6] = 1;
    field(8, 32, "");
    field(40, 32, joliet ? joliet_name(label, 16) : ascii_name(label, 32));
    dual(sector.data() + 80, capacity, 4);
    if (joliet) std::memcpy(sector.data() + 88, "%/E", 3);
    dual(sector.data() + 120, 1, 2);
    dual(sector.data() + 124, 1, 2);
    dual(sector.data() + 128, sector_size, 2);
    dual(sector.data() + 132, layout.table_bytes, 4);
    endian(sector.data() + 140, layout.table_l, 4, false);
    endian(sector.data() + 148, layout.table_m, 4, true);
    record(sector.data() + 156, root, set, std::string_view("\0", 1));
    for (auto offset : {190, 318, 446, 574}) field(offset, 128, "");
    for (auto offset : {702, 739, 776}) field(offset, 37, "");
    SYSTEMTIME date{};
    GetSystemTime(&date);
    char stamp[24]{};
    std::snprintf(stamp, sizeof(stamp), "%04d%02d%02d%02d%02d%02d%02d",
                  date.wYear, date.wMonth, date.wDay, date.wHour, date.wMinute, date.wSecond, date.wMilliseconds / 10);
    for (auto offset : {813, 830, 864}) std::memcpy(sector.data() + offset, stamp, 16);
    std::fill_n(sector.data() + 847, 16, '0');
    sector[881] = 1;
    output.write(sector.data(), sector.size());
}

void path_table(AtomicOutput& output, const Tree& tree, const std::vector<Node>& nodes, unsigned set, bool big,
                const IsCancelled& cancelled) {
    Sector buffer{};
    std::size_t position = 0;
    for (const auto index : tree.directories) {
        check_cancel(cancelled);
        const auto& node = nodes[index];
        const auto& name = node.identifiers[set];
        std::array<unsigned char, 8> header{};
        header[0] = static_cast<unsigned char>(name.size());
        endian(header.data() + 2, node.extent[set], 4, big);
        endian(header.data() + 6, nodes[node.parent].dir_number[set], 2, big);
        const auto append = [&](unsigned char byte) {
            buffer[position++] = byte;
            if (position == buffer.size()) { output.write(buffer.data(), buffer.size()); position = 0; }
        };
        for (const auto byte : header) append(byte);
        for (const auto ch : name) append(static_cast<unsigned char>(ch));
        if (name.size() % 2) append(0);
    }
    if (position) {
        std::fill(buffer.begin() + position, buffer.end(), 0);
        output.write(buffer.data(), buffer.size());
    }
}

void directories(AtomicOutput& output, const Tree& tree, const std::vector<Node>& nodes, unsigned set,
                 const IsCancelled& cancelled) {
    for (const auto index : tree.directories) {
        check_cancel(cancelled);
        const auto& node = nodes[index];
        Sector sector{};
        auto written = record(sector.data(), node, set, std::string_view("\0", 1));
        written += record(sector.data() + written, nodes[node.parent], set, std::string_view("\1", 1));
        auto sorted = node.children;
        std::sort(sorted.begin(), sorted.end(), [&](std::size_t a, std::size_t b) {
            return nodes[a].identifiers[set] < nodes[b].identifiers[set];
        });
        for (const auto child : sorted) {
            const auto& item = nodes[child];
            const auto& name = item.identifiers[set];
            if (written + record_length(name.size()) > sector.size()) {
                output.write(sector.data(), sector.size());
                sector.fill(0);
                written = 0;
            }
            written += record(sector.data() + written, item, set, name);
        }
        output.write(sector.data(), sector.size());
    }
}

std::wstring input_path(const std::vector<Node>& nodes, std::size_t index, const std::wstring& folder) {
    if (!index) return folder;
    return combine(input_path(nodes, nodes[index].parent, folder), nodes[index].name);
}

}

void write_iso_joliet(const std::wstring& folder, const std::wstring& destination, const std::wstring& label,
                      std::size_t buffer_size, const Report& report, const IsCancelled& cancelled) {
    if (label.empty() || label.size() > 16 || !buffer_size) throw Failure(Error::Input, 0, L"ISO / Joliet volume label (1–16)");
    (void)joliet_name(label, 16);
    check_image_destination(folder, destination);
    std::vector<Node> nodes(1);
    nodes.front().identifiers.fill(std::string(1, '\0'));
    scan(nodes, 0, folder, 1, cancelled);
    if (nodes.size() == 1) throw Failure(Error::Input, 0, folder);
    auto primary = layout_tree(nodes, 0);
    auto joliet = layout_tree(nodes, 1);
    std::uint64_t next_block = 19;
    for (auto* tree : {&primary, &joliet}) {
        tree->table_l = static_cast<std::uint32_t>(next_block);
        next_block += blocks(tree->table_bytes);
        tree->table_m = static_cast<std::uint32_t>(next_block);
        next_block += blocks(tree->table_bytes);
    }
    for (unsigned n = 0; n < 2; ++n) {
        for (const auto index : (n == 0 ? primary : joliet).directories) {
            auto& dir = nodes[index];
            dir.extent[n] = static_cast<std::uint32_t>(next_block);
            next_block += blocks(dir.directory_bytes[n]);
        }
    }
    const auto data_start = next_block;
    for (auto& node : nodes) {
        if (node.directory) continue;
        if (next_block + blocks(node.size) > std::numeric_limits<std::uint32_t>::max()) throw Failure(Error::Capacity);
        node.extent.fill(static_cast<std::uint32_t>(next_block));
        next_block += blocks(node.size);
    }
    const auto capacity = static_cast<std::uint32_t>(next_block);
    AtomicOutput output(destination);
    Sector zero{};
    for (unsigned sector = 0; sector < 16; ++sector) output.write(zero.data(), zero.size());
    descriptor(output, 0, primary, nodes.front(), capacity, label);
    descriptor(output, 1, joliet, nodes.front(), capacity, label);
    Sector terminator{};
    terminator[0] = 255;
    std::memcpy(terminator.data() + 1, "CD001", 5);
    terminator[6] = 1;
    output.write(terminator.data(), terminator.size());
    for (unsigned n = 0; n < 2; ++n) {
        const auto& tree = n == 0 ? primary : joliet;
        path_table(output, tree, nodes, n, false, cancelled);
        path_table(output, tree, nodes, n, true, cancelled);
    }
    directories(output, primary, nodes, 0, cancelled);
    directories(output, joliet, nodes, 1, cancelled);
    std::vector<unsigned char> buffer(buffer_size);
    ProgressMeter meter(report);
    auto written = data_start * sector_size;
    const auto total = next_block * sector_size;
    meter.update(Stage::Writing, written, total, true);
    for (std::size_t i = 1; i < nodes.size(); ++i) {
        const auto& node = nodes[i];
        if (node.directory) continue;
        check_cancel(cancelled);
        const auto path = input_path(nodes, i, folder);
        const InputFile file(path);
        if (file.size() != node.size || file.identity().modified != node.modified) throw Failure(Error::Read, 0, path);
        for (std::uint64_t done = 0; done < node.size;) {
            check_cancel(cancelled);
            const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(buffer.size(), node.size - done));
            file.read(done, buffer.data(), count);
            output.write(buffer.data(), count);
            done += count;
            written += count;
            meter.update(Stage::Writing, written, total);
        }
        const auto padding = blocks(node.size) * sector_size - node.size;
        if (padding) output.write(zero.data(), static_cast<std::size_t>(padding));
        written += padding;
    }
    check_cancel(cancelled);
    output.commit();
    meter.update(Stage::Writing, total, total, true);
}

}
