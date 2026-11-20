#include "optical.hpp"
#include "identity.hpp"
#include "iso_writer.hpp"

#include <algorithm>
#include <array>
#include <initializer_list>
#include <limits>
#include <winioctl.h>
#include <ntddcdrm.h>
#include <ocidl.h>
#include <oleauto.h>
#include <shlwapi.h>

namespace dc {
namespace {

class CriticalErrors {
public:
    CriticalErrors() : changed_(SetThreadErrorMode(SEM_FAILCRITICALERRORS, &previous_)) {}
    ~CriticalErrors() { if (changed_) SetThreadErrorMode(previous_, nullptr); }
private:
    DWORD previous_ = 0;
    BOOL changed_;
};

std::wstring device_name(const std::wstring& root) {
    if (root.size() != 3 || root[1] != L':' || root[2] != L'\\' || GetDriveTypeW(root.c_str()) != DRIVE_CDROM)
        throw Failure(Error::Input, 0, root);
    return L"\\\\.\\" + root.substr(0, 2);
}

class Variant {
public:
    Variant() { VariantInit(&value); }
    explicit Variant(long number) : Variant() { value.vt = VT_I4; value.lVal = number; }
    explicit Variant(bool flag) : Variant() { value.vt = VT_BOOL; value.boolVal = flag ? VARIANT_TRUE : VARIANT_FALSE; }
    explicit Variant(const std::wstring& text) : Variant() {
        value.vt = VT_BSTR;
        value.bstrVal = SysAllocStringLen(text.data(), static_cast<UINT>(text.size()));
        if (!value.bstrVal) throw std::bad_alloc();
    }
    explicit Variant(IDispatch* dispatch) : Variant() {
        value.vt = VT_DISPATCH; value.pdispVal = dispatch; dispatch->AddRef();
    }
    explicit Variant(IStream* stream) : Variant() {
        value.vt = VT_UNKNOWN; value.punkVal = stream; stream->AddRef();
    }
    ~Variant() { VariantClear(&value); }
    Variant(const Variant&) = delete;
    Variant& operator=(const Variant&) = delete;
    Variant(Variant&& rhs) noexcept : value(rhs.value) { VariantInit(&rhs.value); }
    VARIANT value;
};

void clear_exception(EXCEPINFO& failure) noexcept {
    SysFreeString(failure.bstrSource);
    SysFreeString(failure.bstrDescription);
    SysFreeString(failure.bstrHelpFile);
}

Variant invoke(IDispatch* object, const wchar_t* name, WORD operation, std::initializer_list<const Variant*> args = {}) {
    auto member = const_cast<wchar_t*>(name);
    DISPID id = DISPID_UNKNOWN;
    check_hr(object->GetIDsOfNames(IID_NULL, &member, 1, LOCALE_USER_DEFAULT, &id), Error::Imapi, name);
    std::array<VARIANTARG, 3> reversed{};
    if (args.size() > reversed.size()) throw Failure(Error::Unexpected);
    std::size_t i = args.size();
    for (const auto arg : args) reversed[--i] = arg->value;
    DISPID put_id = DISPID_PROPERTYPUT;
    DISPPARAMS params{reversed.data(), &put_id, static_cast<UINT>(args.size()),
                      operation == DISPATCH_PROPERTYPUT || operation == DISPATCH_PROPERTYPUTREF ? 1u : 0u};
    Variant output;
    EXCEPINFO exception{};
    const auto result = object->Invoke(id, IID_NULL, LOCALE_USER_DEFAULT, operation, &params, &output.value, &exception, nullptr);
    if (result == DISP_E_EXCEPTION && exception.pfnDeferredFillIn) exception.pfnDeferredFillIn(&exception);
    const auto code = result == DISP_E_EXCEPTION && FAILED(exception.scode) ? exception.scode : result;
    clear_exception(exception);
    check_hr(code, Error::Imapi, name);
    return output;
}

long number(const Variant& variant) {
    Variant value;
    auto input = variant.value;
    check_hr(VariantChangeType(&value.value, &input, 0, VT_I4), Error::Imapi);
    return value.value.lVal;
}

bool flag(const Variant& variant) {
    Variant value;
    auto input = variant.value;
    check_hr(VariantChangeType(&value.value, &input, 0, VT_BOOL), Error::Imapi);
    return value.value.boolVal != VARIANT_FALSE;
}

std::wstring text(const Variant& variant) {
    if (variant.value.vt != VT_BSTR || !variant.value.bstrVal) throw Failure(Error::Imapi, DISP_E_TYPEMISMATCH);
    return {variant.value.bstrVal, SysStringLen(variant.value.bstrVal)};
}

IUnknown* unknown(const Variant& variant) {
    if (variant.value.vt == VT_DISPATCH && variant.value.pdispVal) return variant.value.pdispVal;
    if (variant.value.vt == VT_UNKNOWN && variant.value.punkVal) return variant.value.punkVal;
    throw Failure(Error::Imapi, DISP_E_TYPEMISMATCH);
}

template <class Interface>
ComPtr<Interface> query(IUnknown* object, REFIID iid, Error error = Error::Imapi) {
    ComPtr<Interface> result;
    check_hr(object->QueryInterface(iid, reinterpret_cast<void**>(result.put())), error);
    return result;
}

ComPtr<IDispatch> activate(const wchar_t* class_id) {
    CLSID clsid{};
    check_hr(CLSIDFromString(class_id, &clsid), Error::Imapi, class_id);
    ComPtr<IDispatch> object;
    check_hr(CoCreateInstance(clsid, nullptr, CLSCTX_INPROC_SERVER | CLSCTX_LOCAL_SERVER, IID_IDispatch,
                              reinterpret_cast<void**>(object.put())), Error::Imapi, class_id);
    return object;
}

SAFEARRAY* array(const Variant& arg) {
    if (!(arg.value.vt & VT_ARRAY) || !arg.value.parray || SafeArrayGetDim(arg.value.parray) != 1)
        throw Failure(Error::Imapi, DISP_E_TYPEMISMATCH);
    return arg.value.parray;
}

std::pair<LONG, LONG> bounds(SAFEARRAY* arr) {
    LONG first = 0, last = -1;
    check_hr(SafeArrayGetLBound(arr, 1, &first), Error::Imapi);
    check_hr(SafeArrayGetUBound(arr, 1, &last), Error::Imapi);
    if (last >= std::numeric_limits<LONG>::max() || std::int64_t(last) - first > 4096)
        throw Failure(Error::Imapi);
    return {first, last};
}

bool has_volume(IDispatch* recorder, const std::wstring& root) {
    const auto names = invoke(recorder, L"VolumePathNames", DISPATCH_PROPERTYGET);
    const auto list = array(names);
    const auto [first, last] = bounds(list);
    for (LONG i = first; i <= last; ++i) {
        Variant path;
        if ((names.value.vt & VT_TYPEMASK) == VT_BSTR) {
            path.value.vt = VT_BSTR;
            check_hr(SafeArrayGetElement(list, &i, &path.value.bstrVal), Error::Imapi);
        } else if ((names.value.vt & VT_TYPEMASK) == VT_VARIANT) {
            check_hr(SafeArrayGetElement(list, &i, &path.value), Error::Imapi);
        } else throw Failure(Error::Imapi, DISP_E_TYPEMISMATCH);
        auto value = text(path);
        if (value.size() == 2 && value[1] == L':') value += L'\\';
        if (CompareStringOrdinal(value.c_str(), -1, root.c_str(), -1, TRUE) == CSTR_EQUAL) return true;
    }
    return false;
}

std::vector<long> supported_speeds(IDispatch* writer) {
    const auto speeds = invoke(writer, L"SupportedWriteSpeeds", DISPATCH_PROPERTYGET);
    const auto list = array(speeds);
    const auto [first, last] = bounds(list);
    std::vector<long> result;
    for (LONG i = first; i <= last; ++i) {
        long speed = 0;
        switch (speeds.value.vt & VT_TYPEMASK) {
        case VT_I4: check_hr(SafeArrayGetElement(list, &i, &speed), Error::Imapi); break;
        case VT_VARIANT: {
            Variant value;
            check_hr(SafeArrayGetElement(list, &i, &value.value), Error::Imapi);
            speed = number(value);
            break;
        }
        default: throw Failure(Error::Imapi, DISP_E_TYPEMISMATCH);
        }
        if (speed > 0) result.push_back(speed);
    }
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

ComPtr<IDispatch> find_recorder(const std::wstring& root) {
    (void)device_name(root);
    auto master = activate(L"{2735412E-7F64-5B0F-8F00-5D77AFBE261E}");
    if (!flag(invoke(master.get(), L"IsSupportedEnvironment", DISPATCH_PROPERTYGET)))
        throw Failure(Error::RecorderMissing, 0, root);
    const auto count = number(invoke(master.get(), L"Count", DISPATCH_PROPERTYGET));
    if (count < 0 || count > 64) throw Failure(Error::Imapi);
    for (long i = 0; i < count; ++i) {
        const Variant index(i);
        const auto id = invoke(master.get(), L"Item", DISPATCH_PROPERTYGET, {&index});
        auto recorder = activate(L"{2735412D-7F64-5B0F-8F00-5D77AFBE261E}");
        invoke(recorder.get(), L"InitializeDiscRecorder", DISPATCH_METHOD, {&id});
        if (has_volume(recorder.get(), root)) return recorder;
    }
    throw Failure(Error::RecorderMissing, 0, root);
}

class ExclusiveRecorder {
public:
    explicit ExclusiveRecorder(IDispatch* recorder) : recorder_(recorder), name_(std::wstring(product_name)) {
        const Variant force(false);
        invoke(recorder_, L"AcquireExclusiveAccess", DISPATCH_METHOD, {&force, &name_});
    }
    ~ExclusiveRecorder() {
        // A lost drive can make the release call fail. COM object destruction remains the fallback.
        try { invoke(recorder_, L"ReleaseExclusiveAccess", DISPATCH_METHOD); } catch (...) {}
    }
private:
    IDispatch* recorder_;
    Variant name_;
};

ComPtr<IDispatch> configure_writer(IDispatch* recorder) {
    auto writer = activate(L"{2735412A-7F64-5B0F-8F00-5D77AFBE261E}");
    const Variant target(recorder);
    if (!flag(invoke(writer.get(), L"IsRecorderSupported", DISPATCH_METHOD, {&target})) ||
        !flag(invoke(writer.get(), L"IsCurrentMediaSupported", DISPATCH_METHOD, {&target})))
        throw Failure(Error::UnsupportedMedia);
    invoke(writer.get(), L"Recorder", DISPATCH_PROPERTYPUT, {&target});
    const Variant client{std::wstring(product_name)};
    invoke(writer.get(), L"ClientName", DISPATCH_PROPERTYPUT, {&client});
    return writer;
}

MediaInfo medium(IDispatch* writer, bool speeds) {
    const auto type = number(invoke(writer, L"CurrentPhysicalMediaType", DISPATCH_PROPERTYGET));
    const auto free = number(invoke(writer, L"FreeSectorsOnMedia", DISPATCH_PROPERTYGET));
    const auto state = number(invoke(writer, L"CurrentMediaStatus", DISPATCH_PROPERTYGET));
    const bool looks_blank = flag(invoke(writer, L"MediaHeuristicallyBlank", DISPATCH_PROPERTYGET));
    constexpr long blank = 0x2;
    constexpr long nonblank = 0x4 | 0x8 | 0xfc00;
    return {static_cast<int>(type), free > 0 ? static_cast<std::uint64_t>(free) : 0,
            looks_blank && (state & blank) && !(state & nonblank),
            speeds ? supported_speeds(writer) : std::vector<long>{}};
}

class BurnVerification : public IUnknown {
public:
    virtual HRESULT STDMETHODCALLTYPE put_BurnVerificationLevel(int level) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_BurnVerificationLevel(int* level) = 0;
};

constexpr GUID verification_iid{0xd2ffd834, 0x958b, 0x426d, {0x84, 0x70, 0x2a, 0x13, 0x87, 0x9c, 0x6a, 0x91}};
constexpr GUID writer_events_iid{0x2735413c, 0x7f64, 0x5b0f, {0x8f, 0x00, 0x5d, 0x77, 0xaf, 0xbe, 0x26, 0x1e}};

class WriteEventInterface : public IDispatch {
public:
    virtual HRESULT STDMETHODCALLTYPE Update(IDispatch*, IDispatch*) noexcept = 0;
};

class WriterEvents final : public WriteEventInterface {
public:
    WriterEvents(IDispatch* writer, const Report& report, const IsCancelled& cancelled)
        : writer_(writer), report_(report), cancelled_(cancelled) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** result) override {
        if (!result) return E_POINTER;
        *result = nullptr;
        if (iid != IID_IUnknown && iid != IID_IDispatch && iid != writer_events_iid) return E_NOINTERFACE;
        *result = static_cast<IDispatch*>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return static_cast<ULONG>(InterlockedIncrement(&references_)); }
    ULONG STDMETHODCALLTYPE Release() override {
        const auto remaining = InterlockedDecrement(&references_);
        if (!remaining) delete this;
        return static_cast<ULONG>(remaining);
    }
    HRESULT STDMETHODCALLTYPE GetTypeInfoCount(UINT* count) override { if (!count) return E_POINTER; *count = 0; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetTypeInfo(UINT, LCID, ITypeInfo**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetIDsOfNames(REFIID, LPOLESTR*, UINT, LCID, DISPID*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Invoke(DISPID, REFIID, LCID, WORD, DISPPARAMS* parameters, VARIANT*, EXCEPINFO*, UINT*) override {
        if (!parameters || parameters->cArgs != 2) return DISP_E_BADPARAMCOUNT;
        const auto& arg = parameters->rgvarg[0];
        if (arg.vt != VT_DISPATCH || !arg.pdispVal) return DISP_E_TYPEMISMATCH;
        return Update(nullptr, arg.pdispVal);
    }
    // Some IMAPI2 registrations use the early-bound event interface.
    HRESULT STDMETHODCALLTYPE Update(IDispatch*, IDispatch* event) noexcept override {
        try {
            const auto action = number(invoke(event, L"CurrentAction", DISPATCH_PROPERTYGET));
            if (!cancel_sent_ && cancelled_ && cancelled_() && action < 5) {
                try {
                    invoke(writer_, L"CancelWrite", DISPATCH_METHOD);
                    cancel_sent_ = true;
                } catch (const Failure&) {
                    // IMAPI2 can reject a request during calibration. Retry at the next event.
                }
            }
            if (!report_) return S_OK;
            const auto remaining = std::max(0L, number(invoke(event, L"RemainingTime", DISPATCH_PROPERTYGET)));
            if (action == 4) {
                const auto start = number(invoke(event, L"StartLba", DISPATCH_PROPERTYGET));
                const auto count = std::max(0L, number(invoke(event, L"SectorCount", DISPATCH_PROPERTYGET)));
                const auto last = number(invoke(event, L"LastWrittenLba", DISPATCH_PROPERTYGET));
                const auto sectors = std::clamp(std::int64_t(last) - start + 1, std::int64_t(0), std::int64_t(count));
                const auto speed = std::max(0L, number(invoke(writer_, L"CurrentWriteSpeed", DISPATCH_PROPERTYGET)));
                report_({Stage::Writing, sectors * sector_size, std::uint64_t(count) * sector_size,
                         std::uint64_t(speed) * sector_size, static_cast<unsigned>(remaining)});
            } else {
                const auto stage = action == 7 ? Stage::Verifying : action == 5 || action == 6 ? Stage::Finalizing : Stage::Preparing;
                report_({stage, 0, 0, 0, static_cast<unsigned>(remaining)});
            }
        } catch (...) {
            // A progress property is optional during some media events.
        }
        return S_OK;
    }
private:
    volatile LONG references_ = 1;
    IDispatch* writer_;
    Report report_;
    IsCancelled cancelled_;
    bool cancel_sent_ = false;
};

class Subscription {
public:
    Subscription(IDispatch* writer, const Report& report, const IsCancelled& cancelled) {
        auto container = query<IConnectionPointContainer>(writer, IID_IConnectionPointContainer);
        check_hr(container->FindConnectionPoint(writer_events_iid, point_.put()), Error::Imapi, L"Write events");
        const ComPtr<IDispatch> sink(new WriterEvents(writer, report, cancelled));
        check_hr(point_->Advise(sink.get(), &cookie_), Error::Imapi, L"Write events");
    }
    ~Subscription() { if (cookie_) point_->Unadvise(cookie_); }
private:
    ComPtr<IConnectionPoint> point_;
    DWORD cookie_ = 0;
};

struct ImageStream {
    ComPtr<IDispatch> filesystem;
    ComPtr<IDispatch> result;
    ComPtr<IStream> stream;
};

ImageStream image_stream(const std::wstring& folder, const ImageOptions& options, const IsCancelled& cancelled) {
    if (options.label.size() > 32 || options.label.empty()) throw Failure(Error::Input, 0, L"Volume name");
    auto filesystem = activate(L"{2C941FC5-975B-59BE-A960-9A2A262853A5}");
    const Variant types(options.filesystem == FileSystem::Udf ? 4L : options.filesystem == FileSystem::Hybrid ? 7L : 3L);
    const Variant label(options.label);
    const Variant yes(true);
    const Variant no(false);
    // Keep the file contents on disk. The image is consumed through IStream.
    invoke(filesystem.get(), L"StageFiles", DISPATCH_PROPERTYPUT, {&no});
    invoke(filesystem.get(), L"FileSystemsToCreate", DISPATCH_PROPERTYPUT, {&types});
    invoke(filesystem.get(), L"StrictFileSystemCompliance", DISPATCH_PROPERTYPUT, {&yes});
    invoke(filesystem.get(), L"VolumeName", DISPATCH_PROPERTYPUT, {&label});
    auto root = query<IDispatch>(unknown(invoke(filesystem.get(), L"Root", DISPATCH_PROPERTYGET)), IID_IDispatch);
    check_cancel(cancelled);
    const Variant directory(folder);
    invoke(root.get(), L"AddTree", DISPATCH_METHOD, {&directory, &no});
    check_cancel(cancelled);
    auto result = query<IDispatch>(unknown(invoke(filesystem.get(), L"CreateResultImage", DISPATCH_METHOD)), IID_IDispatch);
    auto stream = query<IStream>(unknown(invoke(result.get(), L"ImageStream", DISPATCH_PROPERTYGET)), IID_IStream);
    return {std::move(filesystem), std::move(result), std::move(stream)};
}

std::uint64_t stream_size(IStream* stream) {
    STATSTG stat{};
    check_hr(stream->Stat(&stat, STATFLAG_NONAME), Error::Read, L"IStream::Stat");
    return stat.cbSize.QuadPart;
}

void write_stream(IDispatch* recorder, IStream* stream, const BurnOptions& options,
                  const Report& report, const IsCancelled& cancelled) {
    auto writer = configure_writer(recorder);
    const auto info = medium(writer.get(), options.sectors_per_second > 0);
    if (!info.blank) throw Failure(Error::MediaNotBlank);
    const auto bytes = stream_size(stream);
    if (!bytes || bytes % sector_size != 0) throw Failure(Error::InvalidImage);
    if (!fits_capacity(bytes, info.free_sectors)) throw Failure(Error::Capacity);
    const Variant yes(true);
    invoke(writer.get(), L"ForceMediaToBeClosed", DISPATCH_PROPERTYPUT, {&yes});
    if (options.verify) {
        auto verification = query<BurnVerification>(writer.get(), verification_iid, Error::Verification);
        check_hr(verification->put_BurnVerificationLevel(2), Error::Verification, L"Full verification");
    }
    if (options.sectors_per_second > 0) {
        if (!std::binary_search(info.write_speeds.begin(), info.write_speeds.end(), options.sectors_per_second))
            throw Failure(Error::Input, 0, L"Write speed");
        const Variant speed(options.sectors_per_second), cav(false);
        invoke(writer.get(), L"SetWriteSpeed", DISPATCH_METHOD, {&speed, &cav});
    }
    check_cancel(cancelled);
    Subscription subscription(writer.get(), report, cancelled);
    const Variant image(stream);
    try {
        invoke(writer.get(), L"Write", DISPATCH_METHOD, {&image});
    } catch (const Failure& failure) {
        constexpr std::uint32_t request_cancelled = 0xc0aa0002;
        if (failure.system_code() == request_cancelled) throw Failure(Error::Cancelled, failure.system_code(), options.drive);
        if (failure.system_code() == 0xc0aa0007) throw Failure(Error::Verification, failure.system_code(), options.drive);
        throw;
    }
}

std::uint64_t track_address(const UCHAR* address) {
    return (std::uint64_t(address[0]) << 24) | (std::uint64_t(address[1]) << 16) |
           (std::uint64_t(address[2]) << 8) | address[3];
}

std::uint64_t data_track_bytes(HANDLE device, const std::wstring& drive) {
    DWORD received = 0;
    CDROM_READ_TOC_EX request{};
    request.Format = CDROM_READ_TOC_EX_FORMAT_SESSION;
    CDROM_TOC_SESSION_DATA sessions{};
    if (!DeviceIoControl(device, IOCTL_CDROM_READ_TOC_EX, &request, sizeof(request), &sessions, sizeof(sessions), &received, nullptr))
        win_failure(Error::DiscLayout, drive);
    if (received < sizeof(sessions) || sessions.FirstCompleteSession != 1 || sessions.LastCompleteSession != 1)
        throw Failure(Error::DiscLayout, 0, drive);

    request.Format = CDROM_READ_TOC_EX_FORMAT_TOC;
    CDROM_TOC toc{};
    if (!DeviceIoControl(device, IOCTL_CDROM_READ_TOC_EX, &request, sizeof(request), &toc, sizeof(toc), &received, nullptr))
        win_failure(Error::DiscLayout, drive);
    if (received < 4 + 2 * sizeof(TRACK_DATA) || toc.FirstTrack != 1 || toc.LastTrack != 1 ||
        toc.TrackData[0].TrackNumber != 1 || !(toc.TrackData[0].Control & 4) ||
        toc.TrackData[1].TrackNumber != 0xaa || track_address(toc.TrackData[0].Address) != 0)
        throw Failure(Error::DiscLayout, 0, drive);
    return track_address(toc.TrackData[1].Address) * sector_size;
}

}

std::vector<Drive> optical_drives(const IsCancelled& cancelled) {
    CriticalErrors critical_errors;
    const auto count = GetLogicalDriveStringsW(0, nullptr);
    if (!count) win_failure(Error::Read);
    std::wstring paths(count + 1, L'\0');
    const auto copied = GetLogicalDriveStringsW(static_cast<DWORD>(paths.size()), paths.data());
    if (!copied || copied >= paths.size()) win_failure(Error::Read);
    std::vector<Drive> drives;
    for (auto path = paths.c_str(); *path; path += wcslen(path) + 1) {
        check_cancel(cancelled);
        if (GetDriveTypeW(path) != DRIVE_CDROM) continue;
        Drive drive;
        drive.root = path;
        std::array<wchar_t, 256> label{}, fs{};
        if (GetVolumeInformationW(path, label.data(), static_cast<DWORD>(label.size()), nullptr, nullptr, nullptr,
                                  fs.data(), static_cast<DWORD>(fs.size()))) {
            drive.state = MediaState::Mounted;
            drive.label = label.data();
            drive.filesystem = fs.data();
            ULARGE_INTEGER size{};
            if (GetDiskFreeSpaceExW(path, nullptr, &size, nullptr)) drive.capacity = size.QuadPart;
        } else {
            const auto error = GetLastError();
            drive.state = (error == ERROR_NOT_READY || error == ERROR_NO_MEDIA_IN_DRIVE)
                ? MediaState::NoMedia : MediaState::Unavailable;
        }
        drives.push_back(std::move(drive));
    }
    return drives;
}

void eject(const std::wstring& drive) {
    CriticalErrors critical_errors;
    const auto path = device_name(drive);
    Handle device(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr));
    if (!device) win_failure(Error::Open, drive);
    DWORD returned = 0;
    if (!DeviceIoControl(device.get(), IOCTL_STORAGE_EJECT_MEDIA, nullptr, 0, nullptr, 0, &returned, nullptr))
        win_failure(Error::Write, drive);
}

MediaInfo inspect_media(const std::wstring& drive) {
    CriticalErrors critical_errors;
    auto recorder = find_recorder(drive);
    auto writer = configure_writer(recorder.get());
    return medium(writer.get(), true);
}

void create_image(const std::wstring& folder, const std::wstring& destination, const ImageOptions& options,
                  std::size_t buffer_size, const Report& report, const IsCancelled& cancelled) {
    if (options.filesystem == FileSystem::IsoJoliet) {
        write_iso_joliet(folder, destination, options.label, buffer_size, report, cancelled);
        return;
    }
    check_image_source(folder, destination, cancelled);
    AtomicOutput output(destination);
    auto image = image_stream(folder, options, cancelled);
    const auto total = stream_size(image.stream.get());
    if (!total || !buffer_size || total % sector_size != 0) throw Failure(Error::InvalidImage);
    std::vector<unsigned char> buffer(buffer_size);
    ProgressMeter progress(report);
    progress.update(Stage::Writing, 0, total, true);
    for (std::uint64_t done = 0; done < total;) {
        check_cancel(cancelled);
        const auto bytes = static_cast<ULONG>(std::min<std::uint64_t>(buffer.size(), total - done));
        ULONG received = 0;
        check_hr(image.stream->Read(buffer.data(), bytes, &received), Error::Read, folder);
        if (received != bytes) throw Failure(Error::Read, ERROR_HANDLE_EOF, folder);
        output.write(buffer.data(), bytes);
        done += bytes;
        progress.update(Stage::Writing, done, total);
    }
    check_cancel(cancelled);
    output.commit();
}

void burn_image(const std::wstring& path, const BurnOptions& options, const Report& report, const IsCancelled& cancelled) {
    CriticalErrors critical_errors;
    const InputFile input(path);
    validate_data_image(input.size(), input.reader());
    ComPtr<IStream> stream;
    const auto filename = win_path(path);
    check_hr(SHCreateStreamOnFileEx(filename.c_str(), STGM_READ | STGM_SHARE_DENY_WRITE, 0, FALSE, nullptr, stream.put()),
             Error::Open, path);
    if (input.size() != stream_size(stream.get())) throw Failure(Error::Read, 0, path);
    check_cancel(cancelled);
    auto recorder = find_recorder(options.drive);
    ExclusiveRecorder exclusive(recorder.get());
    write_stream(recorder.get(), stream.get(), options, report, cancelled);
}

void burn_folder(const std::wstring& folder, const ImageOptions& image, const BurnOptions& options,
                 const Report& report, const IsCancelled& cancelled) {
    CriticalErrors critical_errors;
    check_image_source(folder, L"", cancelled);
    auto stream = image_stream(folder, image, cancelled);
    auto recorder = find_recorder(options.drive);
    ExclusiveRecorder exclusive(recorder.get());
    write_stream(recorder.get(), stream.stream.get(), options, report, cancelled);
}

void save_data_disc(const std::wstring& drive, const std::wstring& destination, std::size_t buffer_size,
                    const Report& report, const IsCancelled& cancelled) {
    CriticalErrors critical_errors;
    const auto path = device_name(drive);
    Handle device(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr));
    if (!device) win_failure(Error::Open, drive);
    const auto total = data_track_bytes(device.get(), drive);
    if (buffer_size < sector_size || buffer_size % sector_size != 0) throw Failure(Error::Input);
    DWORD returned = 0;
    if (!DeviceIoControl(device.get(), FSCTL_ALLOW_EXTENDED_DASD_IO, nullptr, 0, nullptr, 0, &returned, nullptr))
        win_failure(Error::Read, drive);
    const auto read = [&](std::uint64_t offset, void* data, std::size_t bytes) {
        LARGE_INTEGER position{};
        position.QuadPart = static_cast<LONGLONG>(offset);
        if (!SetFilePointerEx(device.get(), position, nullptr, FILE_BEGIN)) win_failure(Error::Read, drive);
        DWORD received = 0;
        if (!ReadFile(device.get(), data, static_cast<DWORD>(bytes), &received, nullptr)) win_failure(Error::Read, drive);
        if (received != bytes) throw Failure(Error::Read, ERROR_HANDLE_EOF, drive);
    };
    validate_data_image(total, read);
    AtomicOutput output(destination);
    std::vector<unsigned char> buffer(buffer_size);
    ProgressMeter progress(report);
    progress.update(Stage::Reading, 0, total, true);
    for (std::uint64_t done = 0; done < total;) {
        check_cancel(cancelled);
        const auto bytes = static_cast<std::size_t>(std::min<std::uint64_t>(buffer.size(), total - done));
        read(done, buffer.data(), bytes);
        output.write(buffer.data(), bytes);
        done += bytes;
        progress.update(Stage::Reading, done, total);
    }
    check_cancel(cancelled);
    output.commit();
}

}
