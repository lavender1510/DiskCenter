#pragma once

#include "core.hpp"
#include "iso9660.hpp"

#include <windows.h>
#include <objbase.h>
#include <string>
#include <vector>

namespace dc {

std::wstring win_path(const std::wstring& path);
std::wstring system_message(std::uint32_t code);
[[noreturn]] void win_failure(Error error, const std::wstring& detail = {});
void check_hr(HRESULT result, Error error, const std::wstring& detail = {});

template <class Interface>
class ComPtr {
public:
    ComPtr() = default;
    explicit ComPtr(Interface* pointer) : ptr_(pointer) {}
    ~ComPtr() { if (ptr_) ptr_->Release(); }
    ComPtr(ComPtr&& rhs) noexcept : ptr_(rhs.ptr_) { rhs.ptr_ = nullptr; }
    ComPtr& operator=(ComPtr&& rhs) noexcept {
        if (this != &rhs) {
            if (ptr_) ptr_->Release();
            ptr_ = rhs.ptr_;
            rhs.ptr_ = nullptr;
        }
        return *this;
    }
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;
    Interface* get() const noexcept { return ptr_; }
    Interface* operator->() const noexcept { return ptr_; }
    explicit operator bool() const noexcept { return ptr_ != nullptr; }
    Interface** put() noexcept {
        if (ptr_) ptr_->Release();
        ptr_ = nullptr;
        return &ptr_;
    }
private:
    Interface* ptr_ = nullptr;
};

class ComApartment {
public:
    explicit ComApartment(DWORD mode = COINIT_APARTMENTTHREADED);
    ~ComApartment();
    ComApartment(const ComApartment&) = delete;
    ComApartment& operator=(const ComApartment&) = delete;
};

class Handle {
public:
    Handle() = default;
    explicit Handle(HANDLE handle) : handle_(handle) {}
    ~Handle() { reset(); }
    Handle(Handle&& other) noexcept;
    Handle& operator=(Handle&& other) noexcept;
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    HANDLE get() const noexcept { return handle_; }
    explicit operator bool() const noexcept { return handle_ != INVALID_HANDLE_VALUE && handle_ != nullptr; }
    void reset() noexcept;
private:
    HANDLE handle_ = INVALID_HANDLE_VALUE;
};

struct FileIdentity {
    DWORD volume = 0, index_high = 0, index_low = 0;
    std::uint64_t size = 0, modified = 0;
    bool operator==(const FileIdentity& value) const noexcept;
};

class InputFile {
public:
    explicit InputFile(const std::wstring& path);
    FileIdentity identity() const noexcept { return identity_; }
    std::uint64_t size() const noexcept { return identity_.size; }
    void read(std::uint64_t offset, void* buffer, std::size_t size) const;
    ReadAt reader() const;
private:
    std::wstring path_;
    Handle input_;
    FileIdentity identity_;
};

class AtomicOutput {
public:
    explicit AtomicOutput(const std::wstring& path, bool replace = false);
    ~AtomicOutput();
    AtomicOutput(const AtomicOutput&) = delete;
    AtomicOutput& operator=(const AtomicOutput&) = delete;
    void write(const void* buffer, std::size_t size);
    void commit();
private:
    std::wstring final_;
    std::wstring temporary_;
    bool replace_;
    Handle handle_;
};

struct Entry {
    std::wstring name;
    std::uint64_t size = 0;
    bool directory = false;
    bool safe = true;
    IsoEntry iso;
    std::uint64_t modified = 0;
};

std::wstring combine(const std::wstring& folder, const std::wstring& name);
std::wstring parent_path(const std::wstring& path);
std::wstring file_name(const std::wstring& path);
std::vector<Entry> read_directory(const std::wstring& folder, const IsCancelled& cancelled);
void check_image_destination(const std::wstring& source, const std::wstring& destination);
void check_image_source(const std::wstring& source, const std::wstring& destination, const IsCancelled& cancelled);

class ProgressMeter {
public:
    explicit ProgressMeter(Report report) : report_(std::move(report)) {}
    void update(Stage stage, std::uint64_t done, std::uint64_t total, bool force = false);
private:
    Report report_;
    ULONGLONG start_ = GetTickCount64();
    ULONGLONG updated_ = 0;
};

void copy_range(const InputFile& input, std::uint64_t offset, std::uint64_t count,
                const std::wstring& destination, std::size_t buffer_size, const Report& report,
                const IsCancelled& cancelled);
void write_catalog(const std::wstring& path, const std::vector<Entry>& entries, const IsCancelled& cancelled);
std::wstring sha256_file(const std::wstring& path, std::size_t buffer_size,
                        const Report& report, const IsCancelled& cancelled);

struct Storage {
    std::wstring folder;
    bool portable = false;
    std::wstring settings() const;
    std::wstring log() const;
};

Storage resolve_storage();
Settings load_settings(const Storage& storage);
void save_settings(const Storage& storage, const Settings& settings);
bool log_event(const Storage& storage, const std::wstring& message) noexcept;
SystemResources system_resources(bool display_accelerated) noexcept;
bool display_accelerated() noexcept;
struct ProcessMemory { std::uint64_t private_bytes = 0, working_set = 0, virtual_bytes = 0; };
ProcessMemory process_memory(bool include_virtual = false) noexcept;

}
