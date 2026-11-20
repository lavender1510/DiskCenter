#include "platform.hpp"
#include "identity.hpp"

#include <algorithm>
#include <array>
#include <bcrypt.h>
#include <limits>
#include <psapi.h>
#include <shlobj.h>
#include <vector>

namespace dc {
namespace {

std::wstring full_path(const std::wstring& path) {
    if (path.empty()) throw Failure(Error::Input);
    DWORD size = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
    if (!size || size > 32768) win_failure(Error::UnsafePath, path);
    std::wstring result(size, L'\0');
    size = GetFullPathNameW(path.c_str(), size, result.data(), nullptr);
    if (!size || size >= result.size()) win_failure(Error::UnsafePath, path);
    result.resize(size);
    return result;
}

void validate_absolute_path(const std::wstring& path) {
    if (path.compare(0, 4, L"\\\\.\\") == 0 || path.compare(0, 4, L"\\\\?\\") == 0)
        throw Failure(Error::UnsafePath, 0, path);
    std::size_t pos = 0;
    if (path.size() >= 3 && path[1] == L':' && path[2] == L'\\') pos = 3;
    else if (path.compare(0, 2, L"\\\\") == 0) pos = 2;
    else throw Failure(Error::UnsafePath, 0, path);
    while (pos < path.size()) {
        const auto slash = path.find(L'\\', pos);
        const auto component = std::wstring_view(path).substr(pos, slash - pos);
        if (!safe_component(component)) throw Failure(Error::UnsafePath, 0, path);
        if (slash == std::wstring::npos) break;
        pos = slash + 1;
    }
}

std::wstring final_path(const std::wstring& path) {
    const auto native = win_path(path);
    Handle handle(CreateFileW(native.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr));
    if (!handle) win_failure(Error::Open, path);
    const DWORD capacity = GetFinalPathNameByHandleW(handle.get(), nullptr, 0, FILE_NAME_NORMALIZED);
    if (!capacity || capacity > 32768) win_failure(Error::UnsafePath, path);
    std::wstring result(capacity, L'\0');
    const auto size = GetFinalPathNameByHandleW(handle.get(), result.data(), capacity, FILE_NAME_NORMALIZED);
    if (!size || size >= capacity) win_failure(Error::UnsafePath, path);
    result.resize(size);
    return result;
}

std::string utf8(std::wstring_view s) {
    if (s.empty()) return {};
    const auto size = static_cast<int>(s.size());
    const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s.data(), size, nullptr, 0, nullptr, nullptr);
    if (count <= 0) win_failure(Error::Input);
    std::string out(count, '\0');
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s.data(), size, out.data(), count, nullptr, nullptr))
        win_failure(Error::Input);
    return out;
}

void create_storage_directory(const Storage& storage) {
    if (!CreateDirectoryW(win_path(storage.folder).c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
        win_failure(Error::Settings, storage.folder);
}

void walk_image_tree(const std::wstring& source, std::size_t depth, std::size_t& count, const IsCancelled& cancelled) {
    if (depth > 64) throw Failure(Error::TooManyEntries, 0, source);
    for (const auto& entry : read_directory(source, cancelled)) {
        check_cancel(cancelled);
        const auto path = combine(source, entry.name);
        if (!entry.safe) throw Failure(Error::UnsafePath, 0, path);
        if (++count > entry_limit) throw Failure(Error::TooManyEntries, 0, source);
        if (entry.directory) walk_image_tree(path, depth + 1, count, cancelled);
    }
}

bool beneath(const std::wstring& source, const std::wstring& destination) {
    if (destination.size() < source.size()) return false;
    const auto length = static_cast<int>(source.size());
    return CompareStringOrdinal(source.c_str(), length, destination.c_str(), length, TRUE) == CSTR_EQUAL &&
        (destination.size() == source.size() || source.back() == L'\\' || destination[length] == L'\\');
}

}

std::wstring win_path(const std::wstring& path) {
    if (path.find(L'\0') != std::wstring::npos || path.compare(0, 4, L"\\\\.\\") == 0 ||
        path.compare(0, 4, L"\\\\?\\") == 0 || path.find(L':', 2) != std::wstring::npos)
        throw Failure(Error::UnsafePath, 0, path);
    const auto normalized = full_path(path);
    validate_absolute_path(normalized);
    return normalized.compare(0, 2, L"\\\\") == 0 ? L"\\\\?\\UNC\\" + normalized.substr(2) : L"\\\\?\\" + normalized;
}

std::wstring system_message(std::uint32_t code) {
    if (!code) return {};
    wchar_t buffer[512]{};
    auto count = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, code, 0,
                                buffer, static_cast<DWORD>(std::size(buffer)), nullptr);
    if (count == 0 && (code & 0xffff0000u) == 0x80070000u)
        count = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, code & 0xffff, 0,
                               buffer, static_cast<DWORD>(std::size(buffer)), nullptr);
    std::wstring result(buffer, count);
    while (!result.empty() && (result.back() == L'\r' || result.back() == L'\n' || result.back() == L' ')) result.pop_back();
    return result;
}

void win_failure(Error error, const std::wstring& detail) {
    const auto code = GetLastError();
    throw Failure(error, code, detail);
}

void check_hr(HRESULT result, Error error, const std::wstring& detail) {
    if (FAILED(result)) throw Failure(error, static_cast<std::uint32_t>(result), detail);
}

ComApartment::ComApartment(DWORD mode) {
    check_hr(CoInitializeEx(nullptr, mode), Error::Unexpected, L"CoInitializeEx");
}

ComApartment::~ComApartment() { CoUninitialize(); }

Handle::Handle(Handle&& other) noexcept : handle_(other.handle_) { other.handle_ = INVALID_HANDLE_VALUE; }

Handle& Handle::operator=(Handle&& other) noexcept {
    if (this != &other) {
        reset();
        handle_ = other.handle_;
        other.handle_ = INVALID_HANDLE_VALUE;
    }
    return *this;
}

void Handle::reset() noexcept {
    if (*this) CloseHandle(handle_);
    handle_ = INVALID_HANDLE_VALUE;
}

bool FileIdentity::operator==(const FileIdentity& rhs) const noexcept {
    return volume == rhs.volume && index_high == rhs.index_high && index_low == rhs.index_low &&
        size == rhs.size && modified == rhs.modified;
}

InputFile::InputFile(const std::wstring& path)
    : path_(path), input_(CreateFileW(win_path(path).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                     FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr)) {
    if (!input_) win_failure(Error::Open, path);
    BY_HANDLE_FILE_INFORMATION info{};
    if (GetFileType(input_.get()) != FILE_TYPE_DISK || !GetFileInformationByHandle(input_.get(), &info))
        win_failure(Error::Open, path);
    if (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))
        throw Failure(Error::UnsafePath, 0, path);
    identity_ = {info.dwVolumeSerialNumber, info.nFileIndexHigh, info.nFileIndexLow,
                 (std::uint64_t(info.nFileSizeHigh) << 32) | info.nFileSizeLow,
                 (std::uint64_t(info.ftLastWriteTime.dwHighDateTime) << 32) | info.ftLastWriteTime.dwLowDateTime};
}

void InputFile::read(std::uint64_t offset, void* buffer, std::size_t bytes) const {
    if (offset > size() || bytes > size() - offset || bytes > std::numeric_limits<DWORD>::max())
        throw Failure(Error::Read, ERROR_HANDLE_EOF, path_);
    LARGE_INTEGER position{};
    position.QuadPart = static_cast<LONGLONG>(offset);
    if (!SetFilePointerEx(input_.get(), position, nullptr, FILE_BEGIN)) win_failure(Error::Read, path_);
    DWORD actual = 0;
    if (!ReadFile(input_.get(), buffer, static_cast<DWORD>(bytes), &actual, nullptr)) win_failure(Error::Read, path_);
    if (actual != bytes) throw Failure(Error::Read, ERROR_HANDLE_EOF, path_);
}

ReadAt InputFile::reader() const {
    return [this](std::uint64_t offset, void* buffer, std::size_t bytes) { read(offset, buffer, bytes); };
}

AtomicOutput::AtomicOutput(const std::wstring& path, bool replace) : final_(win_path(path)), replace_(replace) {
    const auto attrs = GetFileAttributesW(final_.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES) {
        if (!replace_) throw Failure(Error::Exists, ERROR_FILE_EXISTS, path);
        if (attrs & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) throw Failure(Error::UnsafePath, 0, path);
    }
    const auto directory = parent_path(final_);
    for (unsigned attempt = 0; attempt < 32; ++attempt) {
        auto candidate = combine(directory, L".disccenter-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                                 std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(attempt) + L".partial");
        Handle handle(CreateFileW(candidate.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (handle) {
            temporary_ = std::move(candidate);
            handle_ = std::move(handle);
            return;
        }
        if (GetLastError() != ERROR_FILE_EXISTS && GetLastError() != ERROR_ALREADY_EXISTS) win_failure(Error::Write, path);
    }
    throw Failure(Error::Write, ERROR_FILE_EXISTS, path);
}

AtomicOutput::~AtomicOutput() {
    handle_.reset();
    if (!temporary_.empty()) DeleteFileW(temporary_.c_str());
}

void AtomicOutput::write(const void* data, std::size_t bytes) {
    if (bytes > std::numeric_limits<DWORD>::max()) throw Failure(Error::Write);
    DWORD actual = 0;
    if (!WriteFile(handle_.get(), data, static_cast<DWORD>(bytes), &actual, nullptr)) win_failure(Error::Write, final_);
    if (actual != bytes) throw Failure(Error::Write, ERROR_WRITE_FAULT, final_);
}

void AtomicOutput::commit() {
    if (!FlushFileBuffers(handle_.get())) win_failure(Error::Write, final_);
    handle_.reset();
    const auto flags = MOVEFILE_WRITE_THROUGH | (replace_ ? MOVEFILE_REPLACE_EXISTING : 0);
    if (!MoveFileExW(temporary_.c_str(), final_.c_str(), flags)) {
        const auto code = GetLastError();
        throw Failure((code == ERROR_ALREADY_EXISTS || code == ERROR_FILE_EXISTS) ? Error::Exists : Error::Write, code, final_);
    }
    temporary_.clear();
}

std::wstring combine(const std::wstring& folder, const std::wstring& name) {
    return folder + (folder.empty() || folder.back() == L'\\' ? L"" : L"\\") + name;
}

std::wstring parent_path(const std::wstring& path) {
    if (path.empty()) return {};
    const auto end = path.find_last_not_of(L"\\/");
    if (end == std::wstring::npos) return path;
    const auto slash = path.find_last_of(L"\\/", end);
    if (slash == std::wstring::npos) return {};
    const auto extra = slash > 0 && path[slash - 1] == L':' ? 1 : 0;
    return path.substr(0, slash + extra);
}

std::wstring file_name(const std::wstring& path) {
    auto slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

std::vector<Entry> read_directory(const std::wstring& folder, const IsCancelled& cancelled) {
    const auto root = win_path(folder);
    const auto attrs = GetFileAttributesW(root.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) win_failure(Error::Open, folder);
    if ((attrs & FILE_ATTRIBUTE_DIRECTORY) == 0 || (attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
        throw Failure(Error::UnsafePath, 0, folder);
    WIN32_FIND_DATAW data{};
    const auto pattern = combine(root, L"*");
    struct FindHandle {
        HANDLE value;
        ~FindHandle() { if (value != INVALID_HANDLE_VALUE) FindClose(value); }
    } find{FindFirstFileW(pattern.c_str(), &data)};
    if (find.value == INVALID_HANDLE_VALUE) {
        if (GetLastError() == ERROR_FILE_NOT_FOUND) return {};
        win_failure(Error::Read, folder);
    }
    std::vector<Entry> entries;
    do {
        check_cancel(cancelled);
        if (wcscmp(data.cFileName, L".") == 0 || wcscmp(data.cFileName, L"..") == 0) continue;
        if (entries.size() == entry_limit) throw Failure(Error::TooManyEntries, 0, folder);
        entries.push_back({data.cFileName, (std::uint64_t(data.nFileSizeHigh) << 32) | data.nFileSizeLow,
                           (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0,
                           (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0 && safe_component(data.cFileName), {},
                           (std::uint64_t(data.ftLastWriteTime.dwHighDateTime) << 32) | data.ftLastWriteTime.dwLowDateTime});
    } while (FindNextFileW(find.value, &data));
    if (GetLastError() != ERROR_NO_MORE_FILES) win_failure(Error::Read, folder);
    std::sort(entries.begin(), entries.end(), [](const Entry& left, const Entry& right) {
        if (left.directory != right.directory) return left.directory;
        return CompareStringOrdinal(left.name.c_str(), -1, right.name.c_str(), -1, TRUE) == CSTR_LESS_THAN;
    });
    return entries;
}

void check_image_destination(const std::wstring& source, const std::wstring& destination) {
    if (destination.empty()) return;
    const auto source_target = final_path(source);
    const auto output_parent = final_path(parent_path(full_path(destination)));
    if (beneath(source_target, output_parent)) throw Failure(Error::UnsafePath, 0, destination);
}

void check_image_source(const std::wstring& source, const std::wstring& destination, const IsCancelled& cancelled) {
    check_image_destination(source, destination);
    std::size_t count = 0;
    walk_image_tree(source, 0, count, cancelled);
    if (count == 0) throw Failure(Error::Input, 0, source);
}

void ProgressMeter::update(Stage stage, std::uint64_t done, std::uint64_t total, bool force) {
    if (!report_) return;
    const auto now = GetTickCount64();
    if (!force && updated_ && now - updated_ < 160 && done != total) return;
    updated_ = now;
    const auto elapsed = now - start_;
    const auto velocity = elapsed >= 100 ? static_cast<std::uint64_t>(double(done) * 1000 / double(elapsed)) : 0;
    const auto remaining = velocity && total > done ? (total - done) / velocity : 0;
    report_({stage, done, total, velocity, static_cast<unsigned>(std::min<std::uint64_t>(remaining, 86400))});
}

void copy_range(const InputFile& input, std::uint64_t offset, std::uint64_t total,
                const std::wstring& destination, std::size_t buffer_size, const Report& report,
                const IsCancelled& cancelled) {
    if (offset > input.size() || total > input.size() - offset || buffer_size == 0) throw Failure(Error::Input);
    AtomicOutput output(destination);
    std::vector<unsigned char> buffer(buffer_size);
    ProgressMeter progress(report);
    progress.update(Stage::Reading, 0, total, true);
    for (std::uint64_t done = 0; done < total;) {
        check_cancel(cancelled);
        const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(buffer.size(), total - done));
        input.read(offset + done, buffer.data(), count);
        output.write(buffer.data(), count);
        done += count;
        progress.update(Stage::Reading, done, total);
    }
    check_cancel(cancelled);
    output.commit();
}

void write_catalog(const std::wstring& path, const std::vector<Entry>& entries, const IsCancelled& cancelled) {
    AtomicOutput output(path);
    constexpr std::string_view header = "\xef\xbb\xbf" "name,type,bytes\r\n";
    output.write(header.data(), header.size());
    for (const auto& entry : entries) {
        check_cancel(cancelled);
        // CSV files may be opened by spreadsheet applications that interpret formulas.
        std::wstring name = entry.name;
        if (!name.empty() && std::wstring_view(L"=+-@\t\r\n").find(name.front()) != std::wstring_view::npos)
            name.insert(name.begin(), L'\'');
        auto line = std::string("\"");
        for (const char ch : utf8(name)) { if (ch == '"') line.push_back(ch); line.push_back(ch); }
        line += entry.directory ? "\",directory,\r\n" : "\",file," + std::to_string(entry.size) + "\r\n";
        output.write(line.data(), line.size());
    }
    check_cancel(cancelled);
    output.commit();
}

std::wstring sha256_file(const std::wstring& path, std::size_t buffer_size, const Report& report,
                        const IsCancelled& cancelled) {
    if (!buffer_size) throw Failure(Error::Input);
    struct Sha256 {
        BCRYPT_ALG_HANDLE algorithm{};
        BCRYPT_HASH_HANDLE hash{};
        ~Sha256() { if (hash) BCryptDestroyHash(hash); if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0); }
    };
    std::vector<unsigned char> state;
    Sha256 sha;
    const auto validate = [](NTSTATUS status) {
        if (status < 0) throw Failure(Error::Unexpected, static_cast<std::uint32_t>(status), L"SHA-256");
    };
    validate(BCryptOpenAlgorithmProvider(&sha.algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0));
    DWORD size = 0, actual = 0;
    validate(BCryptGetProperty(sha.algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&size), sizeof(size), &actual, 0));
    state.resize(size);
    validate(BCryptCreateHash(sha.algorithm, &sha.hash, state.data(), size, nullptr, 0, 0));
    InputFile input(path);
    std::vector<unsigned char> block(buffer_size);
    ProgressMeter progress(report);
    progress.update(Stage::Verifying, 0, input.size(), true);
    for (std::uint64_t done = 0; done < input.size();) {
        check_cancel(cancelled);
        const auto count = static_cast<ULONG>(std::min<std::uint64_t>(block.size(), input.size() - done));
        input.read(done, block.data(), count);
        validate(BCryptHashData(sha.hash, block.data(), count, 0));
        done += count;
        progress.update(Stage::Verifying, done, input.size());
    }
    check_cancel(cancelled);
    std::array<unsigned char, 32> digest{};
    validate(BCryptFinishHash(sha.hash, digest.data(), static_cast<ULONG>(digest.size()), 0));
    std::wstring result;
    for (const auto byte : digest) {
        result += L"0123456789abcdef"[byte >> 4];
        result += L"0123456789abcdef"[byte & 15];
    }
    return result;
}

std::wstring Storage::settings() const { return combine(folder, L"settings.ini"); }
std::wstring Storage::log() const { return combine(folder, L"activity.log"); }

Storage resolve_storage() {
    wchar_t module[32768]{};
    const auto count = GetModuleFileNameW(nullptr, module, static_cast<DWORD>(std::size(module)));
    if (!count || count == std::size(module)) win_failure(Error::Settings);
    const auto executable_folder = parent_path(module);
    if (GetFileAttributesW(combine(executable_folder, L"portable.flag").c_str()) != INVALID_FILE_ATTRIBUTES)
        return {executable_folder, true};
    PWSTR profile = nullptr;
    const auto result = SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &profile);
    struct FreeFolder { PWSTR value; ~FreeFolder() { CoTaskMemFree(value); } } owned{profile};
    check_hr(result, Error::Settings, L"LocalAppData");
    return {combine(profile, product_name), false};
}

Settings load_settings(const Storage& storage) {
    Settings initial;
    if (PRIMARYLANGID(GetUserDefaultUILanguage()) != LANG_GERMAN) initial.language = Language::English;
    try {
        const InputFile input(storage.settings());
        if (input.size() > 64 * 1024) throw Failure(Error::Settings, 0, storage.settings());
        std::string content(static_cast<std::size_t>(input.size()), '\0');
        input.read(0, content.data(), content.size());
        return parse_settings(content, initial);
    } catch (const Failure& failure) {
        if (failure.system_code() == ERROR_FILE_NOT_FOUND || failure.system_code() == ERROR_PATH_NOT_FOUND) return initial;
        throw;
    }
}

void save_settings(const Storage& storage, const Settings& settings) {
    if (!storage.portable) create_storage_directory(storage);
    const auto content = serialize_settings(settings);
    AtomicOutput output(storage.settings(), true);
    output.write(content.data(), content.size());
    output.commit();
}

bool log_event(const Storage& storage, const std::wstring& message) noexcept {
    try {
        if (!storage.portable) create_storage_directory(storage);
        const auto path = win_path(storage.log());
        WIN32_FILE_ATTRIBUTE_DATA attrs{};
        if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &attrs) && (attrs.nFileSizeHigh || attrs.nFileSizeLow >= 256 * 1024)) {
            if (!MoveFileExW(path.c_str(), (path + L".1").c_str(), MOVEFILE_REPLACE_EXISTING)) return false;
        }
        Handle output(CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                  nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        if (!output) return false;
        BY_HANDLE_FILE_INFORMATION info{};
        if (!GetFileInformationByHandle(output.get(), &info) || (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) return false;
        SYSTEMTIME time{};
        GetLocalTime(&time);
        wchar_t stamp[32]{};
        swprintf(stamp, std::size(stamp), L"%04u-%02u-%02u %02u:%02u:%02u ",
                 time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond);
        auto line = message;
        for (auto& ch : line) if (ch == L'\r' || ch == L'\n') ch = L' ';
        const auto data = utf8(std::wstring(stamp) + line + L"\r\n");
        DWORD written = 0;
        return WriteFile(output.get(), data.data(), static_cast<DWORD>(data.size()), &written, nullptr) && written == data.size();
    } catch (...) {
        return false;
    }
}

SystemResources system_resources(bool accelerated) noexcept {
    SystemResources result;
    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof(memory);
    if (GlobalMemoryStatusEx(&memory)) {
        result.installed = memory.ullTotalPhys;
        result.available = memory.ullAvailPhys;
        result.memory_load = memory.dwMemoryLoad;
    }
    SYSTEM_INFO system{};
    GetNativeSystemInfo(&system);
    result.processors = system.dwNumberOfProcessors;
    result.process_64_bit = sizeof(void*) == 8;
    using VersionFn = LONG (WINAPI*)(OSVERSIONINFOW*);
    const auto proc = GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion");
    VersionFn version_fn = nullptr;
    static_assert(sizeof(version_fn) == sizeof(proc));
    memcpy(&version_fn, &proc, sizeof(version_fn));
    OSVERSIONINFOW version_info{};
    version_info.dwOSVersionInfoSize = sizeof(version_info);
    if (version_fn && version_fn(&version_info) == 0) {
        result.windows_major = version_info.dwMajorVersion;
        result.windows_minor = version_info.dwMinorVersion;
    }
    result.display_accelerated = accelerated;
    return result;
}

bool display_accelerated() noexcept {
    if (GetSystemMetrics(SM_REMOTESESSION)) return false;
    DISPLAY_DEVICEW device{};
    device.cb = sizeof(device);
    for (DWORD i = 0; EnumDisplayDevicesW(nullptr, i, &device, 0); ++i) {
        if ((device.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE) &&
            !(device.StateFlags & DISPLAY_DEVICE_MIRRORING_DRIVER)) return true;
    }
    return false;
}

ProcessMemory process_memory(bool include_virtual) noexcept {
    PROCESS_MEMORY_COUNTERS_EX memory{};
    memory.cb = sizeof(memory);
    if (!GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory))) return {};
    std::uint64_t virtual_bytes = 0;
    if (include_virtual) {
        SYSTEM_INFO system{};
        GetSystemInfo(&system);
        auto address = reinterpret_cast<std::uintptr_t>(system.lpMinimumApplicationAddress);
        const auto end = reinterpret_cast<std::uintptr_t>(system.lpMaximumApplicationAddress);
        MEMORY_BASIC_INFORMATION information{};
        while (address < end && VirtualQuery(reinterpret_cast<const void*>(address), &information, sizeof(information))) {
            if (information.State == MEM_COMMIT || information.State == MEM_RESERVE) virtual_bytes += information.RegionSize;
            const auto next = reinterpret_cast<std::uintptr_t>(information.BaseAddress) + information.RegionSize;
            if (next <= address) break;
            address = next;
        }
    }
    return {memory.PrivateUsage, memory.WorkingSetSize, virtual_bytes};
}

}
