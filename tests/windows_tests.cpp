#include "optical.hpp"
#include "test_common.hpp"

#include <filesystem>
#include <winioctl.h>

namespace fs = std::filesystem;

int main(int argc, char** argv) {
    using namespace dc;
    try {
        ComApartment com;
        auto base = fs::temp_directory_path() / (L"disccenter-tests-" + std::to_wstring(GetCurrentProcessId()) +
                                                 L"-" + std::to_wstring(GetTickCount64()));
        fs::create_directory(base);
        struct Cleanup {
            fs::path dir;
            ~Cleanup() { std::error_code error; fs::remove_all(dir, error); }
        } cleanup{base};
        const auto directory = base / L"source";
        fs::create_directory(directory);
        const auto input = (directory / L"message.txt").wstring();
        const auto output = (base / L"copy.txt").wstring();
        const auto catalog = (base / L"catalog.csv").wstring();
        const auto abandoned = (base / L"cancelled.txt").wstring();
        const auto create = [](const std::wstring& file, std::string_view data) {
            AtomicOutput target(file);
            target.write(data.data(), data.size());
            target.commit();
        };
        create(input, "abc");
        const auto hash = sha256_file(input, 1024 * 1024, {}, {});
        expect(hash == L"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "SHA-256 known digest");
        const InputFile source(input);
        copy_range(source, 0, source.size(), output, 4096, {}, {});
        expect(sha256_file(output, 4096, {}, {}) == hash, "block copy and digest");
        expect_error(Error::Exists, [&] { create(output, "other"); }, "no overwrites");
        const auto raced = (base / L"raced.txt").wstring();
        {
            AtomicOutput pending(raced);
            pending.write("late", 4);
            create(raced, "existing");
            expect_error(Error::Exists, [&] { pending.commit(); }, "a destination appearing before commit survives");
            const InputFile retained(raced);
            char data[8]{};
            retained.read(0, data, sizeof(data));
            expect(std::string_view(data, sizeof(data)) == "existing", "retained contents after racing commit");
        }
        expect_error(Error::Cancelled, [&] {
            copy_range(source, 0, source.size(), abandoned, 1, {}, [] { return true; });
        }, "block copy cancellation");
        expect(!fs::exists(abandoned), "no partial target");
        expect(fs::directory_iterator(base) != fs::directory_iterator(), "working test directory");
        for (const auto& entry : fs::directory_iterator(base))
            expect(entry.path().extension() != L".partial", "temporary file cleanup");
        expect_error(Error::UnsafePath, [&] { InputFile invalid(input + L":ads"); }, "no alternate data streams");
        expect_error(Error::UnsafePath, [&] { check_image_source(directory.wstring(), (directory / L"self.iso").wstring(), {}); },
                     "no output beneath image source");
        const auto entries = read_directory(directory.wstring(), {});
        expect(entries.size() == 1 && entries[0].name == L"message.txt" && entries[0].size == 3, "directory listing");
        write_catalog(catalog, entries, {});
        const InputFile exported(catalog);
        expect(exported.size() > 17, "CSV written");
        auto storage = Storage{base.wstring(), true};
        Settings settings;
        settings.profile = ProfileChoice::Modern;
        save_settings(storage, settings);
        settings.profile = ProfileChoice::Classic;
        save_settings(storage, settings);
        expect(load_settings(storage).profile == ProfileChoice::Classic, "atomic setting replacement");
        expect(log_event(storage, L"Integration test"), "logging");
        (void)optical_drives();
        const auto sparse_path = (base / L"sparse.bin").wstring();
        const auto native_sparse = win_path(sparse_path);
        Handle sparse(CreateFileW(native_sparse.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
        expect(static_cast<bool>(sparse), "create sparse-file fixture");
        DWORD returned = 0;
        if (DeviceIoControl(sparse.get(), FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &returned, nullptr)) {
            constexpr std::uint64_t offset = (1ULL << 32) + 23;
            LARGE_INTEGER at{};
            at.QuadPart = offset;
            expect(SetFilePointerEx(sparse.get(), at, nullptr, FILE_BEGIN) &&
                   WriteFile(sparse.get(), "Z", 1, &returned, nullptr) && returned == 1, "write above 4 GiB");
            sparse.reset();
            InputFile sparse_source(sparse_path);
            char byte = 0;
            sparse_source.read(offset, &byte, 1);
            expect(sparse_source.size() == offset + 1 && byte == 'Z', "64-bit read offset");
        } else {
            std::cout << "Sparse-file scenario skipped by the test filesystem\n";
        }
        {
            const auto nested = directory / L"Unterordner";
            fs::create_directory(nested);
            create((nested / L"Grüße.txt").wstring(), "umlaut");
            create((directory / L"empty").wstring(), "");
            const auto image = argc > 1 ? fs::path(argv[1]).wstring() : (base / L"test.iso").wstring();
            create_image(directory.wstring(), image, {}, 1024 * 1024, {}, {});
            const InputFile iso(image);
            const IsoVolume volume(iso.size(), iso.reader());
            const auto items = volume.directory(volume.root(), iso.reader());
            expect(items.size() == 3 && items.front().directory && items.front().name == L"Unterordner",
                   "ISO directory order and Joliet");
            const auto subitems = volume.directory(items.front(), iso.reader());
            expect(subitems.size() == 1 && subitems.front().name == L"Grüße.txt" && subitems.front().size == 6,
                   "nested Unicode file");
            expect(items[1].name == L"empty" && items[1].size == 0 && items[2].size == source.size(), "ISO file sizes");
            char data[3]{};
            iso.read(items[2].offset, data, sizeof(data));
            expect(std::string_view(data, sizeof(data)) == "abc", "ISO file data");
        }
        std::cout << "Windows integration tests passed\n";
    } catch (const Failure& failure) {
        std::wcerr << describe(failure.code(), Language::English) << L" (0x" << std::hex << failure.system_code()
                   << L") " << failure.detail() << L"\n" << system_message(failure.system_code()) << L'\n';
        return 1;
    }
}
