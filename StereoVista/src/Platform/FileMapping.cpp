#include "Platform/FileMapping.h"

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace Platform {

bool FileMapping::open(const std::string& utf8Path) {
    close();

    // UTF-8 -> UTF-16 for the wide Win32 file APIs (paths with non-ASCII
    // characters must survive; CreateFileA would mangle them).
    const int wideLen = MultiByteToWideChar(CP_UTF8, 0, utf8Path.c_str(), -1, nullptr, 0);
    if (wideLen <= 0)
        return false;
    std::wstring widePath(static_cast<size_t>(wideLen), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8Path.c_str(), -1, &widePath[0], wideLen);

    HANDLE file = CreateFileW(widePath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                              OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;

    LARGE_INTEGER fileSize{};
    if (!GetFileSizeEx(file, &fileSize) || fileSize.QuadPart <= 0) {
        CloseHandle(file);
        return false;
    }

    HANDLE mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (mapping == nullptr) {
        CloseHandle(file);
        return false;
    }

    const void* view = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
    if (view == nullptr) {
        CloseHandle(mapping);
        CloseHandle(file);
        return false;
    }

    fileHandle_ = file;
    mappingHandle_ = mapping;
    data_ = static_cast<const uint8_t*>(view);
    size_ = static_cast<size_t>(fileSize.QuadPart);
    return true;
}

void FileMapping::close() {
    if (data_ != nullptr)
        UnmapViewOfFile(data_);
    if (mappingHandle_ != nullptr)
        CloseHandle(static_cast<HANDLE>(mappingHandle_));
    if (fileHandle_ != nullptr)
        CloseHandle(static_cast<HANDLE>(fileHandle_));
    data_ = nullptr;
    size_ = 0;
    fileHandle_ = nullptr;
    mappingHandle_ = nullptr;
}

void FileMapping::moveFrom(FileMapping& other) noexcept {
    data_ = other.data_;
    size_ = other.size_;
    fileHandle_ = other.fileHandle_;
    mappingHandle_ = other.mappingHandle_;
    other.data_ = nullptr;
    other.size_ = 0;
    other.fileHandle_ = nullptr;
    other.mappingHandle_ = nullptr;
}

} // namespace Platform

#else // POSIX variant (local parsing sanity runs only; not the shipped path)

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace Platform {

bool FileMapping::open(const std::string& utf8Path) {
    close();

    const int fd = ::open(utf8Path.c_str(), O_RDONLY);
    if (fd < 0)
        return false;

    struct stat st {};
    if (fstat(fd, &st) != 0 || st.st_size <= 0) {
        ::close(fd);
        return false;
    }

    void* view = mmap(nullptr, static_cast<size_t>(st.st_size), PROT_READ, MAP_PRIVATE, fd, 0);
    if (view == MAP_FAILED) {
        ::close(fd);
        return false;
    }

    fd_ = fd;
    data_ = static_cast<const uint8_t*>(view);
    size_ = static_cast<size_t>(st.st_size);
    return true;
}

void FileMapping::close() {
    if (data_ != nullptr)
        munmap(const_cast<uint8_t*>(data_), size_);
    if (fd_ >= 0)
        ::close(fd_);
    data_ = nullptr;
    size_ = 0;
    fd_ = -1;
}

void FileMapping::moveFrom(FileMapping& other) noexcept {
    data_ = other.data_;
    size_ = other.size_;
    fd_ = other.fd_;
    other.data_ = nullptr;
    other.size_ = 0;
    other.fd_ = -1;
}

} // namespace Platform

#endif
