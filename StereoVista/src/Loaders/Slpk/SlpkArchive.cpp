#include "Loaders/Slpk/SlpkArchive.h"

#include <libdeflate/libdeflate.h>

// md5-c is plain C without an extern "C" guard of its own (md5.c is compiled
// as C by the vcxproj, so the declarations must not be C++-mangled here).
extern "C" {
#include <md5/md5.h>
}

#include <algorithm>
#include <cstring>

namespace i3s {

namespace {

// ---- little-endian field readers (the mapping is raw ZIP bytes) ------------

uint16_t readU16(const uint8_t* p) {
    uint16_t v;
    std::memcpy(&v, p, sizeof(v));
    return v; // x64 target is little-endian; ZIP fields are little-endian
}

uint32_t readU32(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, sizeof(v));
    return v;
}

uint64_t readU64(const uint8_t* p) {
    uint64_t v;
    std::memcpy(&v, p, sizeof(v));
    return v;
}

// ---- ZIP structure constants ------------------------------------------------

constexpr uint32_t kEocdSignature = 0x06054b50;       // end of central directory
constexpr uint32_t kEocd64LocatorSignature = 0x07064b50;
constexpr uint32_t kEocd64Signature = 0x06064b50;
constexpr uint32_t kCentralDirSignature = 0x02014b50;
constexpr uint32_t kLocalHeaderSignature = 0x04034b50;

constexpr size_t kEocdSize = 22;
constexpr size_t kEocd64LocatorSize = 20;
constexpr size_t kLocalHeaderSize = 30;
constexpr size_t kCentralDirEntrySize = 46;

constexpr uint16_t kMethodStore = 0;
constexpr uint16_t kMethodDeflate = 8;
constexpr uint16_t kFlagDataDescriptor = 0x0008; // sizes live after the data

// The v1.7+ O(1) lookup resource: sorted {md5(lowercase path), u64 offset}
// records, written as the LAST entry of the archive (data ends where the
// central directory starts) — that placement is what lets us find it without
// walking the central directory.
constexpr char kHashIndexName[] = "@specialIndexFileHASH128@";
constexpr size_t kHashIndexNameLen = sizeof(kHashIndexName) - 1; // 25
constexpr size_t kHashRecordSize = 24;

bool endsWithGz(const uint8_t* name, size_t nameLen) {
    return nameLen >= 3 && name[nameLen - 3] == '.' &&
           (name[nameLen - 2] == 'g' || name[nameLen - 2] == 'G') &&
           (name[nameLen - 1] == 'z' || name[nameLen - 1] == 'Z');
}

} // namespace

// ---- path normalization + hashing -------------------------------------------

std::string SlpkArchive::normalizePath(const std::string& path) {
    // Slash-normalize and strip leading "./" — but PRESERVE case: the archive
    // hash index stores md5 of the path exactly as written into the zip
    // (verified against ArcGIS-cooked packages: "3dSceneLayer.json.gz" is
    // hashed mixed-case). Case-insensitive fallbacks happen in findEntry.
    std::string out;
    out.reserve(path.size());
    size_t start = 0;
    if (path.size() >= 2 && path[0] == '.' && (path[1] == '/' || path[1] == '\\'))
        start = 2;
    while (start < path.size() && (path[start] == '/' || path[start] == '\\'))
        ++start;
    for (size_t i = start; i < path.size(); ++i) {
        char c = path[i];
        if (c == '\\')
            c = '/';
        out.push_back(c);
    }
    return out;
}

std::string SlpkArchive::toLower(const std::string& path) {
    std::string out = path;
    for (char& c : out)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    return out;
}

std::array<uint8_t, 16> SlpkArchive::md5Of(const std::string& normalizedPath) {
    // md5-c's update signature takes a non-const buffer pointer; it never
    // writes through it, so the const_cast is safe.
    MD5Context ctx;
    md5Init(&ctx);
    md5Update(&ctx,
              const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(normalizedPath.data())),
              normalizedPath.size());
    md5Finalize(&ctx);
    std::array<uint8_t, 16> digest;
    std::memcpy(digest.data(), ctx.digest, 16);
    return digest;
}

// ---- open: EOCD -> (hash index | central directory) --------------------------

bool SlpkArchive::open(const std::string& utf8Path) {
    hashIndex_.clear();
    entries_.clear();
    entryIndexByHashPos_.clear();
    usedHashIndex_ = false;
    error_.clear();
    path_ = utf8Path;

    if (!mapping_.open(utf8Path)) {
        error_ = "cannot open / map file";
        return false;
    }
    if (mapping_.size() < kEocdSize) {
        error_ = "file too small to be a zip archive";
        mapping_.close();
        return false;
    }

    if (!parseEndOfCentralDirectory()) {
        mapping_.close();
        return false;
    }

    if (!tryAttachArchiveHashIndex()) {
        if (!parseCentralDirectory()) {
            mapping_.close();
            return false;
        }
    }
    return true;
}

bool SlpkArchive::parseEndOfCentralDirectory() {
    const uint8_t* base = mapping_.data();
    const size_t size = mapping_.size();

    // Scan backwards for the EOCD signature; prefer a candidate whose comment
    // length exactly reaches the file end (defends against the signature bytes
    // appearing inside a trailing comment).
    const size_t scanLimit = kEocdSize + 65535;
    const size_t lowest = size > scanLimit ? size - scanLimit : 0;
    size_t eocdPos = SIZE_MAX;
    size_t firstCandidate = SIZE_MAX;
    for (size_t pos = size - kEocdSize + 1; pos-- > lowest;) {
        if (readU32(base + pos) != kEocdSignature)
            continue;
        if (firstCandidate == SIZE_MAX)
            firstCandidate = pos;
        const uint16_t commentLen = readU16(base + pos + 20);
        if (pos + kEocdSize + commentLen == size) {
            eocdPos = pos;
            break;
        }
    }
    if (eocdPos == SIZE_MAX)
        eocdPos = firstCandidate;
    if (eocdPos == SIZE_MAX) {
        error_ = "no end-of-central-directory record (not a zip archive)";
        return false;
    }

    entryCount_ = readU16(base + eocdPos + 10);
    centralDirSize_ = readU32(base + eocdPos + 12);
    centralDirOffset_ = readU32(base + eocdPos + 16);

    // ZIP64: a locator directly precedes the EOCD when the archive needs
    // 64-bit fields (SLPKs beyond 4 GB always do).
    if (eocdPos >= kEocd64LocatorSize &&
        readU32(base + eocdPos - kEocd64LocatorSize) == kEocd64LocatorSignature) {
        const uint64_t eocd64Off = readU64(base + eocdPos - kEocd64LocatorSize + 8);
        if (eocd64Off + 56 <= size && readU32(base + eocd64Off) == kEocd64Signature) {
            entryCount_ = readU64(base + eocd64Off + 32);
            centralDirSize_ = readU64(base + eocd64Off + 40);
            centralDirOffset_ = readU64(base + eocd64Off + 48);
        }
    }

    // Subtraction form: the sum can wrap uint64 on a corrupt/hostile file and
    // sail past a naive `offset + size > fileSize` check.
    if (centralDirOffset_ > size || centralDirSize_ > size - centralDirOffset_) {
        error_ = "central directory extends past end of file (corrupt archive)";
        return false;
    }
    return true;
}

bool SlpkArchive::tryAttachArchiveHashIndex() {
    // Speculative O(1) locate (mirrors Esri's own reader): the hash-index
    // resource is written as the archive's last entry, so its data ends where
    // the central directory begins. With N = entryCount-1 records of 24 bytes,
    // its local header sits at
    //     centralDirOffset - N*24 - (30 + 25 + extraFieldLen)
    // for an unknown small extra field length — probe a modest range and
    // verify signature + name. Any mismatch => central-directory fallback.
    const uint8_t* base = mapping_.data();
    if (entryCount_ < 2)
        return false;
    const uint64_t recordCount = entryCount_ - 1;
    // A forged entry count could overflow the record math or demand a
    // multi-GB index allocation; the whole index must fit inside the file.
    if (recordCount > mapping_.size() / kHashRecordSize)
        return false;
    const uint64_t dataSize = recordCount * kHashRecordSize;
    const uint64_t headerAndName = kLocalHeaderSize + kHashIndexNameLen;
    if (dataSize + headerAndName > centralDirOffset_)
        return false;

    uint64_t headerPos = UINT64_MAX;
    uint64_t extraLenFound = 0;
    const uint64_t baseGuess = centralDirOffset_ - dataSize - headerAndName;
    const uint64_t maxExtra = std::min<uint64_t>(baseGuess, 256);
    for (uint64_t extra = 0; extra <= maxExtra; ++extra) {
        const uint64_t pos = baseGuess - extra;
        if (readU32(base + pos) != kLocalHeaderSignature)
            continue;
        if (readU16(base + pos + 26) != kHashIndexNameLen)
            continue;
        if (readU16(base + pos + 28) != extra)
            continue;
        if (std::memcmp(base + pos + kLocalHeaderSize, kHashIndexName,
                        kHashIndexNameLen) != 0)
            continue;
        headerPos = pos;
        extraLenFound = extra;
        break;
    }
    if (headerPos == UINT64_MAX)
        return false;

    // The index itself must be STORE'd with in-header sizes.
    const uint16_t flags = readU16(base + headerPos + 6);
    const uint16_t method = readU16(base + headerPos + 8);
    const uint32_t compSize32 = readU32(base + headerPos + 18);
    if (method != kMethodStore || (flags & kFlagDataDescriptor) != 0)
        return false;
    if (compSize32 != dataSize && compSize32 != 0xFFFFFFFFu)
        return false;

    const uint8_t* records = base + headerPos + headerAndName + extraLenFound;
    hashIndex_.resize(static_cast<size_t>(recordCount));
    for (size_t i = 0; i < recordCount; ++i) {
        std::memcpy(hashIndex_[i].md5.data(), records + i * kHashRecordSize, 16);
        hashIndex_[i].localHeaderOffset = readU64(records + i * kHashRecordSize + 16);
    }
    // Esri writes the records sorted; verify (memcmp order) so the binary
    // search below is safe, and sort if a writer didn't.
    const bool sorted = std::is_sorted(
        hashIndex_.begin(), hashIndex_.end(), [](const HashRecord& a, const HashRecord& b) {
            return std::memcmp(a.md5.data(), b.md5.data(), 16) < 0;
        });
    if (!sorted) {
        std::sort(hashIndex_.begin(), hashIndex_.end(),
                  [](const HashRecord& a, const HashRecord& b) {
                      return std::memcmp(a.md5.data(), b.md5.data(), 16) < 0;
                  });
    }

    // Sanity-check the fast path end-to-end on the one resource every SLPK
    // has: the scene-layer document. If its local header doesn't parse with
    // trustworthy sizes, distrust the whole index and use the central
    // directory instead (open-time decision keeps read() immutable).
    Entry probe;
    usedHashIndex_ = true;
    const bool ok = findEntryAnyCase("3dSceneLayer.json.gz", probe) ||
                    findEntryAnyCase("3dSceneLayer.json", probe);
    if (!ok) {
        usedHashIndex_ = false;
        hashIndex_.clear();
        return false;
    }
    return true;
}

bool SlpkArchive::parseCentralDirectory() {
    const uint8_t* base = mapping_.data();
    const size_t size = mapping_.size();

    entries_.clear();
    hashIndex_.clear();
    entryIndexByHashPos_.clear();
    entries_.reserve(static_cast<size_t>(entryCount_));
    hashIndex_.reserve(static_cast<size_t>(entryCount_));

    uint64_t pos = centralDirOffset_;
    const uint64_t end = centralDirOffset_ + centralDirSize_;
    std::string normalized;
    while (pos + kCentralDirEntrySize <= end) {
        if (readU32(base + pos) != kCentralDirSignature)
            break;
        const uint16_t method = readU16(base + pos + 10);
        uint64_t compSize = readU32(base + pos + 20);
        uint64_t uncompSize = readU32(base + pos + 24);
        const uint16_t nameLen = readU16(base + pos + 28);
        const uint16_t extraLen = readU16(base + pos + 30);
        const uint16_t commentLen = readU16(base + pos + 32);
        uint64_t localOff = readU32(base + pos + 42);

        const uint64_t recordEnd =
            pos + kCentralDirEntrySize + nameLen + extraLen + commentLen;
        if (recordEnd > end || recordEnd > size)
            break;

        // ZIP64 extended-information extra field (id 0x0001): 64-bit values
        // appear, in order, only for the 32-bit fields that saturated.
        if (compSize == 0xFFFFFFFFu || uncompSize == 0xFFFFFFFFu ||
            localOff == 0xFFFFFFFFu) {
            uint64_t extraPos = pos + kCentralDirEntrySize + nameLen;
            const uint64_t extraEnd = extraPos + extraLen;
            while (extraPos + 4 <= extraEnd) {
                const uint16_t fieldId = readU16(base + extraPos);
                const uint16_t fieldSize = readU16(base + extraPos + 2);
                if (fieldId == 0x0001) {
                    uint64_t f = extraPos + 4;
                    if (uncompSize == 0xFFFFFFFFu && f + 8 <= extraEnd) {
                        uncompSize = readU64(base + f);
                        f += 8;
                    }
                    if (compSize == 0xFFFFFFFFu && f + 8 <= extraEnd) {
                        compSize = readU64(base + f);
                        f += 8;
                    }
                    if (localOff == 0xFFFFFFFFu && f + 8 <= extraEnd) {
                        localOff = readU64(base + f);
                        f += 8;
                    }
                    break;
                }
                extraPos += 4 + fieldSize;
            }
        }

        const uint8_t* name = base + pos + kCentralDirEntrySize;

        // Skip the hash-index resource itself; it is not addressable content.
        const bool isHashIndexEntry =
            nameLen == kHashIndexNameLen &&
            std::memcmp(name, kHashIndexName, kHashIndexNameLen) == 0;
        if (!isHashIndexEntry) {
            Entry entry;
            entry.localHeaderOffset = localOff;
            entry.compressedSize = compSize;
            entry.uncompressedSize = uncompSize;
            entry.method = method;
            entry.sizesValid = true;
            entry.isGzipName = endsWithGz(name, nameLen);

            normalized.assign(reinterpret_cast<const char*>(name), nameLen);
            normalized = normalizePath(normalized);

            HashRecord rec;
            rec.md5 = md5Of(normalized);
            rec.localHeaderOffset = localOff;
            hashIndex_.push_back(rec);
            entryIndexByHashPos_.push_back(static_cast<uint32_t>(entries_.size()));
            entries_.push_back(entry);
        }

        pos = recordEnd;
    }

    if (entries_.empty()) {
        error_ = "central directory yielded no entries (corrupt archive)";
        return false;
    }

    // Sort md5 index and the parallel entry-index array together.
    std::vector<uint32_t> order(hashIndex_.size());
    for (uint32_t i = 0; i < order.size(); ++i)
        order[i] = i;
    std::sort(order.begin(), order.end(), [this](uint32_t a, uint32_t b) {
        return std::memcmp(hashIndex_[a].md5.data(), hashIndex_[b].md5.data(), 16) < 0;
    });
    std::vector<HashRecord> sortedHash(hashIndex_.size());
    std::vector<uint32_t> sortedIdx(hashIndex_.size());
    for (size_t i = 0; i < order.size(); ++i) {
        sortedHash[i] = hashIndex_[order[i]];
        sortedIdx[i] = entryIndexByHashPos_[order[i]];
    }
    hashIndex_ = std::move(sortedHash);
    entryIndexByHashPos_ = std::move(sortedIdx);
    return true;
}

// ---- lookup ------------------------------------------------------------------

bool SlpkArchive::entryFromLocalHeader(uint64_t localHeaderOffset, Entry& out) const {
    const uint8_t* base = mapping_.data();
    const size_t size = mapping_.size();
    // Subtraction form: the offset comes from the archive's own hash index
    // and may be hostile; the addition can wrap.
    if (localHeaderOffset > size || size - localHeaderOffset < kLocalHeaderSize)
        return false;
    const uint8_t* h = base + localHeaderOffset;
    if (readU32(h) != kLocalHeaderSignature)
        return false;

    const uint16_t flags = readU16(h + 6);
    const uint16_t method = readU16(h + 8);
    uint64_t compSize = readU32(h + 18);
    uint64_t uncompSize = readU32(h + 22);
    const uint16_t nameLen = readU16(h + 26);
    const uint16_t extraLen = readU16(h + 28);
    if (localHeaderOffset + kLocalHeaderSize + nameLen + extraLen > size)
        return false;

    // ZIP64 sizes in the local extra field.
    if (compSize == 0xFFFFFFFFu || uncompSize == 0xFFFFFFFFu) {
        uint64_t extraPos = localHeaderOffset + kLocalHeaderSize + nameLen;
        const uint64_t extraEnd = extraPos + extraLen;
        while (extraPos + 4 <= extraEnd) {
            const uint16_t fieldId = readU16(base + extraPos);
            const uint16_t fieldSize = readU16(base + extraPos + 2);
            if (fieldId == 0x0001) {
                uint64_t f = extraPos + 4;
                if (uncompSize == 0xFFFFFFFFu && f + 8 <= extraEnd) {
                    uncompSize = readU64(base + f);
                    f += 8;
                }
                if (compSize == 0xFFFFFFFFu && f + 8 <= extraEnd) {
                    compSize = readU64(base + f);
                    f += 8;
                }
                break;
            }
            extraPos += 4 + fieldSize;
        }
    }

    out.localHeaderOffset = localHeaderOffset;
    out.compressedSize = compSize;
    out.uncompressedSize = uncompSize;
    out.method = method;
    // A data-descriptor entry keeps its sizes AFTER the payload; SLPK writers
    // don't produce those, so treat them as unreadable rather than guessing.
    out.sizesValid = (flags & kFlagDataDescriptor) == 0;
    out.isGzipName = endsWithGz(h + kLocalHeaderSize, nameLen);
    return out.sizesValid;
}

bool SlpkArchive::findEntryAnyCase(const std::string& normalizedPath,
                                   Entry& out) const {
    if (findEntry(normalizedPath, out))
        return true;
    // Hash keys are case-exact (paths hashed as stored); tolerate callers /
    // packages that disagree on casing via a lowercase retry.
    const std::string lower = toLower(normalizedPath);
    return lower != normalizedPath && findEntry(lower, out);
}

bool SlpkArchive::findEntry(const std::string& normalizedPath, Entry& out) const {
    if (hashIndex_.empty())
        return false;
    const std::array<uint8_t, 16> digest = md5Of(normalizedPath);
    const auto it = std::lower_bound(
        hashIndex_.begin(), hashIndex_.end(), digest,
        [](const HashRecord& rec, const std::array<uint8_t, 16>& key) {
            return std::memcmp(rec.md5.data(), key.data(), 16) < 0;
        });
    if (it == hashIndex_.end() || std::memcmp(it->md5.data(), digest.data(), 16) != 0)
        return false;

    if (!usedHashIndex_) {
        // Central-directory path: full entry data is already parsed.
        const size_t hashPos = static_cast<size_t>(it - hashIndex_.begin());
        out = entries_[entryIndexByHashPos_[hashPos]];
        return true;
    }
    return entryFromLocalHeader(it->localHeaderOffset, out);
}

bool SlpkArchive::dataSpan(const Entry& entry, const uint8_t*& outData,
                           size_t& outSize) const {
    const uint8_t* base = mapping_.data();
    const size_t size = mapping_.size();
    if (entry.localHeaderOffset > size ||
        size - entry.localHeaderOffset < kLocalHeaderSize)
        return false;
    const uint8_t* h = base + entry.localHeaderOffset;
    if (readU32(h) != kLocalHeaderSignature)
        return false;
    const uint16_t nameLen = readU16(h + 26);
    const uint16_t extraLen = readU16(h + 28);
    const uint64_t dataStart =
        entry.localHeaderOffset + kLocalHeaderSize + nameLen + extraLen;
    if (dataStart > size || entry.compressedSize > size - dataStart)
        return false;
    outData = base + dataStart;
    outSize = static_cast<size_t>(entry.compressedSize);
    return true;
}

bool SlpkArchive::exists(const std::string& resourcePath) const {
    Entry entry;
    const std::string normalized = normalizePath(resourcePath);
    return findEntryAnyCase(normalized, entry) ||
           findEntryAnyCase(normalized + ".gz", entry);
}

bool SlpkArchive::read(const std::string& resourcePath, std::vector<uint8_t>& out) const {
    out.clear();
    if (!valid())
        return false;

    const std::string normalized = normalizePath(resourcePath);
    Entry entry;
    if (!findEntryAnyCase(normalized, entry) &&
        !findEntryAnyCase(normalized + ".gz", entry))
        return false;
    if (!entry.sizesValid)
        return false;

    const uint8_t* data = nullptr;
    size_t dataSize = 0;
    if (!dataSpan(entry, data, dataSize))
        return false;

    // Rare non-STORE zip entry: inflate the raw deflate stream first.
    std::vector<uint8_t> inflated;
    if (entry.method == kMethodDeflate) {
        if (entry.uncompressedSize == 0 || entry.uncompressedSize > (1ull << 32))
            return false;
        inflated.resize(static_cast<size_t>(entry.uncompressedSize));
        libdeflate_decompressor* dec = libdeflate_alloc_decompressor();
        if (dec == nullptr)
            return false;
        size_t actual = 0;
        const libdeflate_result res = libdeflate_deflate_decompress(
            dec, data, dataSize, inflated.data(), inflated.size(), &actual);
        libdeflate_free_decompressor(dec);
        if (res != LIBDEFLATE_SUCCESS)
            return false;
        inflated.resize(actual);
        data = inflated.data();
        dataSize = inflated.size();
    } else if (entry.method != kMethodStore) {
        return false; // unsupported compression method
    }

    if (!entry.isGzipName) {
        out.assign(data, data + dataSize);
        return true;
    }

    // Gzip-wrapped resource. The gzip trailer's ISIZE (uncompressed size mod
    // 2^32) sizes the output buffer; grow-and-retry covers the pathological
    // >4 GB single-resource case.
    if (dataSize < 18) // minimal gzip stream
        return false;
    uint64_t expected = readU32(data + dataSize - 4);
    if (expected == 0)
        expected = 4096;

    libdeflate_decompressor* dec = libdeflate_alloc_decompressor();
    if (dec == nullptr)
        return false;
    bool ok = false;
    for (int attempt = 0; attempt < 4; ++attempt) {
        out.resize(static_cast<size_t>(expected));
        size_t actual = 0;
        const libdeflate_result res = libdeflate_gzip_decompress(
            dec, data, dataSize, out.data(), out.size(), &actual);
        if (res == LIBDEFLATE_SUCCESS) {
            out.resize(actual);
            ok = true;
            break;
        }
        if (res != LIBDEFLATE_INSUFFICIENT_SPACE)
            break;
        expected *= 4;
    }
    libdeflate_free_decompressor(dec);
    if (!ok)
        out.clear();
    return ok;
}

const uint8_t* SlpkArchive::rawSpan(const std::string& resourcePath,
                                    size_t& outSize) const {
    outSize = 0;
    if (!valid())
        return nullptr;
    Entry entry;
    if (!findEntryAnyCase(normalizePath(resourcePath), entry))
        return nullptr;
    if (!entry.sizesValid || entry.method != kMethodStore || entry.isGzipName)
        return nullptr;
    const uint8_t* data = nullptr;
    size_t dataSize = 0;
    if (!dataSpan(entry, data, dataSize))
        return nullptr;
    outSize = dataSize;
    return data;
}

} // namespace i3s
