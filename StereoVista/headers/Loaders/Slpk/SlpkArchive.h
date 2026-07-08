#pragma once

// ============================================================================
// i3s::SlpkArchive — memory-mapped reader for Esri Scene Layer Packages.
// ----------------------------------------------------------------------------
// An SLPK is a ZIP archive written STORE-only (no zip-level compression;
// ZIP64 for >4 GB), with the individual resources gzip-compressed *inside*
// (*.json.gz, *.bin.gz; jpg/png stored raw). That shape is what makes
// "instant open" possible:
//
//   * the file is memory-mapped (Platform::FileMapping) — a STORE'd entry is
//     a pointer span into the mapping, no read syscalls, no copies;
//   * v1.7+ packages end with an @specialIndexFileHASH128@ resource: a sorted
//     array of {md5(lowercased path), uint64 local-header offset} records.
//     When present (and locatable without walking the central directory), the
//     archive opens O(1) — EOCD + one speculative probe — regardless of entry
//     count. Otherwise the central directory is parsed once into a compact
//     name arena + the same sorted-md5 index (still fast: one linear walk of
//     mapped memory).
//
// read() is gzip-transparent: read("x.json") finds "x.json.gz" and inflates
// it (libdeflate). Rare DEFLATE-compressed zip entries are also handled.
//
// Thread safety: after open() the archive is immutable; read()/exists() may
// be called from any number of threads concurrently.
// ============================================================================

#include "Platform/FileMapping.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace i3s {

class SlpkArchive {
public:
    SlpkArchive() = default;

    // Maps the file and indexes it (hash-index fast path, else central
    // directory). Returns false with a reason in error() on failure.
    bool open(const std::string& utf8Path);

    bool valid() const { return mapping_.valid(); }
    const std::string& error() const { return error_; }
    const std::string& path() const { return path_; }

    // Total entries as declared by the end-of-central-directory record.
    uint64_t entryCount() const { return entryCount_; }
    // True when the O(1) @specialIndexFileHASH128@ path was used (no central
    // directory walk happened).
    bool usedHashIndex() const { return usedHashIndex_; }

    // Reads a resource by archive path ("nodepages/0.json", forward slashes,
    // case-insensitive via lowercase-normalization — SLPK paths are written
    // lowercase). Tries "<path>" then "<path>.gz"; a .gz entry is inflated
    // transparently. Returns false if the entry does not exist or inflation
    // fails. Thread-safe.
    bool read(const std::string& resourcePath, std::vector<uint8_t>& out) const;

    // Zero-copy access for STORE'd, non-gzip entries (textures in M1). Returns
    // nullptr when the entry is absent, gzipped, or DEFLATE'd (use read()).
    const uint8_t* rawSpan(const std::string& resourcePath, size_t& outSize) const;

    bool exists(const std::string& resourcePath) const;

private:
    struct Entry {
        uint64_t localHeaderOffset = 0;
        uint64_t compressedSize = 0;
        uint64_t uncompressedSize = 0;
        uint16_t method = 0;      // 0 = STORE, 8 = DEFLATE
        bool sizesValid = false;  // false => parse the local header for sizes
        bool isGzipName = false;  // entry name ends in ".gz"
    };

    // A sorted (memcmp order) md5 -> local-header-offset index; either read
    // straight out of the archive's @specialIndexFileHASH128@ resource or
    // built from the central directory.
    struct HashRecord {
        std::array<uint8_t, 16> md5;
        uint64_t localHeaderOffset;
    };

    bool parseEndOfCentralDirectory();
    bool tryAttachArchiveHashIndex();
    bool parseCentralDirectory();

    // Resolves a lookup key to an Entry. With the archive hash index the
    // entry data (sizes/method/name) is parsed from the local file header on
    // the spot; with a parsed central directory it comes from entries_.
    // Hash keys are md5 of the path AS STORED in the zip (case-exact);
    // findEntryAnyCase retries lowercased for casing mismatches.
    bool findEntry(const std::string& normalizedPath, Entry& out) const;
    bool findEntryAnyCase(const std::string& normalizedPath, Entry& out) const;
    bool entryFromLocalHeader(uint64_t localHeaderOffset, Entry& out) const;
    bool dataSpan(const Entry& entry, const uint8_t*& outData, size_t& outSize) const;

    static std::string normalizePath(const std::string& path);
    static std::string toLower(const std::string& path);
    static std::array<uint8_t, 16> md5Of(const std::string& normalizedPath);

    Platform::FileMapping mapping_;
    std::string path_;
    std::string error_;

    uint64_t entryCount_ = 0;
    uint64_t centralDirOffset_ = 0;
    uint64_t centralDirSize_ = 0;

    // Sorted md5 index (always populated after a successful open).
    std::vector<HashRecord> hashIndex_;
    bool usedHashIndex_ = false;

    // Central-directory data; only populated when the hash-index fast path
    // was not available. Entry names live in one arena (no per-entry string).
    std::vector<Entry> entries_;                 // parallel to hashIndex_ payload
    std::vector<uint32_t> entryIndexByHashPos_;  // hashIndex_[i] -> entries_ index
};

} // namespace i3s
