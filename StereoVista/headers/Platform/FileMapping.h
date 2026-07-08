#pragma once

// ============================================================================
// Platform::FileMapping — RAII read-only memory map of a whole file.
// ----------------------------------------------------------------------------
// The SLPK archive reader (Loaders/Slpk) works on a mapped view instead of
// stream reads: the OS page cache becomes the tile server, so "reading" a
// STORE'd zip entry is a pointer into the mapping (zero copies, no syscalls
// after open). Windows (Win32 CreateFileMappingW/MapViewOfFile) is the shipped
// path; a POSIX mmap variant is kept under #else so the pure-CPU parsing code
// can be exercised off-Windows (it changes nothing for the MSVC build).
//
// Thread safety: after a successful open() the mapping is immutable — any
// number of threads may read concurrently. open/close are not thread-safe.
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <string>

namespace Platform {

class FileMapping {
public:
    FileMapping() = default;
    ~FileMapping() { close(); }
    FileMapping(const FileMapping&) = delete;
    FileMapping& operator=(const FileMapping&) = delete;
    FileMapping(FileMapping&& other) noexcept { moveFrom(other); }
    FileMapping& operator=(FileMapping&& other) noexcept {
        if (this != &other) {
            close();
            moveFrom(other);
        }
        return *this;
    }

    // Maps the whole file read-only. Returns false (and stays closed) on any
    // failure — missing file, zero-length file, mapping failure. The path is
    // UTF-8 (converted to wide chars for Win32).
    bool open(const std::string& utf8Path);
    void close();

    bool valid() const { return data_ != nullptr; }
    const uint8_t* data() const { return data_; }
    size_t size() const { return size_; }

private:
    void moveFrom(FileMapping& other) noexcept;

    const uint8_t* data_ = nullptr;
    size_t size_ = 0;
#ifdef _WIN32
    void* fileHandle_ = nullptr;    // HANDLE (INVALID_HANDLE_VALUE sentinel kept as nullptr-when-closed)
    void* mappingHandle_ = nullptr; // HANDLE
#else
    int fd_ = -1;
#endif
};

} // namespace Platform
