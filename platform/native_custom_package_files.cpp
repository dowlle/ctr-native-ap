#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include <platform/native_custom_package.h>
#include <platform/native_custom_offline.h>
#include <nlohmann/json.hpp>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
#include <atomic>
#include <memory>
#include <algorithm>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cerrno>
#include <sys/syscall.h>
#include <dirent.h>
#endif

namespace {
bool revision_name(const std::string &name)
{
    return name.size() == 64 && name.find_first_not_of("0123456789abcdef") == std::string::npos;
}
#ifdef CTR_PACKAGE_TESTING
thread_local int installFailureCountdown = -1;
extern "C" void CustomPackage_TestInstallFailure(int checkpoints) { installFailureCountdown = checkpoints; }
#endif
void install_checkpoint()
{
#ifdef CTR_PACKAGE_TESTING
    if (installFailureCountdown == 0) throw std::runtime_error("Injected package install interruption");
    if (installFailureCountdown > 0) --installFailureCountdown;
#endif
}
std::vector<std::string> components(const std::string &path)
{
    std::vector<std::string> parts;
    size_t start = 0;
    while (start < path.size())
    {
        auto end = path.find('/', start);
        auto part = path.substr(start, end == std::string::npos ? end : end - start);
        if (part.empty() || part == "." || part == ".." || part.find('\\') != std::string::npos)
            throw std::runtime_error("Ambiguous package directory component");
        parts.push_back(part);
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return parts;
}

#ifdef _WIN32
struct Handle
{
    HANDLE value;
    explicit Handle(HANDLE handle) : value(handle) { if (value == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot open package path"); }
    ~Handle() { CloseHandle(value); }
    Handle(const Handle &) = delete;
    Handle &operator=(const Handle &) = delete;
    Handle(Handle &&other) noexcept : value(other.value) { other.value = INVALID_HANDLE_VALUE; }
};

std::wstring wide(const std::string &text)
{
    int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), (int)text.size(), nullptr, 0);
    if (!size) throw std::runtime_error("Package directory is not valid UTF-8");
    std::wstring result((size_t)size, 0);
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), (int)text.size(), &result[0], size) != size)
        throw std::runtime_error("Package directory conversion failed");
    return result;
}

Handle open_windows(const std::wstring &path, bool directory, bool renameable = false)
{
    Handle result(CreateFileW(path.c_str(), (directory ? FILE_READ_ATTRIBUTES : GENERIC_READ) | (renameable ? DELETE : 0),
        FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT | (directory ? FILE_FLAG_BACKUP_SEMANTICS : 0), nullptr));
    BY_HANDLE_FILE_INFORMATION info;
    if (!GetFileInformationByHandle(result.value, &info) || GetFileType(result.value) != FILE_TYPE_DISK ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
        bool(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != directory)
        throw std::runtime_error("Package path is reparse-pointed or has wrong file type");
    return result;
}

struct Directory
{
    std::wstring path;
    std::vector<Handle> pinned;
    explicit Directory(std::string root, bool renameable = false)
    {
        for (char &c : root) if (c == '\\') c = '/';
        if (root.size() < 3 || root[1] != ':' || root[2] != '/' ||
            !((root[0] >= 'A' && root[0] <= 'Z') || (root[0] >= 'a' && root[0] <= 'z')))
            throw std::runtime_error("Package directory needs an absolute Windows drive path");
        path = L"\\\\?\\" + wide(root.substr(0, 3));
        path.back() = L'\\';
        pinned.push_back(open_windows(path, true));
        const auto parts = components(root.substr(3));
        for (size_t i = 0; i < parts.size(); i++)
        {
            const auto &part = parts[i];
            if (part.find(':') != std::string::npos || part.back() == '.' || part.back() == ' ')
                throw std::runtime_error("Ambiguous Windows package directory");
            if (path.back() != L'\\') path += L'\\';
            path += wide(part);
            pinned.push_back(open_windows(path, true, renameable && i + 1 == parts.size()));
        }
    }
    std::wstring child(const std::string &name) const { return path + (path.back() == L'\\' ? L"" : L"\\") + wide(name); }
    std::vector<std::string> revisions()
    {
        WIN32_FIND_DATAW data;
        HANDLE search = FindFirstFileW(child("*").c_str(), &data);
        if (search == INVALID_HANDLE_VALUE)
        {
            if (GetLastError() == ERROR_FILE_NOT_FOUND) return {};
            throw std::runtime_error("Cannot enumerate package store");
        }
        std::vector<std::string> names;
        try
        {
            do
            {
                std::string name;
                for (size_t i = 0; data.cFileName[i] && i < 65; i++)
                {
                    if (data.cFileName[i] > 127) { name.clear(); break; }
                    name += (char)data.cFileName[i];
                }
                if (!revision_name(name)) continue;
                if (names.size() == 1024) throw std::runtime_error("Package store exceeds 1024 revisions");
                names.push_back(name);
            } while (FindNextFileW(search, &data));
            if (GetLastError() != ERROR_NO_MORE_FILES) throw std::runtime_error("Package store enumeration failed");
        }
        catch (...) { FindClose(search); throw; }
        FindClose(search);
        std::sort(names.begin(), names.end());
        return names;
    }
    bool exists(const std::string &name)
    {
        if (GetFileAttributesW(child(name).c_str()) != INVALID_FILE_ATTRIBUTES) return true;
        if (GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND) return false;
        throw std::runtime_error("Cannot inspect existing package revision");
    }
    bool mkdir(const std::string &name)
    {
        if (CreateDirectoryW(child(name).c_str(), nullptr)) return true;
        if (GetLastError() == ERROR_ALREADY_EXISTS) return false;
        throw std::runtime_error("Cannot create package staging directory");
    }
    void write(const std::string &relative, const void *data, size_t size)
    {
        auto parts = components(relative);
        std::wstring current = path;
        std::vector<Handle> directories;
        for (size_t i = 0; i + 1 < parts.size(); i++)
        {
            if (current.back() != L'\\') current += L'\\';
            current += wide(parts[i]);
            if (!CreateDirectoryW(current.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
                throw std::runtime_error("Cannot create package payload directory");
            directories.push_back(open_windows(current, true));
        }
        if (current.back() != L'\\') current += L'\\';
        Handle file(CreateFileW((current + wide(parts.back())).c_str(), GENERIC_WRITE, 0, nullptr,
                                CREATE_NEW, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        DWORD written = 0;
        if (!WriteFile(file.value, data, (DWORD)size, &written, nullptr) || written != size || !FlushFileBuffers(file.value))
            throw std::runtime_error("Package payload write/flush failed");
    }
    void publish(Directory &stage, const std::string &, const std::string &name)
    {
        auto destination = child(name);
        std::vector<unsigned char> storage(sizeof(FILE_RENAME_INFO) + destination.size() * sizeof(wchar_t), 0);
        auto *info = reinterpret_cast<FILE_RENAME_INFO *>(storage.data());
        info->ReplaceIfExists = FALSE;
        info->RootDirectory = nullptr;
        info->FileNameLength = (DWORD)(destination.size() * sizeof(wchar_t));
        std::memcpy(info->FileName, destination.data(), info->FileNameLength);
        if (!SetFileInformationByHandle(stage.pinned.back().value, FileRenameInfo, info, (DWORD)storage.size()))
            throw std::runtime_error("Cannot publish package without replacement (Windows error " + std::to_string(GetLastError()) + ")");
    }
    void sync_publication() {} // Payloads were flushed; do not claim a Windows directory-flush guarantee.
    std::vector<unsigned char> read(const std::string &relative, size_t maximum)
    {
        auto parts = components(relative);
        if (parts.empty()) throw std::runtime_error("Package file path required");
        std::wstring current = path;
        std::vector<Handle> directories;
        for (size_t i = 0; i + 1 < parts.size(); i++)
        {
            if (current.back() != L'\\') current += L'\\';
            current += wide(parts[i]);
            directories.push_back(open_windows(current, true));
        }
        if (current.back() != L'\\') current += L'\\';
        auto file = open_windows(current + wide(parts.back()), false);
        LARGE_INTEGER size;
        if (!GetFileSizeEx(file.value, &size) || size.QuadPart <= 0 || (unsigned long long)size.QuadPart > maximum)
            throw std::runtime_error("Package file size outside bounds");
        std::vector<unsigned char> bytes((size_t)size.QuadPart);
        DWORD got = 0;
        if (!ReadFile(file.value, bytes.data(), (DWORD)bytes.size(), &got, nullptr) || got != bytes.size())
            throw std::runtime_error("Package file short read");
        unsigned char extra;
        if (!ReadFile(file.value, &extra, 1, &got, nullptr) || got)
            throw std::runtime_error("Package file grew during read");
        return bytes;
    }
};
#else
struct Handle
{
    int value;
    explicit Handle(int fd) : value(fd)
    {
        if (value >= 0) return;
        const int saved = errno;
        if (saved == ENOENT) throw std::runtime_error("Package file or directory is missing");
        if (saved == EACCES || saved == EPERM) throw std::runtime_error("Package path access was denied");
        if (saved == ELOOP) throw std::runtime_error("Package path contains a symbolic link");
        throw std::runtime_error(std::string("Cannot open package path: ") + std::strerror(saved));
    }
    ~Handle() { if (value >= 0) close(value); }
    Handle(const Handle &) = delete;
    Handle &operator=(const Handle &) = delete;
    Handle(Handle &&other) noexcept : value(other.value) { other.value = -1; }
    Handle &operator=(Handle &&other) noexcept
    {
        if (value >= 0) close(value);
        value = other.value; other.value = -1; return *this;
    }
};
constexpr int directoryFlags = O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC;
struct Directory
{
    Handle root;
    explicit Directory(const std::string &path, bool = false) : root(open("/", directoryFlags))
    {
        if (path.empty() || path[0] != '/') throw std::runtime_error("Package directory must be absolute");
        for (const auto &part : components(path.substr(1)))
            root = Handle(openat(root.value, part.c_str(), directoryFlags));
    }
    std::vector<std::string> revisions()
    {
        int copy = dup(root.value);
        if (copy < 0) throw std::runtime_error("Cannot duplicate package store handle");
        DIR *directory = fdopendir(copy);
        if (!directory) { close(copy); throw std::runtime_error("Cannot enumerate package store"); }
        std::vector<std::string> names;
        try
        {
            for (;;)
            {
                errno = 0;
                auto *entry = readdir(directory);
                if (!entry)
                {
                    if (errno) throw std::runtime_error("Package store enumeration failed");
                    break;
                }
                if (!revision_name(entry->d_name)) continue;
                if (names.size() == 1024) throw std::runtime_error("Package store exceeds 1024 revisions");
                names.emplace_back(entry->d_name);
            }
        }
        catch (...) { closedir(directory); throw; }
        closedir(directory);
        std::sort(names.begin(), names.end());
        return names;
    }
    bool exists(const std::string &name)
    {
        struct stat info;
        if (!fstatat(root.value, name.c_str(), &info, AT_SYMLINK_NOFOLLOW)) return true;
        if (errno == ENOENT) return false;
        throw std::runtime_error("Cannot inspect existing package revision");
    }
    bool mkdir(const std::string &name)
    {
        if (!mkdirat(root.value, name.c_str(), 0700)) return true;
        if (errno == EEXIST) return false;
        throw std::runtime_error("Cannot create package staging directory");
    }
    void write(const std::string &relative, const void *data, size_t size)
    {
        auto parts = components(relative);
        Handle directory(dup(root.value));
        for (size_t i = 0; i + 1 < parts.size(); i++)
        {
            if (mkdirat(directory.value, parts[i].c_str(), 0700) && errno != EEXIST)
                throw std::runtime_error("Cannot create package payload directory");
            if (fsync(directory.value)) throw std::runtime_error("Package directory sync failed");
            directory = Handle(openat(directory.value, parts[i].c_str(), directoryFlags));
        }
        Handle file(openat(directory.value, parts.back().c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600));
        size_t offset = 0;
        while (offset < size)
        {
            auto count = ::write(file.value, (const unsigned char *)data + offset, size - offset);
            if (count < 0 && errno == EINTR) continue;
            if (count <= 0) throw std::runtime_error("Package payload write failed");
            offset += (size_t)count;
        }
        if (fsync(file.value) || fsync(directory.value)) throw std::runtime_error("Package payload sync failed");
    }
    void publish(Directory &stage, const std::string &stageName, const std::string &name)
    {
        if (fsync(stage.root.value)) throw std::runtime_error("Package staging sync failed");
        // Linux RENAME_NOREPLACE. Never fall back to overwriting rename().
        if (syscall(SYS_renameat2, root.value, stageName.c_str(), root.value, name.c_str(), 1u))
            throw std::runtime_error("Cannot publish package without replacement");
    }
    void sync_publication()
    {
        if (fsync(root.value)) throw std::runtime_error("Revision published but store sync failed; verify before retry");
    }
    std::vector<unsigned char> read(const std::string &relative, size_t maximum)
    {
        auto parts = components(relative);
        if (parts.empty()) throw std::runtime_error("Package file path required");
        Handle directory(dup(root.value));
        for (size_t i = 0; i + 1 < parts.size(); i++)
            directory = Handle(openat(directory.value, parts[i].c_str(), directoryFlags));
        Handle file(openat(directory.value, parts.back().c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK));
        struct stat info;
        if (fstat(file.value, &info) || !S_ISREG(info.st_mode) || info.st_size <= 0 || (unsigned long long)info.st_size > maximum)
            throw std::runtime_error("Package input is not a bounded regular file");
        std::vector<unsigned char> bytes((size_t)info.st_size);
        size_t offset = 0;
        while (offset < bytes.size())
        {
            auto got = ::read(file.value, bytes.data() + offset, bytes.size() - offset);
            if (got < 0 && errno == EINTR) continue;
            if (got <= 0) throw std::runtime_error("Package file short read");
            offset += (size_t)got;
        }
        unsigned char extra;
        ssize_t got;
        do { got = ::read(file.value, &extra, 1); } while (got < 0 && errno == EINTR);
        if (got != 0) throw std::runtime_error("Package file changed during read");
        return bytes;
    }
};
#endif
}


extern "C" int CustomPackage_AcquireDirectory(const char *path, const char *expected,
    CustomPackageOwned **out, char *error, size_t errorSize)
{
    if (error && errorSize) error[0] = 0;
    try
    {
        if (!path || std::strlen(path) > 32700 || !out || *out)
            throw std::runtime_error("Expected bounded directory and empty package output");
        Directory directory(path);
        auto json = directory.read("manifest.json", 65536);
        CustomPackageManifest manifest;
        if (!CustomPackage_ParseManifest((const char *)json.data(), json.size(), expected, &manifest, error, errorSize)) return 0;
        std::vector<std::vector<unsigned char>> buffers;
        std::vector<CustomPackageInput> inputs;
        buffers.reserve(manifest.count);
        for (unsigned int i = 0; i < manifest.count; i++)
        {
            const auto &file = manifest.files[i];
            buffers.push_back(directory.read(file.path, file.bytes));
            inputs.push_back({file.role, buffers.back().data(), buffers.back().size()});
        }
        return CustomPackage_AcquireBuffers((const char *)json.data(), json.size(), expected,
            inputs.data(), inputs.size(), out, error, errorSize);
    }
    catch (const std::exception &exception)
    {
        if (error && errorSize) std::snprintf(error, errorSize, "%s", exception.what());
        return 0;
    }
}

extern "C" int CustomPackage_Install(const CustomPackageOwned *package, const char *path,
                                     char *error, size_t errorSize)
{
    std::string staging;
    if (error && errorSize) error[0] = 0;
    try
    {
        CustomPackageManifest manifest;
        if (!path || std::strlen(path) > 32000 || !CustomPackage_GetManifest(package, &manifest))
            throw std::runtime_error("Expected verified package and bounded existing store directory");
        Directory store(path);
        std::string prefix(path);
        if (prefix.back() != '/' && prefix.back() != '\\') prefix += '/';
        if (store.exists(manifest.sha256))
        {
            CustomPackageOwned *existing = nullptr;
            if (!CustomPackage_AcquireDirectory((prefix + manifest.sha256).c_str(), manifest.sha256,
                                                 &existing, error, errorSize)) return 0;
            CustomPackage_Free(&existing);
            return 2;
        }
        static std::atomic<unsigned long> serial{0};
#ifdef _WIN32
        auto process = GetCurrentProcessId();
#else
        auto process = getpid();
#endif
        bool created = false;
        for (unsigned int attempt = 0; attempt < 100 && !created; attempt++)
        {
            staging = ".pending-" + std::string(manifest.sha256) + "-" + std::to_string(process) + "-" + std::to_string(++serial);
            created = store.mkdir(staging);
        }
        if (!created) { staging.clear(); throw std::runtime_error("No unused staging name available"); }
        Directory stage(prefix + staging, true);
        std::vector<unsigned char> bytes;
        for (unsigned int i = 0; i < manifest.count; i++)
        {
            install_checkpoint();
            const auto &file = manifest.files[i];
            bytes.resize(file.bytes);
            if (!CustomPackage_CopyFile(package, file.role, bytes.data(), bytes.size(), nullptr))
                throw std::runtime_error("Verified package copy failed");
            stage.write(file.path, bytes.data(), bytes.size());
        }
        bytes.resize(65536);
        size_t size = 0;
        if (!CustomPackage_CopyManifestJSON(package, bytes.data(), bytes.size(), &size))
            throw std::runtime_error("Verified manifest serialization failed");
        install_checkpoint();
        stage.write("manifest.json", bytes.data(), size);
        install_checkpoint();
        store.publish(stage, staging, manifest.sha256);
        staging.clear();
        store.sync_publication();
        return 1;
    }
    catch (const std::exception &exception)
    {
        if (error && errorSize) std::snprintf(error, errorSize, "%s%s%s", exception.what(),
            staging.empty() ? "" : "; staging retained: ", staging.c_str());
        return 0;
    }
}

extern "C" int CustomPackage_InstallAssets(const CustomPackageOwned *package, const char *assets,
    char *error, size_t errorSize)
{
    try
    {
        if (!package || !assets || std::strlen(assets) > 31000)
            throw std::runtime_error("Expected verified package and assets directory");
        std::string root(assets);
        while (root.size() > 1 && !(root.size() == 3 && root[1] == ':') &&
               (root.back() == '/' || root.back() == '\\')) root.pop_back();
        Directory assetDirectory(root);
        assetDirectory.mkdir("tracks");
        Directory tracks(root + "/tracks");
        tracks.mkdir("packages");
        return CustomPackage_Install(package, (root + "/tracks/packages").c_str(), error, errorSize);
    }
    catch (const std::exception &e)
    {
        if (error && errorSize) std::snprintf(error, errorSize, "%s", e.what());
        return 0;
    }
}

extern "C" int CustomPackage_InstallInbox(const char *assets, const char *pin, char *error, size_t errorSize)
{
    CustomPackageOwned *package = nullptr;
    if (error && errorSize) error[0] = 0;
    try
    {
        if (!assets || std::strlen(assets) > 31000 || !pin || std::strlen(pin) != 64 ||
            std::strspn(pin, "0123456789abcdef") != 64)
            throw std::runtime_error("Expected assets directory and exact lowercase package pin");
        std::string root(assets);
        while (root.size() > 1 && !(root.size() == 3 && root[1] == ':') &&
               (root.back() == '/' || root.back() == '\\')) root.pop_back();
        if (!CustomPackage_AcquireDirectory((root + "/tracks/inbox/" + pin).c_str(), pin,
                                             &package, error, errorSize)) return 0;
        int result = CustomPackage_InstallAssets(package, assets, error, errorSize);
        CustomPackage_Free(&package);
        return result;
    }
    catch (const std::exception &exception)
    {
        CustomPackage_Free(&package);
        if (error && errorSize) std::snprintf(error, errorSize, "%s", exception.what());
        return 0;
    }
}

extern "C" int CustomPackage_ScanStore(const char *path, CustomPackageStoreVisitor visitor, void *context,
                                      char *error, size_t errorSize)
{
    if (error && errorSize) error[0] = 0;
    try
    {
        if (!path || std::strlen(path) > 32000 || !visitor)
            throw std::runtime_error("Expected bounded store path and visitor");
        Directory store(path);
        std::string prefix(path);
        if (prefix.back() != '/' && prefix.back() != '\\') prefix += '/';
        for (const auto &pin : store.revisions())
        {
            CustomPackageStoreEntry entry = {};
            std::memcpy(entry.pin, pin.c_str(), sizeof entry.pin);
            CustomPackageOwned *package = nullptr;
            entry.verified = CustomPackage_AcquireDirectory((prefix + pin).c_str(), pin.c_str(),
                &package, entry.error, sizeof entry.error);
            if (entry.verified)
            {
                CustomPackage_GetManifest(package, &entry.manifest);
                std::memcpy(entry.local.uuid, entry.manifest.uuid, sizeof entry.local.uuid);
                entry.local.arcade = CustomOffline_PackageArcade(package);
                for (unsigned int i=0;i<entry.manifest.count;i++)
                {
                    const auto &file=entry.manifest.files[i];
                    if (std::strcmp(file.role,"presentation") || std::strcmp(file.path,"metadata/saphi-source.json") || file.bytes>4096) continue;
                    std::string bytes(file.bytes,'\0');
                    if (!CustomPackage_CopyFile(package,"presentation",bytes.data(),bytes.size(),nullptr)) continue;
                    try {
                        auto source=nlohmann::json::parse(bytes);
                        if (source.at("provider")!="projectsaphi" || source.at("schema_version")!=1 || source.at("version")!=entry.manifest.version) continue;
                        int track=source.at("track_id").get<int>(), lev=source.at("lev_media_id").get<int>(), vrm=source.at("vrm_media_id").get<int>();
                        auto author=source.at("author").get<std::string>();
                        if (track<=0 || lev<=0 || vrm<=0 || author.size()>=sizeof entry.local.author ||
                            std::any_of(author.begin(),author.end(),[](unsigned char c){return c<32 || c==127;})) continue;
                        entry.local.trackID=track;entry.local.levID=lev;entry.local.vrmID=vrm;
                        if (source.contains("sca_media_id") && source["sca_media_id"].is_number_integer() && source["sca_media_id"]>0)
                            entry.local.scaID=source["sca_media_id"].get<int>();
                        std::memcpy(entry.local.author,author.c_str(),author.size()+1);
                    } catch (const std::exception &) { /* Optional display metadata cannot grant admission. */ }
                }
            }
            CustomPackage_Free(&package);
            visitor(&entry, context);
        }
        return 1;
    }
    catch (const std::exception &exception)
    {
        if (error && errorSize) std::snprintf(error, errorSize, "%s", exception.what());
        return 0;
    }
}
