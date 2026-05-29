// point_cloud_loader.cpp
#include "Loaders/PointCloudLoader.h"
#include "Engine/OctreePointCloudManager.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <thread>
#include <mutex>
#include <atomic>
#include <glad/glad.h>
#include <filesystem>
#include <future>
#include <map>
#include <omp.h>
#include <glm/gtx/norm.hpp>
#include <utility>

#include <random>
#include <execution>
#include <algorithm>

#include <Utils/octree.h>

// HDF5 includes
#include <hdf5/H5Cpp.h>
#include <chrono>
#include <ctime>
using namespace H5;

// LASzip – handles both LAS and LAZ transparently
#ifndef LASZIP_DYN_LINK
#  define LASZIP_DYN_LINK   // required to get __declspec(dllimport) on Windows
#endif
#include <laszip/laszip_api.h>


namespace Engine {

    // =========================================================================
    // Streaming compute SSBO infrastructure
    // =========================================================================
    // These helpers let every loader stream points directly to GPU SSBOs one
    // batch (kComputeBatchSize = 10240 points) at a time.
    // Peak CPU RAM = one batch (~280 KB) regardless of point cloud size.
    //
    // Coordinate quantisation matches the Schütz compute rasterizer exactly:
    //   normalised = (pos - batchMin) / batchSize   ∈ [0,1]
    //   bits30     = uint32( normalised * 2^30 )
    //   ssXyz_4b   packs bits30[29:20]  (coarsest; always read by shader)
    //   ssXyz_8b   packs bits30[19:10]
    //   ssXyz_12b  packs bits30[ 9: 0]  (finest; level-0 only)
    // =========================================================================

    static void deleteComputeSSBOs(PointCloud& pc) {
        auto del = [](GLuint& id) { if (id) { glDeleteBuffers(1, &id); id = 0; } };
        del(pc.computeBatchSSBO);
        del(pc.computeXyz12bSSBO);
        del(pc.computeXyz8bSSBO);
        del(pc.computeXyz4bSSBO);
        del(pc.computeRGBASSBO);
    }

    // Pre-allocate all five SSBOs for totalPoints without uploading data.
    // GL_DYNAMIC_DRAW because we fill them incrementally with glBufferSubData.
    static void allocateComputeSSBOs(PointCloud& pc, size_t totalPoints) {
        deleteComputeSSBOs(pc);
        if (pc.vbo) { glDeleteBuffers(1, &pc.vbo); pc.vbo = 0; }
        if (pc.vao) { glDeleteVertexArrays(1, &pc.vao); pc.vao = 0; }
        if (totalPoints == 0) return;

        const size_t numBatches =
            (totalPoints + PointCloud::kComputeBatchSize - 1) / PointCloud::kComputeBatchSize;

        auto alloc = [](GLuint& id, GLsizeiptr bytes) {
            glGenBuffers(1, &id);
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, id);
            glBufferData(GL_SHADER_STORAGE_BUFFER, bytes, nullptr, GL_DYNAMIC_DRAW);
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
        };

        alloc(pc.computeBatchSSBO,  static_cast<GLsizeiptr>(numBatches  * sizeof(ComputeBatch)));
        alloc(pc.computeXyz4bSSBO,  static_cast<GLsizeiptr>(totalPoints * sizeof(uint32_t)));
        alloc(pc.computeXyz8bSSBO,  static_cast<GLsizeiptr>(totalPoints * sizeof(uint32_t)));
        alloc(pc.computeXyz12bSSBO, static_cast<GLsizeiptr>(totalPoints * sizeof(uint32_t)));
        alloc(pc.computeRGBASSBO,   static_cast<GLsizeiptr>(totalPoints * sizeof(uint32_t)));
        pc.computePointsPerThread = (PointCloud::kComputeBatchSize + 127) / 128;
    }

    // Trim over-allocated SSBOs to actualPoints using GPU-side copy.
    static void trimComputeSSBOs(PointCloud& pc, size_t actualPoints) {
        if (actualPoints == 0) { deleteComputeSSBOs(pc); return; }

        const size_t actualBatches =
            (actualPoints + PointCloud::kComputeBatchSize - 1) / PointCloud::kComputeBatchSize;

        auto trimBuffer = [](GLuint& id, GLsizeiptr newBytes) {
            GLuint newId = 0;
            glGenBuffers(1, &newId);
            glBindBuffer(GL_COPY_WRITE_BUFFER, newId);
            glBufferData(GL_COPY_WRITE_BUFFER, newBytes, nullptr, GL_STATIC_DRAW);
            glBindBuffer(GL_COPY_READ_BUFFER, id);
            glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, 0, 0, newBytes);
            glDeleteBuffers(1, &id);
            id = newId;
        };

        trimBuffer(pc.computeBatchSSBO,  static_cast<GLsizeiptr>(actualBatches * sizeof(ComputeBatch)));
        trimBuffer(pc.computeXyz4bSSBO,  static_cast<GLsizeiptr>(actualPoints  * sizeof(uint32_t)));
        trimBuffer(pc.computeXyz8bSSBO,  static_cast<GLsizeiptr>(actualPoints  * sizeof(uint32_t)));
        trimBuffer(pc.computeXyz12bSSBO, static_cast<GLsizeiptr>(actualPoints  * sizeof(uint32_t)));
        trimBuffer(pc.computeRGBASSBO,   static_cast<GLsizeiptr>(actualPoints  * sizeof(uint32_t)));
        glBindBuffer(GL_COPY_READ_BUFFER, 0);
        glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
    }

    // Upload one batch of count points to the pre-allocated SSBOs.
    //   batchIndex – 0-based batch index
    //   firstPoint – absolute point offset of pts[0] within the full cloud
    static void uploadComputeBatch(PointCloud& pc,
                                   const PointCloudPoint* pts,
                                   int count, int batchIndex, int firstPoint)
    {
        if (count <= 0) return;

        constexpr int      STEPS_30BIT = 1073741824; // 2^30 (exactly representable)
        constexpr uint32_t MASK_10BIT  = 1023u;

        glm::vec3 bMin = pts[0].position, bMax = pts[0].position;
        for (int i = 1; i < count; i++) {
            bMin = glm::min(bMin, pts[i].position);
            bMax = glm::max(bMax, pts[i].position);
        }
        glm::vec3 sz = bMax - bMin;
        if (sz.x < 1e-6f) sz.x = 1e-6f;
        if (sz.y < 1e-6f) sz.y = 1e-6f;
        if (sz.z < 1e-6f) sz.z = 1e-6f;

        const ComputeBatch batchDesc = {
            bMin.x, bMin.y, bMin.z, bMax.x, bMax.y, bMax.z, count, firstPoint
        };

        std::vector<uint32_t> xyz4b(count), xyz8b(count), xyz12b(count), rgba(count);

        for (int i = 0; i < count; i++) {
            const glm::vec3& p = pts[i].position;
            auto q = [&](float v, float lo, float s) -> uint32_t {
                float n = glm::clamp((v - lo) / s, 0.0f, 1.0f);
                return std::min(static_cast<uint32_t>(n * static_cast<float>(STEPS_30BIT)),
                                static_cast<uint32_t>(STEPS_30BIT - 1));
            };
            const uint32_t Xb = q(p.x, bMin.x, sz.x);
            const uint32_t Yb = q(p.y, bMin.y, sz.y);
            const uint32_t Zb = q(p.z, bMin.z, sz.z);
            xyz4b [i] = ((Xb>>20)&MASK_10BIT) | (((Yb>>20)&MASK_10BIT)<<10) | (((Zb>>20)&MASK_10BIT)<<20);
            xyz8b [i] = ((Xb>>10)&MASK_10BIT) | (((Yb>>10)&MASK_10BIT)<<10) | (((Zb>>10)&MASK_10BIT)<<20);
            xyz12b[i] = (Xb&MASK_10BIT) | ((Yb&MASK_10BIT)<<10) | ((Zb&MASK_10BIT)<<20);
            const auto& c = pts[i].color;
            const uint32_t r8 = static_cast<uint32_t>(glm::clamp(c.r,0.f,1.f)*255.f+0.5f);
            const uint32_t g8 = static_cast<uint32_t>(glm::clamp(c.g,0.f,1.f)*255.f+0.5f);
            const uint32_t b8 = static_cast<uint32_t>(glm::clamp(c.b,0.f,1.f)*255.f+0.5f);
            rgba[i] = (0xFFu<<24)|(b8<<16)|(g8<<8)|r8;
        }

        glBindBuffer(GL_SHADER_STORAGE_BUFFER, pc.computeBatchSSBO);
        glBufferSubData(GL_SHADER_STORAGE_BUFFER,
                        static_cast<GLintptr>(batchIndex)*sizeof(ComputeBatch),
                        sizeof(ComputeBatch), &batchDesc);

        const GLintptr   off = static_cast<GLintptr>(firstPoint)*sizeof(uint32_t);
        const GLsizeiptr byt = static_cast<GLsizeiptr>(count)*sizeof(uint32_t);

        glBindBuffer(GL_SHADER_STORAGE_BUFFER, pc.computeXyz4bSSBO);
        glBufferSubData(GL_SHADER_STORAGE_BUFFER, off, byt, xyz4b.data());
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, pc.computeXyz8bSSBO);
        glBufferSubData(GL_SHADER_STORAGE_BUFFER, off, byt, xyz8b.data());
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, pc.computeXyz12bSSBO);
        glBufferSubData(GL_SHADER_STORAGE_BUFFER, off, byt, xyz12b.data());
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, pc.computeRGBASSBO);
        glBufferSubData(GL_SHADER_STORAGE_BUFFER, off, byt, rgba.data());
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    }

    // Fast single-pass scan: count data lines in a text point cloud file.
    // A "data line" starts with a digit, '-', or '+'.
    static size_t countTextLines(const std::string& filePath) {
        std::ifstream f(filePath, std::ios::binary);
        if (!f) return 0;
        constexpr size_t BUF = 32 * 1024 * 1024;
        std::vector<char> buf(BUF);
        size_t count = 0;
        bool startOfLine = true, lineIsData = false;
        while (f.read(buf.data(), static_cast<std::streamsize>(BUF)) || f.gcount() > 0) {
            const std::streamsize n = f.gcount();
            for (std::streamsize i = 0; i < n; i++) {
                const char c = buf[i];
                if (c == '\n') {
                    if (lineIsData) count++;
                    startOfLine = true; lineIsData = false;
                } else if (c == '\r') {
                    // ignore CR in CRLF
                } else if (startOfLine) {
                    startOfLine = false;
                    if ((c>='0'&&c<='9')||c=='-'||c=='+') lineIsData = true;
                }
            }
        }
        if (lineIsData) count++; // file without trailing newline
        return count;
    }

    // =========================================================================
    // End of streaming infrastructure
    // =========================================================================

    PointCloud PointCloudLoader::loadPointCloudFile(const std::string& filePath, size_t downsampleFactor) {
        std::cout << "[DEBUG] PointCloudLoader::loadPointCloudFile() called with file: " << filePath << std::endl;
        std::cout << "[DEBUG] Downsample factor: " << downsampleFactor << std::endl;
        
        std::filesystem::path file_path(filePath);
        std::string extension = file_path.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
        
        std::cout << "[DEBUG] File extension detected: " << extension << std::endl;

        // Check file extension and delegate to appropriate loader
        if (extension == ".h5" || extension == ".hdf5" || extension == ".f5") {
            std::cout << "[DEBUG] Loading as HDF5 file" << std::endl;
            return loadFromHDF5(filePath, downsampleFactor);
        }
        else if (extension == ".pcb") {
            std::cout << "[DEBUG] Loading as binary file" << std::endl;
            return loadFromBinary(filePath);
        }
        else if (extension == ".las" || extension == ".laz") {
            std::cout << "[DEBUG] Loading as LAS/LAZ file" << std::endl;
            return loadFromLAS(filePath, downsampleFactor);
        }

        // ── Default handling: XYZ, PLY, TXT, and other text formats ─────────
        // Streaming load: points are quantised and uploaded to GPU one batch
        // (kComputeBatchSize = 10240 points) at a time.  Peak CPU RAM is a
        // single batch (~280 KB) regardless of file size.
        PointCloud pointCloud;
        pointCloud.name = "PointCloud_" + std::filesystem::path(filePath).filename().string();
        pointCloud.position = glm::vec3(0.0f);
        pointCloud.rotation = glm::vec3(0.0f);
        pointCloud.scale    = glm::vec3(1.0f);

        // ── Pass 1: count data lines to pre-allocate SSBOs exactly ───────────
        std::cout << "[TextPC] Counting lines in: " << filePath << " ..." << std::endl;
        const size_t estimatedPoints = countTextLines(filePath);
        // Apply downsampling factor to the estimate
        const size_t allocPoints = (downsampleFactor > 1)
                                 ? (estimatedPoints / downsampleFactor + 1)
                                 : estimatedPoints;
        std::cout << "[TextPC] Estimated " << estimatedPoints
                  << " data lines (allocating for "
                  << allocPoints << " after downsample x" << downsampleFactor << ")\n";

        if (allocPoints == 0) {
            std::cerr << "[TextPC] No data lines found in " << filePath << "\n";
            return std::move(pointCloud);
        }

        allocateComputeSSBOs(pointCloud, allocPoints);

        // ── Pass 2: stream-parse and upload ─────────────────────────────────
        std::ifstream file(filePath, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "[TextPC] Failed to open: " << filePath << "\n";
            deleteComputeSSBOs(pointCloud);
            return std::move(pointCloud);
        }

        // Large IO buffer – reduces syscall overhead on big files
        constexpr size_t IO_BUF = 64 * 1024 * 1024; // 64 MB
        std::vector<char> ioBuf(IO_BUF);
        file.rdbuf()->pubsetbuf(ioBuf.data(), static_cast<std::streamsize>(IO_BUF));

        // Batch accumulator – only one batch worth of points live in RAM
        std::vector<PointCloudPoint> batchBuf;
        batchBuf.reserve(PointCloud::kComputeBatchSize);

        int    batchIndex  = 0;
        size_t totalPoints = 0;
        size_t lineCounter = 0;   // global line counter for downsampling

        // Global bounds tracking (updated per-point, negligible cost)
        glm::vec3 gMin( FLT_MAX), gMax(-FLT_MAX);

        // Carry-over buffer for partial lines at IO block boundaries
        std::string carryOver;
        carryOver.reserve(512);

        auto flushBatch = [&]() {
            if (batchBuf.empty()) return;
            uploadComputeBatch(pointCloud,
                               batchBuf.data(),
                               static_cast<int>(batchBuf.size()),
                               batchIndex,
                               static_cast<int>(totalPoints));
            totalPoints += batchBuf.size();
            batchIndex++;
            batchBuf.clear();

            if (batchIndex % 1000 == 0)
                std::cout << "[TextPC] " << totalPoints << " points uploaded...\n";
        };

        constexpr size_t READ_CHUNK = 8 * 1024 * 1024; // 8 MB per read
        std::vector<char> readBuf(READ_CHUNK);

        while (file.read(readBuf.data(), static_cast<std::streamsize>(READ_CHUNK))
               || file.gcount() > 0)
        {
            const std::streamsize bytesRead = file.gcount();
            const char* src   = readBuf.data();
            const char* end   = src + bytesRead;
            const char* lineStart = src;

            while (lineStart < end) {
                // Find end of this line
                const char* nl = static_cast<const char*>(
                    std::memchr(lineStart, '\n', static_cast<size_t>(end - lineStart)));

                if (!nl) {
                    // Partial line at end of read chunk – carry it over
                    carryOver.append(lineStart, end - lineStart);
                    break;
                }

                // Build full line (prefix with carry-over if any)
                const char* lineData;
                size_t       lineLen;
                std::string  fullLine; // only allocated when carry-over is present

                if (!carryOver.empty()) {
                    fullLine  = std::move(carryOver);
                    carryOver.clear();
                    fullLine.append(lineStart, nl - lineStart);
                    lineData = fullLine.c_str();
                    lineLen  = fullLine.size();
                } else {
                    lineData = lineStart;
                    lineLen  = static_cast<size_t>(nl - lineStart);
                }

                lineStart = nl + 1;

                // Skip empty / whitespace-only / comment lines
                if (lineLen == 0) continue;
                const char first = lineData[0];
                if (!((first >= '0' && first <= '9') || first == '-' || first == '+'))
                    continue;

                // Apply downsampling
                const size_t myLine = lineCounter++;
                if (downsampleFactor > 1 && (myLine % downsampleFactor) != 0)
                    continue;

                // ── Parse line ───────────────────────────────────────────
                // Strategy: try from most-specific to least-specific format.
                //
                //  7 fields → XYZIRGB  (x y z intensity r g b)
                //  6 fields → XYZRGB   (x y z r g b, no intensity)
                //  4 fields → XYZI     (x y z intensity)
                //  3 fields → XYZ      (x y z)
                //
                // The naive single-format approach (sscanf "%f %f %f %f %d %d %d")
                // misreads XYZRGB (6 cols) as XYZI+partial-RGB because %f happily
                // consumes an integer (128 → 128.0) as the intensity field,
                // shifting R/G/B one column to the right and dropping B entirely.
                // The two-step below avoids that shift.
                float x, y, z, intensity = 1.0f;
                int   r = 255, g = 255, b = 255;

                const int n7 = sscanf_s(lineData,
                                        "%f %f %f %f %d %d %d",
                                        &x, &y, &z, &intensity, &r, &g, &b);
                if (n7 == 7) {
                    // XYZIRGB – all fields present, nothing to do
                } else {
                    // Re-try with integer 4th field to detect XYZRGB
                    int ri = 255;
                    const int n6 = sscanf_s(lineData,
                                            "%f %f %f %d %d %d",
                                            &x, &y, &z, &ri, &g, &b);
                    if (n6 == 6) {
                        // XYZRGB (no intensity column)
                        r = ri;
                        intensity = 1.0f;
                    } else if (n7 >= 4) {
                        // XYZI – intensity was parsed correctly by first sscanf,
                        // but there were no valid integer colour fields.
                        r = g = b = 255;
                    } else if (n7 == 3 || n6 >= 3) {
                        // XYZ only
                        intensity = 1.0f;
                        r = g = b = 255;
                    } else {
                        continue; // unparseable line
                    }
                }

                PointCloudPoint pt;
                pt.position  = glm::vec3(x, y, z);
                pt.intensity = intensity;
                pt.color     = glm::vec3(r / 255.0f, g / 255.0f, b / 255.0f);

                gMin = glm::min(gMin, pt.position);
                gMax = glm::max(gMax, pt.position);
                batchBuf.push_back(pt);

                if (static_cast<int>(batchBuf.size()) == PointCloud::kComputeBatchSize)
                    flushBatch();
            }
        }

        // Handle trailing carry-over without newline (same multi-format logic)
        if (!carryOver.empty()) {
            const char* lineData = carryOver.c_str();
            const char  first    = lineData[0];
            if ((first >= '0' && first <= '9') || first == '-' || first == '+') {
                float x, y, z, intensity = 1.0f;
                int   r = 255, g = 255, b = 255;
                const int n7 = sscanf_s(lineData, "%f %f %f %f %d %d %d",
                                        &x, &y, &z, &intensity, &r, &g, &b);
                bool valid = true;
                if (n7 < 7) {
                    int ri = 255;
                    const int n6 = sscanf_s(lineData, "%f %f %f %d %d %d",
                                            &x, &y, &z, &ri, &g, &b);
                    if (n6 == 6)      { r = ri; intensity = 1.0f; }
                    else if (n7 >= 4) { r = g = b = 255; }
                    else if (n7 == 3 || n6 >= 3) { intensity = 1.0f; r = g = b = 255; }
                    else              { valid = false; }
                }
                if (valid) {
                    PointCloudPoint pt;
                    pt.position  = glm::vec3(x, y, z);
                    pt.intensity = intensity;
                    pt.color     = glm::vec3(r / 255.0f, g / 255.0f, b / 255.0f);
                    gMin = glm::min(gMin, pt.position);
                    gMax = glm::max(gMax, pt.position);
                    batchBuf.push_back(pt);
                }
            }
        }

        flushBatch(); // Upload last partial batch
        file.close();

        // Trim SSBOs if we over-allocated (header lines, blank lines, etc.)
        if (totalPoints < allocPoints) {
            trimComputeSSBOs(pointCloud, totalPoints);
        }

        pointCloud.numBatches      = static_cast<uint32_t>(batchIndex);
        pointCloud.totalPointCount = static_cast<uint32_t>(totalPoints);
        pointCloud.boundsMin       = gMin;
        pointCloud.boundsMax       = gMax;

        std::cout << "[TextPC] Loaded " << totalPoints << " points into "
                  << batchIndex << " compute batches (no octree, no CPU copy)\n";

        return std::move(pointCloud);
    }

    bool PointCloudLoader::exportToXYZ(const PointCloud& pointCloud, const std::string& filePath) {
        std::ofstream file(filePath);
        if (!file.is_open()) {
            std::cerr << "Failed to open file for writing: " << filePath << std::endl;
            return false;
        }

        // Create transformation matrix
        glm::mat4 transform = glm::mat4(1.0f);
        transform = glm::translate(transform, pointCloud.position);
        transform = glm::rotate(transform, glm::radians(pointCloud.rotation.x), glm::vec3(1, 0, 0));
        transform = glm::rotate(transform, glm::radians(pointCloud.rotation.y), glm::vec3(0, 1, 0));
        transform = glm::rotate(transform, glm::radians(pointCloud.rotation.z), glm::vec3(0, 0, 1));
        transform = glm::scale(transform, pointCloud.scale);

        for (const auto& point : pointCloud.points) {
            // Apply transformation
            glm::vec4 transformedPos = transform * glm::vec4(point.position, 1.0f);

            file << std::fixed << std::setprecision(3)
                << transformedPos.x << " "
                << transformedPos.y << " "
                << transformedPos.z << " "
                << static_cast<int>(point.intensity * 1000) << " "
                << static_cast<int>(point.color.r * 255) << " "
                << static_cast<int>(point.color.g * 255) << " "
                << static_cast<int>(point.color.b * 255) << "\n";
        }

        file.close();
        return true;
    }

    bool PointCloudLoader::exportToBinary(const PointCloud& pointCloud, const std::string& filePath) {
        std::ofstream file(filePath, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "Failed to open file for writing: " << filePath << std::endl;
            return false;
        }

        // Create transformation matrix
        glm::mat4 transform = glm::mat4(1.0f);
        transform = glm::translate(transform, pointCloud.position);
        transform = glm::rotate(transform, glm::radians(pointCloud.rotation.x), glm::vec3(1, 0, 0));
        transform = glm::rotate(transform, glm::radians(pointCloud.rotation.y), glm::vec3(0, 1, 0));
        transform = glm::rotate(transform, glm::radians(pointCloud.rotation.z), glm::vec3(0, 0, 1));
        transform = glm::scale(transform, pointCloud.scale);

        // Write header
        file.write(BINARY_MAGIC_NUMBER, 4);
        uint32_t numPoints = pointCloud.points.size();
        file.write(reinterpret_cast<const char*>(&numPoints), sizeof(numPoints));

        // Write point data
        for (const auto& point : pointCloud.points) {
            // Apply transformation
            glm::vec4 transformedPos = transform * glm::vec4(point.position, 1.0f);
            glm::vec3 finalPos(transformedPos);

            file.write(reinterpret_cast<const char*>(&finalPos), sizeof(finalPos));

            uint32_t intensity = static_cast<uint32_t>(point.intensity * 1000);
            file.write(reinterpret_cast<const char*>(&intensity), sizeof(intensity));

            glm::u8vec3 color = glm::u8vec3(point.color * 255.0f);
            file.write(reinterpret_cast<const char*>(&color), sizeof(color));
        }

        file.close();
        return true;
    }

    struct IVec3Comparator{
    bool operator()(const glm::ivec3 & lhs, const glm::ivec3 & rhs) const {
        if (lhs.x != rhs.x) return lhs.x < rhs.x;
        if (lhs.y != rhs.y) return lhs.y < rhs.y;
        return lhs.z < rhs.z;
    }
    };

    PointCloud PointCloudLoader::loadFromBinary(const std::string& filePath) {
        PointCloud pointCloud;
        pointCloud.name     = "PointCloud_" + std::filesystem::path(filePath).filename().string();
        pointCloud.position = glm::vec3(0.0f);
        pointCloud.rotation = glm::vec3(0.0f);
        pointCloud.scale    = glm::vec3(1.0f);

        std::ifstream file(filePath, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "[PCB] Failed to open: " << filePath << "\n";
            return std::move(pointCloud);
        }

        try {
            // ── Header ───────────────────────────────────────────────────────
            char magic[4];
            file.read(magic, 4);
            if (std::memcmp(magic, BINARY_MAGIC_NUMBER, 4) != 0)
                throw std::runtime_error("Invalid binary point cloud file format (bad magic)");

            uint32_t numPoints = 0;
            file.read(reinterpret_cast<char*>(&numPoints), sizeof(numPoints));
            std::cout << "[PCB] " << numPoints << " points in " << filePath << "\n";

            // ── Allocate SSBOs up-front using exact header count ─────────────
            allocateComputeSSBOs(pointCloud, numPoints);

            // ── Stream-read in batches ────────────────────────────────────────
            // The binary format stores each point as: vec3 | uint32_t | u8vec3
            constexpr size_t PT_SIZE = sizeof(glm::vec3) + sizeof(uint32_t) + sizeof(glm::u8vec3);

            std::vector<PointCloudPoint> batchBuf;
            batchBuf.reserve(PointCloud::kComputeBatchSize);

            // Raw read buffer: holds exactly kComputeBatchSize raw points
            const size_t rawChunkBytes = static_cast<size_t>(PointCloud::kComputeBatchSize) * PT_SIZE;
            std::vector<char> rawBuf(rawChunkBytes);

            int    batchIdx    = 0;
            size_t uploadedPts = 0;
            size_t remaining   = numPoints;
            glm::vec3 gMin( FLT_MAX), gMax(-FLT_MAX);

            while (remaining > 0) {
                const size_t toRead      = std::min(remaining,
                                                    static_cast<size_t>(PointCloud::kComputeBatchSize));
                const size_t bytesToRead = toRead * PT_SIZE;

                file.read(rawBuf.data(), static_cast<std::streamsize>(bytesToRead));
                const size_t actualBytes = static_cast<size_t>(file.gcount());
                const size_t actualPts   = actualBytes / PT_SIZE;

                if (actualPts == 0) break;

                batchBuf.clear();
                batchBuf.reserve(actualPts);

                const char* src = rawBuf.data();
                for (size_t i = 0; i < actualPts; i++) {
                    PointCloudPoint pt;
                    std::memcpy(&pt.position, src, sizeof(pt.position));
                    src += sizeof(pt.position);

                    uint32_t rawIntensity;
                    std::memcpy(&rawIntensity, src, sizeof(rawIntensity));
                    pt.intensity = rawIntensity / 1000.0f;
                    src += sizeof(rawIntensity);

                    glm::u8vec3 c;
                    std::memcpy(&c, src, sizeof(c));
                    pt.color = glm::vec3(c) / 255.0f;
                    src += sizeof(c);

                    gMin = glm::min(gMin, pt.position);
                    gMax = glm::max(gMax, pt.position);
                    batchBuf.push_back(pt);
                }

                uploadComputeBatch(pointCloud, batchBuf.data(),
                                   static_cast<int>(batchBuf.size()),
                                   batchIdx, static_cast<int>(uploadedPts));
                uploadedPts += batchBuf.size();
                batchIdx++;
                remaining   -= actualPts;

                if (batchIdx % 2000 == 0)
                    std::cout << "[PCB] " << uploadedPts << " points uploaded...\n";
            }

            file.close();

            pointCloud.numBatches      = static_cast<uint32_t>(batchIdx);
            pointCloud.totalPointCount = static_cast<uint32_t>(uploadedPts);
            pointCloud.boundsMin       = gMin;
            pointCloud.boundsMax       = gMax;

            std::cout << "[PCB] Streamed " << uploadedPts << " points into "
                      << batchIdx << " compute batches\n";
        }
        catch (const std::exception& e) {
            std::cerr << "[PCB] Error: " << e.what() << "\n";
            deleteComputeSSBOs(pointCloud);
        }

        return std::move(pointCloud);
    }


    void PointCloudLoader::setupPointCloudGLBuffers(PointCloud& pointCloud) {
        // The compute rasterizer path does not use a VAO/VBO.
        // SSBOs are built incrementally by the streaming loaders via
        // allocateComputeSSBOs / uploadComputeBatch.  This function is kept
        // for any legacy callers and handles the rare case where a loader
        // has already populated pointCloud.points (e.g. the f5 separate-arrays
        // path).  For normal streaming loads, this is a no-op.
        if (!pointCloud.points.empty() && pointCloud.numBatches == 0) {
            const size_t N = pointCloud.points.size();

            // Compute bounds before clearing the CPU vector
            glm::vec3 bMin(FLT_MAX), bMax(-FLT_MAX);
            for (const auto& p : pointCloud.points) {
                bMin = glm::min(bMin, p.position);
                bMax = glm::max(bMax, p.position);
            }

            allocateComputeSSBOs(pointCloud, N);

            const int batchSz = PointCloud::kComputeBatchSize;
            int batchIdx = 0;
            for (size_t first = 0; first < N; first += batchSz, batchIdx++) {
                int count = static_cast<int>(std::min((size_t)batchSz, N - first));
                uploadComputeBatch(pointCloud,
                                   pointCloud.points.data() + first,
                                   count, batchIdx, static_cast<int>(first));
            }
            pointCloud.numBatches       = static_cast<uint32_t>(batchIdx);
            pointCloud.totalPointCount  = static_cast<uint32_t>(N);
            pointCloud.boundsMin        = bMin;
            pointCloud.boundsMax        = bMax;

            // Release CPU-side storage – the GPU now owns all the data
            pointCloud.points.clear();
            pointCloud.points.shrink_to_fit();

            std::cout << "[ComputePC] setupPointCloudGLBuffers: built "
                      << pointCloud.numBatches << " batches (" << N << " points)\n";
        }
    }

    PointCloud PointCloudLoader::loadFromHDF5(const std::string& filePath, size_t downsampleFactor) {
        PointCloud pointCloud;
        pointCloud.name = "PointCloud_" + std::filesystem::path(filePath).filename().string();
        pointCloud.position = glm::vec3(0.0f);
        pointCloud.rotation = glm::vec3(0.0f);
        pointCloud.scale = glm::vec3(1.0f);
        
        // Declare coordinate storage variables at function scope
        std::vector<float> xCoords, yCoords, zCoords;
        std::vector<float> rColors, gColors, bColors;
        std::vector<float> intensities;

        try {
            std::cout << "Loading HDF5 point cloud from: " << filePath << std::endl;
            
            // Open the HDF5 file
            H5File file(filePath, H5F_ACC_RDONLY);
            
            // Define the HDF5 compound type for PointCloudPoint
            CompType pointType(sizeof(PointCloudPoint));
            pointType.insertMember("position_x", HOFFSET(PointCloudPoint, position.x), PredType::NATIVE_FLOAT);
            pointType.insertMember("position_y", HOFFSET(PointCloudPoint, position.y), PredType::NATIVE_FLOAT);
            pointType.insertMember("position_z", HOFFSET(PointCloudPoint, position.z), PredType::NATIVE_FLOAT);
            pointType.insertMember("intensity", HOFFSET(PointCloudPoint, intensity), PredType::NATIVE_FLOAT);
            pointType.insertMember("color_r", HOFFSET(PointCloudPoint, color.r), PredType::NATIVE_FLOAT);
            pointType.insertMember("color_g", HOFFSET(PointCloudPoint, color.g), PredType::NATIVE_FLOAT);
            pointType.insertMember("color_b", HOFFSET(PointCloudPoint, color.b), PredType::NATIVE_FLOAT);

            // Try to open the main dataset (common names)
            DataSet dataset;
            std::vector<std::string> datasetNames = {"points", "point_cloud", "data", "vertices"};
            std::string timeSeriesDataset; // Declare at broader scope
            
            bool datasetFound = false;
            for (const auto& name : datasetNames) {
                try {
                    dataset = file.openDataSet(name);
                    datasetFound = true;
                    std::cout << "Found dataset: " << name << std::endl;
                    break;
                } catch (const H5::Exception&) {
                    continue;
                }
            }

            if (!datasetFound) {
                // List available datasets
                std::cout << "Available datasets in file:" << std::endl;
                hsize_t numObjs = file.getNumObjs();
                for (hsize_t i = 0; i < numObjs; i++) {
                    std::string objName = file.getObjnameByIdx(i);
                    std::cout << "  - " << objName << std::endl;
                }
                
                // Look for time-series data (t=timestamp format)
                for (hsize_t i = 0; i < numObjs; i++) {
                    std::string objName = file.getObjnameByIdx(i);
                    if (objName.substr(0, 2) == "t=") {
                        timeSeriesDataset = objName;
                        break;
                    }
                }
                
                if (!timeSeriesDataset.empty()) {
                    std::cout << "Found time-series dataset: " << timeSeriesDataset << std::endl;
                    try {
                        // Try to open the time-series dataset as a group first
                        Group timeGroup = file.openGroup(timeSeriesDataset);
                        
                        // Look for point cloud data within the time group
                        hsize_t numGroupObjs = timeGroup.getNumObjs();
                        std::cout << "Objects in " << timeSeriesDataset << ":" << std::endl;
                        
                        bool foundPointData = false;
                        for (hsize_t j = 0; j < numGroupObjs; j++) {
                            std::string groupObjName = timeGroup.getObjnameByIdx(j);
                            std::cout << "  - " << groupObjName << std::endl;
                            
                            // Look for common point cloud data names
                            if (groupObjName == "vertices" || groupObjName == "points" || 
                                groupObjName == "coordinates" || groupObjName == "positions" ||
                                groupObjName == "Mesh" || groupObjName == "mesh") {
                                try {
                                    dataset = timeGroup.openDataSet(groupObjName);
                                    datasetFound = true;
                                    foundPointData = true;
                                    std::cout << "Using dataset: " << timeSeriesDataset << "/" << groupObjName << std::endl;
                                    break;
                                } catch (const H5::Exception&) {
                                    continue;
                                }
                            }
                        }
                        
                        if (!foundPointData && numGroupObjs > 0) {
                            // Try the first object in the time group
                            std::string firstGroupObj = timeGroup.getObjnameByIdx(0);
                            try {
                                dataset = timeGroup.openDataSet(firstGroupObj);
                                datasetFound = true;
                                std::cout << "Using first object in time group: " << timeSeriesDataset << "/" << firstGroupObj << std::endl;
                            } catch (const H5::Exception&) {
                                std::cout << "First object is not a dataset, trying as nested group" << std::endl;
                                
                                // Try to open as a nested group
                                try {
                                    Group nestedGroup = timeGroup.openGroup(firstGroupObj);
                                    hsize_t numNestedObjs = nestedGroup.getNumObjs();
                                    std::cout << "Objects in " << timeSeriesDataset << "/" << firstGroupObj << ":" << std::endl;
                                    
                                    // Look for datasets in the nested group
                                    for (hsize_t k = 0; k < numNestedObjs; k++) {
                                        std::string nestedObjName = nestedGroup.getObjnameByIdx(k);
                                        std::cout << "  - " << nestedObjName << std::endl;
                                        
                                        // Look for common mesh/point cloud names
                                        if (nestedObjName == "vertices" || nestedObjName == "points" || 
                                            nestedObjName == "coordinates" || nestedObjName == "positions" ||
                                            nestedObjName == "Mesh" || nestedObjName == "mesh" ||
                                            nestedObjName == "geometry" || nestedObjName == "cells" ||
                                            nestedObjName == "topology" || nestedObjName == "Points" ||
                                            nestedObjName == "VerticesSet") {
                                            try {
                                                dataset = nestedGroup.openDataSet(nestedObjName);
                                                datasetFound = true;
                                                foundPointData = true;
                                                std::cout << "Using nested dataset: " << timeSeriesDataset << "/" << firstGroupObj << "/" << nestedObjName << std::endl;
                                                break;
                                            } catch (const H5::Exception&) {
                                                // Try opening as another nested group
                                                try {
                                                    Group deepNestedGroup = nestedGroup.openGroup(nestedObjName);
                                                    hsize_t numDeepObjs = deepNestedGroup.getNumObjs();
                                                    std::cout << "Objects in " << timeSeriesDataset << "/" << firstGroupObj << "/" << nestedObjName << ":" << std::endl;
                                                    
                                                    for (hsize_t l = 0; l < numDeepObjs; l++) {
                                                        std::string deepObjName = deepNestedGroup.getObjnameByIdx(l);
                                                        std::cout << "    - " << deepObjName << std::endl;
                                                        
                                                        try {
                                                            dataset = deepNestedGroup.openDataSet(deepObjName);
                                                            datasetFound = true;
                                                            foundPointData = true;
                                                            std::cout << "Using deep nested dataset: " << timeSeriesDataset << "/" << firstGroupObj << "/" << nestedObjName << "/" << deepObjName << std::endl;
                                                            break;
                                                        } catch (const H5::Exception&) {
                                                            // Try one more level - maybe it's a group too
                                                            try {
                                                                Group veryDeepGroup = deepNestedGroup.openGroup(deepObjName);
                                                                hsize_t numVeryDeepObjs = veryDeepGroup.getNumObjs();
                                                                std::cout << "Objects in " << timeSeriesDataset << "/" << firstGroupObj << "/" << nestedObjName << "/" << deepObjName << ":" << std::endl;
                                                                
                                                                for (hsize_t m = 0; m < numVeryDeepObjs; m++) {
                                                                    std::string veryDeepObjName = veryDeepGroup.getObjnameByIdx(m);
                                                                    std::cout << "      - " << veryDeepObjName << std::endl;
                                                                    
                                                                    try {
                                                                        dataset = veryDeepGroup.openDataSet(veryDeepObjName);
                                                                        datasetFound = true;
                                                                        foundPointData = true;
                                                                        std::cout << "Using very deep nested dataset: " << timeSeriesDataset << "/" << firstGroupObj << "/" << nestedObjName << "/" << deepObjName << "/" << veryDeepObjName << std::endl;
                                                                        break;
                                                                    } catch (const H5::Exception&) {
                                                                        // Try one more level - 5th level deep
                                                                        try {
                                                                            Group ultraDeepGroup = veryDeepGroup.openGroup(veryDeepObjName);
                                                                            hsize_t numUltraDeepObjs = ultraDeepGroup.getNumObjs();
                                                                            std::cout << "Objects in " << timeSeriesDataset << "/" << firstGroupObj << "/" << nestedObjName << "/" << deepObjName << "/" << veryDeepObjName << ":" << std::endl;
                                                                            
                                                                            for (hsize_t n = 0; n < numUltraDeepObjs; n++) {
                                                                                std::string ultraDeepObjName = ultraDeepGroup.getObjnameByIdx(n);
                                                                                std::cout << "        - " << ultraDeepObjName << std::endl;
                                                                                
                                                                                try {
                                                                                    dataset = ultraDeepGroup.openDataSet(ultraDeepObjName);
                                                                                    datasetFound = true;
                                                                                    foundPointData = true;
                                                                                    std::cout << "Using ultra deep nested dataset: " << timeSeriesDataset << "/" << firstGroupObj << "/" << nestedObjName << "/" << deepObjName << "/" << veryDeepObjName << "/" << ultraDeepObjName << std::endl;
                                                                                    break;
                                                                                } catch (const H5::Exception&) {
                                                                                    continue;
                                                                                }
                                                                            }
                                                                            if (foundPointData) break;
                                                                        } catch (const H5::Exception&) {
                                                                            continue;
                                                                        }
                                                                    }
                                                                }
                                                                if (foundPointData) break;
                                                            } catch (const H5::Exception&) {
                                                                continue;
                                                            }
                                                        }
                                                    }
                                                    if (foundPointData) break;
                                                } catch (const H5::Exception&) {
                                                    continue;
                                                }
                                            }
                                        }
                                    }
                                    
                                    // If no specific names found, try the first object in nested group
                                    if (!foundPointData && numNestedObjs > 0) {
                                        std::string firstNestedObj = nestedGroup.getObjnameByIdx(0);
                                        try {
                                            dataset = nestedGroup.openDataSet(firstNestedObj);
                                            datasetFound = true;
                                            std::cout << "Using first nested dataset: " << timeSeriesDataset << "/" << firstGroupObj << "/" << firstNestedObj << std::endl;
                                        } catch (const H5::Exception&) {
                                            std::cout << "First nested object is also not a dataset" << std::endl;
                                        }
                                    }
                                } catch (const H5::Exception&) {
                                    std::cout << "Could not open first object as nested group either" << std::endl;
                                }
                            }
                        }
                    } catch (const H5::Exception&) {
                        std::cout << "Time-series object is not a group, trying as dataset" << std::endl;
                        try {
                            dataset = file.openDataSet(timeSeriesDataset);
                            datasetFound = true;
                            std::cout << "Using time-series dataset: " << timeSeriesDataset << std::endl;
                        } catch (const H5::Exception&) {
                            std::cout << "Could not open time-series as dataset either" << std::endl;
                        }
                    }
                }
                
                // Special handler for f5 files with known structure: Selection/Points/StandardCartesianChart3D/
                if (!datasetFound && !timeSeriesDataset.empty() && filePath.substr(filePath.find_last_of(".") + 1) == "f5") {
                    std::cout << "Attempting f5-specific structure navigation..." << std::endl;
                    try {
                        // Navigate the known f5 structure: t=timestamp/Selection/Points/StandardCartesianChart3D/
                        Group timeGroup = file.openGroup(timeSeriesDataset);
                        Group selectionGroup = timeGroup.openGroup("Selection");
                        Group pointsGroup = selectionGroup.openGroup("Points");
                        Group chartGroup = pointsGroup.openGroup("StandardCartesianChart3D");
                        
                        std::cout << "Successfully navigated to StandardCartesianChart3D group" << std::endl;
                        
                        // Comprehensive exploration of the StandardCartesianChart3D structure
                        hsize_t numChartObjs = chartGroup.getNumObjs();
                        std::cout << "\n=== F5 FILE STRUCTURE ANALYSIS ===" << std::endl;
                        std::cout << "Found " << numChartObjs << " objects in StandardCartesianChart3D:" << std::endl;
                        
                        // Collect all available objects and analyze their structure
                        std::vector<std::string> allObjects;
                        for (hsize_t i = 0; i < numChartObjs; i++) {
                            std::string objName = chartGroup.getObjnameByIdx(i);
                            allObjects.push_back(objName);
                            std::cout << "\n[" << (i+1) << "] Object: " << objName << std::endl;
                            
                            // Try to explore this object as a group
                            try {
                                Group subGroup = chartGroup.openGroup(objName);
                                hsize_t numSubObjs = subGroup.getNumObjs();
                                std::cout << "    Type: Group (" << numSubObjs << " sub-objects)" << std::endl;
                                
                                // List sub-objects and check if they contain datasets
                                for (hsize_t j = 0; j < numSubObjs; j++) {
                                    std::string subObjName = subGroup.getObjnameByIdx(j);
                                    std::cout << "    ├─ " << subObjName;
                                    
                                    // Check if this sub-object is a dataset
                                    try {
                                        DataSet subDataset = subGroup.openDataSet(subObjName);
                                        DataSpace subSpace = subDataset.getSpace();
                                        int subRank = subSpace.getSimpleExtentNdims();
                                        hsize_t subDims[3];
                                        subSpace.getSimpleExtentDims(subDims, NULL);
                                        
                                        std::cout << " (Dataset: ";
                                        for (int k = 0; k < subRank; k++) {
                                            std::cout << subDims[k];
                                            if (k < subRank - 1) std::cout << "×";
                                        }
                                        std::cout << ")" << std::endl;
                                        
                                        // Check data type and compression
                                        DataType subType = subDataset.getDataType();
                                        H5T_class_t subTypeClass = subType.getClass();
                                        std::cout << "      Data type: ";
                                        switch(subTypeClass) {
                                            case H5T_FLOAT: std::cout << "Float"; break;
                                            case H5T_INTEGER: std::cout << "Integer"; break;
                                            case H5T_COMPOUND: std::cout << "Compound"; break;
                                            default: std::cout << "Other"; break;
                                        }
                                        
                                        // Check if dataset has filters (compression)
                                        try {
                                            DSetCreatPropList plist = subDataset.getCreatePlist();
                                            H5D_layout_t layout = plist.getLayout();
                                            if (layout == H5D_CHUNKED) {
                                                std::cout << " (Chunked - possibly compressed)";
                                            }
                                        } catch (const H5::Exception&) {
                                            // Can't determine layout
                                        }
                                        std::cout << std::endl;
                                        
                                    } catch (const H5::Exception&) {
                                        // Not a dataset, might be another group
                                        try {
                                            Group subSubGroup = subGroup.openGroup(subObjName);
                                            hsize_t numSubSubObjs = subSubGroup.getNumObjs();
                                            std::cout << " (Group: " << numSubSubObjs << " objects)" << std::endl;
                                        } catch (const H5::Exception&) {
                                            std::cout << " (Unknown type)" << std::endl;
                                        }
                                    }
                                }
                            } catch (const H5::Exception&) {
                                // Not a group, try as dataset
                                try {
                                    DataSet objDataset = chartGroup.openDataSet(objName);
                                    DataSpace objSpace = objDataset.getSpace();
                                    int objRank = objSpace.getSimpleExtentNdims();
                                    hsize_t objDims[3];
                                    objSpace.getSimpleExtentDims(objDims, NULL);
                                    
                                    std::cout << "    Type: Dataset (";
                                    for (int k = 0; k < objRank; k++) {
                                        std::cout << objDims[k];
                                        if (k < objRank - 1) std::cout << "×";
                                    }
                                    std::cout << ")" << std::endl;
                                } catch (const H5::Exception&) {
                                    std::cout << "    Type: Unknown" << std::endl;
                                }
                            }
                        }
                        
                        std::cout << "\n=== COMPRESSION ANALYSIS ===" << std::endl;
                        std::cout << "This f5 file appears to use LZ4 compression which requires additional HDF5 plugins." << std::endl;
                        std::cout << "To read this file, you have several options:" << std::endl;
                        std::cout << "1. Install HDF5 LZ4 plugin from: https://github.com/HDFGroup/hdf5_plugins" << std::endl;
                        std::cout << "2. Convert the file to uncompressed HDF5 format using h5repack:" << std::endl;
                        std::cout << "   h5repack -f NONE input.f5 output.h5" << std::endl;
                        std::cout << "3. Use a different tool to export point cloud data to a supported format (PLY, XYZ, etc.)" << std::endl;
                        std::cout << "==============================" << std::endl;
                        
                        // Check what kinds of groups are actually available
                        bool hasPositions = false, hasRGB = false, hasIntensity = false;
                        
                        // Look for known data groups
                        for (hsize_t i = 0; i < numChartObjs; i++) {
                            std::string objName = chartGroup.getObjnameByIdx(i);
                            if (objName == "Positions" || objName.find("Position") != std::string::npos) {
                                hasPositions = true;
                            } else if (objName == "RGB" || objName.find("Color") != std::string::npos) {
                                hasRGB = true;
                            } else if (objName == "Intensity" || objName.find("Intensity") != std::string::npos) {
                                hasIntensity = true;
                            }
                        }
                        
                        std::cout << "Available data types: ";
                        if (hasPositions) std::cout << "Positions ";
                        if (hasRGB) std::cout << "RGB ";
                        if (hasIntensity) std::cout << "Intensity ";
                        std::cout << std::endl;
                        
                        // Try to read from any available group
                        for (hsize_t i = 0; i < numChartObjs; i++) {
                            std::string objName = chartGroup.getObjnameByIdx(i);
                            
                            try {
                                Group dataGroup = chartGroup.openGroup(objName);
                                hsize_t numDataObjs = dataGroup.getNumObjs();
                                
                                std::cout << "Trying to read from " << objName << " group..." << std::endl;
                                
                                for (hsize_t j = 0; j < numDataObjs; j++) {
                                    std::string dataObjName = dataGroup.getObjnameByIdx(j);
                                    
                                    try {
                                        DataSet dataDataset = dataGroup.openDataSet(dataObjName);
                                        
                                        // Check if this dataset is readable (not compressed with unsupported filter)
                                        DataSpace dataSpace = dataDataset.getSpace();
                                        int dataRank = dataSpace.getSimpleExtentNdims();
                                        hsize_t dataDims[3];
                                        dataSpace.getSimpleExtentDims(dataDims, NULL);
                                        
                                        std::cout << "  Dataset " << dataObjName << " - dimensions: ";
                                        for (int k = 0; k < dataRank; k++) {
                                            std::cout << dataDims[k];
                                            if (k < dataRank - 1) std::cout << " x ";
                                        }
                                        std::cout << std::endl;
                                        
                                        // Try a small test read to check if data is accessible
                                        if (dataRank == 2 && dataDims[1] == 3 && dataDims[0] > 0) {
                                            std::cout << "  Attempting to read sample data..." << std::endl;
                                            
                                            // Try to read just the first few points as a test
                                            hsize_t testPoints = std::min((hsize_t)10, dataDims[0]);
                                            std::vector<float> testData(testPoints * 3);
                                            
                                            // Create a hyperslab for just the first few points
                                            hsize_t start[2] = {0, 0};
                                            hsize_t count[2] = {testPoints, 3};
                                            DataSpace memSpace(2, count);
                                            dataSpace.selectHyperslab(H5S_SELECT_SET, count, start);
                                            
                                            try {
                                                dataDataset.read(testData.data(), PredType::NATIVE_FLOAT, memSpace, dataSpace);
                                                std::cout << "  Successfully read test data! First point: (" 
                                                         << testData[0] << ", " << testData[1] << ", " << testData[2] << ")" << std::endl;
                                                
                                                // If we can read test data, try to read all data
                                                dataSpace.selectAll();
                                                std::vector<float> allData(dataDims[0] * 3);
                                                DataSpace fullMemSpace(2, dataDims);
                                                dataDataset.read(allData.data(), PredType::NATIVE_FLOAT, fullMemSpace, dataSpace);
                                                
                                                // Store the coordinates
                                                xCoords.resize(dataDims[0]);
                                                yCoords.resize(dataDims[0]);
                                                zCoords.resize(dataDims[0]);
                                                
                                                for (hsize_t k = 0; k < dataDims[0]; k++) {
                                                    xCoords[k] = allData[k * 3 + 0];
                                                    yCoords[k] = allData[k * 3 + 1];
                                                    zCoords[k] = allData[k * 3 + 2];
                                                }
                                                
                                                std::cout << "Successfully read all coordinate data: " << dataDims[0] << " points" << std::endl;
                                                datasetFound = true;
                                                break;
                                                
                                            } catch (const H5::Exception& readEx) {
                                                std::cout << "  Could not read data (compression/filter issue): " << readEx.getDetailMsg() << std::endl;
                                                if (readEx.getDetailMsg().find("lz4") != std::string::npos || 
                                                    readEx.getDetailMsg().find("filter") != std::string::npos) {
                                                    std::cout << "  ERROR: This f5 file uses LZ4 compression which requires additional HDF5 plugins." << std::endl;
                                                    std::cout << "  Please install the HDF5 LZ4 plugin or convert the file to an uncompressed format." << std::endl;
                                                }
                                            }
                                        }
                                        
                                    } catch (const H5::Exception&) {
                                        continue;
                                    }
                                }
                                
                                if (datasetFound) break;
                                
                            } catch (const H5::Exception&) {
                                continue;
                            }
                        }
                        
                    } catch (const H5::Exception& e) {
                        std::cout << "Failed to navigate f5 structure: " << e.getDetailMsg() << std::endl;
                    }
                }
                
                if (!datasetFound && numObjs > 0) {
                    std::string firstDatasetName = file.getObjnameByIdx(0);
                    try {
                        dataset = file.openDataSet(firstDatasetName);
                        datasetFound = true;
                        std::cout << "Using first object as dataset: " << firstDatasetName << std::endl;
                    } catch (const H5::Exception&) {
                        throw std::runtime_error("No valid datasets found in HDF5 file");
                    }
                }
                
                if (!datasetFound) {
                    throw std::runtime_error("No datasets found in HDF5 file");
                }
            }

            // Get dataset dimensions
            DataSpace dataspace = dataset.getSpace();
            int rank = dataspace.getSimpleExtentNdims();
            hsize_t dims[2];
            dataspace.getSimpleExtentDims(dims, NULL);
            
            hsize_t totalPoints = (rank == 1) ? dims[0] : dims[0];
            std::cout << "Dataset contains " << totalPoints << " points" << std::endl;

            // Handle downsampling
            hsize_t pointsToRead = totalPoints;
            if (downsampleFactor > 1) {
                pointsToRead = totalPoints / downsampleFactor;
                std::cout << "Downsampling by factor " << downsampleFactor 
                         << ", reading " << pointsToRead << " points" << std::endl;
            }

            // Try different data layouts
            DataType dtype = dataset.getDataType();
            H5T_class_t typeClass = dtype.getClass();

            if (typeClass == H5T_COMPOUND) {
                // ── Streaming compound-type read ─────────────────────────────
                // Read in chunks of kComputeBatchSize and upload each chunk
                // directly to GPU via uploadComputeBatch.  This keeps CPU RAM
                // usage at ~280 KB regardless of file size.
                allocateComputeSSBOs(pointCloud, static_cast<size_t>(pointsToRead));

                constexpr hsize_t CHUNK = static_cast<hsize_t>(PointCloud::kComputeBatchSize);
                std::vector<PointCloudPoint> chunkBuf(CHUNK);

                int       batchIdx    = 0;
                size_t    uploadedPts = 0;
                glm::vec3 gMin( FLT_MAX), gMax(-FLT_MAX);

                for (hsize_t readSoFar = 0; readSoFar < pointsToRead; ) {
                    hsize_t toRead = std::min(CHUNK, pointsToRead - readSoFar);

                    // File-side hyperslab: skip every downsampleFactor-th element
                    hsize_t fileStart[1] = { readSoFar * static_cast<hsize_t>(downsampleFactor) };
                    hsize_t cnt[1]       = { toRead };
                    hsize_t stride[1]    = { static_cast<hsize_t>(downsampleFactor) };
                    hsize_t block[1]     = { 1 };
                    hsize_t memDims[1]   = { toRead };

                    DataSpace memspace(1, memDims);
                    dataspace.selectHyperslab(H5S_SELECT_SET, cnt, fileStart, stride, block);
                    dataset.read(chunkBuf.data(), pointType, memspace, dataspace);

                    // Update global bounds from this chunk
                    for (hsize_t i = 0; i < toRead; i++) {
                        gMin = glm::min(gMin, chunkBuf[i].position);
                        gMax = glm::max(gMax, chunkBuf[i].position);
                    }

                    uploadComputeBatch(pointCloud, chunkBuf.data(),
                                       static_cast<int>(toRead), batchIdx,
                                       static_cast<int>(uploadedPts));
                    uploadedPts += toRead;
                    batchIdx++;
                    readSoFar += toRead;
                }

                pointCloud.numBatches      = static_cast<uint32_t>(batchIdx);
                pointCloud.totalPointCount = static_cast<uint32_t>(uploadedPts);
                pointCloud.boundsMin       = gMin;
                pointCloud.boundsMax       = gMax;

                std::cout << "[HDF5] Streamed " << uploadedPts << " points into "
                          << batchIdx << " compute batches\n";

            } else {
                // Handle separate arrays format (like f5 files)
                std::cout << "Reading data from separate arrays format..." << std::endl;
                
                // For f5 files, try to find the Positions, RGB, and Intensity groups
                if (filePath.substr(filePath.find_last_of(".") + 1) == "f5" && !timeSeriesDataset.empty()) {
                    try {
                        Group timeGroup = file.openGroup(timeSeriesDataset);
                        Group selectionGroup = timeGroup.openGroup("Selection");
                        Group pointsGroup = selectionGroup.openGroup("Points");
                        Group chartGroup = pointsGroup.openGroup("StandardCartesianChart3D");
                        
                        // Try to read positions data
                        
                        try {
                            Group positionsGroup = chartGroup.openGroup("Positions");
                            hsize_t numPosObjs = positionsGroup.getNumObjs();
                            
                            // Look for coordinate datasets
                            for (hsize_t i = 0; i < numPosObjs; i++) {
                                std::string posObjName = positionsGroup.getObjnameByIdx(i);
                                try {
                                    DataSet posDataset = positionsGroup.openDataSet(posObjName);
                                    DataSpace posSpace = posDataset.getSpace();
                                    hsize_t posDims[2];
                                    posSpace.getSimpleExtentDims(posDims, NULL);
                                    
                                    // Read the dataset - assume it contains 3D coordinates
                                    if (posDims[0] == totalPoints) {
                                        std::vector<float> tempData(totalPoints * 3);
                                        posDataset.read(tempData.data(), PredType::NATIVE_FLOAT);
                                        
                                        // Extract X, Y, Z coordinates
                                        xCoords.resize(totalPoints);
                                        yCoords.resize(totalPoints);
                                        zCoords.resize(totalPoints);
                                        
                                        for (hsize_t j = 0; j < totalPoints; j++) {
                                            xCoords[j] = tempData[j * 3 + 0];
                                            yCoords[j] = tempData[j * 3 + 1];
                                            zCoords[j] = tempData[j * 3 + 2];
                                        }
                                        std::cout << "Successfully read position data: " << totalPoints << " points" << std::endl;
                                        break;
                                    }
                                } catch (const H5::Exception&) {
                                    continue;
                                }
                            }
                        } catch (const H5::Exception& e) {
                            std::cout << "Could not read Positions group: " << e.getDetailMsg() << std::endl;
                        }
                        
                        // Try to read RGB data
                        try {
                            Group rgbGroup = chartGroup.openGroup("RGB");
                            hsize_t numRgbObjs = rgbGroup.getNumObjs();
                            
                            for (hsize_t i = 0; i < numRgbObjs; i++) {
                                std::string rgbObjName = rgbGroup.getObjnameByIdx(i);
                                try {
                                    DataSet rgbDataset = rgbGroup.openDataSet(rgbObjName);
                                    DataSpace rgbSpace = rgbDataset.getSpace();
                                    hsize_t rgbDims[2];
                                    rgbSpace.getSimpleExtentDims(rgbDims, NULL);
                                    
                                    if (rgbDims[0] == totalPoints) {
                                        std::vector<float> tempRgbData(totalPoints * 3);
                                        rgbDataset.read(tempRgbData.data(), PredType::NATIVE_FLOAT);
                                        
                                        rColors.resize(totalPoints);
                                        gColors.resize(totalPoints);
                                        bColors.resize(totalPoints);
                                        
                                        for (hsize_t j = 0; j < totalPoints; j++) {
                                            rColors[j] = tempRgbData[j * 3 + 0] / 255.0f; // Normalize if needed
                                            gColors[j] = tempRgbData[j * 3 + 1] / 255.0f;
                                            bColors[j] = tempRgbData[j * 3 + 2] / 255.0f;
                                        }
                                        std::cout << "Successfully read RGB data" << std::endl;
                                        break;
                                    }
                                } catch (const H5::Exception&) {
                                    continue;
                                }
                            }
                        } catch (const H5::Exception& e) {
                            std::cout << "Could not read RGB group: " << e.getDetailMsg() << std::endl;
                        }
                        
                        // Try to read Intensity data
                        try {
                            Group intensityGroup = chartGroup.openGroup("Intensity");
                            hsize_t numIntObjs = intensityGroup.getNumObjs();
                            
                            for (hsize_t i = 0; i < numIntObjs; i++) {
                                std::string intObjName = intensityGroup.getObjnameByIdx(i);
                                try {
                                    DataSet intDataset = intensityGroup.openDataSet(intObjName);
                                    DataSpace intSpace = intDataset.getSpace();
                                    hsize_t intDims[2];
                                    intSpace.getSimpleExtentDims(intDims, NULL);
                                    
                                    if (intDims[0] == totalPoints) {
                                        intensities.resize(totalPoints);
                                        intDataset.read(intensities.data(), PredType::NATIVE_FLOAT);
                                        std::cout << "Successfully read Intensity data" << std::endl;
                                        break;
                                    }
                                } catch (const H5::Exception&) {
                                    continue;
                                }
                            }
                        } catch (const H5::Exception& e) {
                            std::cout << "Could not read Intensity group: " << e.getDetailMsg() << std::endl;
                        }
                        
                        // If we successfully read coordinate data from f5, stream to GPU
                        if (!xCoords.empty() && !yCoords.empty() && !zCoords.empty()) {
                            const hsize_t numCoordPoints = static_cast<hsize_t>(xCoords.size());
                            const hsize_t ptsToUpload   = (downsampleFactor > 1)
                                                         ? numCoordPoints / static_cast<hsize_t>(downsampleFactor)
                                                         : numCoordPoints;

                            allocateComputeSSBOs(pointCloud, static_cast<size_t>(ptsToUpload));

                            std::vector<PointCloudPoint> batchBuf;
                            batchBuf.reserve(PointCloud::kComputeBatchSize);
                            int       batchIdx    = 0;
                            size_t    uploadedPts = 0;
                            glm::vec3 gMin( FLT_MAX), gMax(-FLT_MAX);

                            auto flushF5Batch = [&]() {
                                if (batchBuf.empty()) return;
                                uploadComputeBatch(pointCloud, batchBuf.data(),
                                                   static_cast<int>(batchBuf.size()),
                                                   batchIdx, static_cast<int>(uploadedPts));
                                uploadedPts += batchBuf.size();
                                batchIdx++;
                                batchBuf.clear();
                            };

                            for (hsize_t i = 0; i < ptsToUpload; i++) {
                                const hsize_t src = i * static_cast<hsize_t>(downsampleFactor);

                                PointCloudPoint pt;
                                pt.position.x = xCoords[src];
                                pt.position.y = yCoords[src];
                                pt.position.z = zCoords[src];

                                if (!rColors.empty() && src < rColors.size()) {
                                    pt.color.r = rColors[src];
                                    pt.color.g = gColors[src];
                                    pt.color.b = bColors[src];
                                } else {
                                    pt.color = glm::vec3(1.0f);
                                }

                                pt.intensity = (!intensities.empty() && src < intensities.size())
                                             ? intensities[src] : 1.0f;

                                gMin = glm::min(gMin, pt.position);
                                gMax = glm::max(gMax, pt.position);
                                batchBuf.push_back(pt);
                                if (static_cast<int>(batchBuf.size()) == PointCloud::kComputeBatchSize)
                                    flushF5Batch();
                            }
                            flushF5Batch();

                            // Release the large coordinate vectors
                            { std::vector<float>().swap(xCoords); }
                            { std::vector<float>().swap(yCoords); }
                            { std::vector<float>().swap(zCoords); }
                            { std::vector<float>().swap(rColors); }
                            { std::vector<float>().swap(gColors); }
                            { std::vector<float>().swap(bColors); }
                            { std::vector<float>().swap(intensities); }

                            pointCloud.numBatches      = static_cast<uint32_t>(batchIdx);
                            pointCloud.totalPointCount = static_cast<uint32_t>(uploadedPts);
                            pointCloud.boundsMin       = gMin;
                            pointCloud.boundsMax       = gMax;

                            std::cout << "[HDF5-f5] Streamed " << uploadedPts
                                      << " points into " << batchIdx << " compute batches\n";

                            file.close();
                            return std::move(pointCloud);

                        } else {
                            std::cout << "Could not find valid coordinate data in f5 file" << std::endl;
                        }
                        
                    } catch (const H5::Exception& e) {
                        std::cout << "Error reading f5 separate arrays: " << e.getDetailMsg() << std::endl;
                    }
                } else {
                    std::cout << "Separate arrays format not fully supported for non-f5 files" << std::endl;
                }
            }

            file.close();

            std::cout << "[HDF5] Load complete: " << pointCloud.totalPointCount
                      << " points in " << pointCloud.numBatches << " batches\n";

        } catch (const H5::Exception& e) {
            std::cerr << "HDF5 error loading point cloud: " << e.getDetailMsg() << std::endl;
            return std::move(pointCloud);
        } catch (const std::exception& e) {
            std::cerr << "Error loading HDF5 point cloud: " << e.what() << std::endl;
            return std::move(pointCloud);
        }

        // If the compound-type path already built the SSBOs, we're done.
        // If pointCloud.points was somehow populated (fallback paths), run the
        // legacy setup which will also stream-upload and clear the CPU vector.
        setupPointCloudGLBuffers(pointCloud);

        return std::move(pointCloud);
    }

    bool PointCloudLoader::exportToHDF5(const PointCloud& pointCloud, const std::string& filePath) {
        try {
            std::cout << "Exporting point cloud to HDF5: " << filePath << std::endl;
            
            // Create the HDF5 file
            H5File file(filePath, H5F_ACC_TRUNC);
            
            // Define the HDF5 compound type for PointCloudPoint
            CompType pointType(sizeof(PointCloudPoint));
            pointType.insertMember("position_x", HOFFSET(PointCloudPoint, position.x), PredType::NATIVE_FLOAT);
            pointType.insertMember("position_y", HOFFSET(PointCloudPoint, position.y), PredType::NATIVE_FLOAT);
            pointType.insertMember("position_z", HOFFSET(PointCloudPoint, position.z), PredType::NATIVE_FLOAT);
            pointType.insertMember("intensity", HOFFSET(PointCloudPoint, intensity), PredType::NATIVE_FLOAT);
            pointType.insertMember("color_r", HOFFSET(PointCloudPoint, color.r), PredType::NATIVE_FLOAT);
            pointType.insertMember("color_g", HOFFSET(PointCloudPoint, color.g), PredType::NATIVE_FLOAT);
            pointType.insertMember("color_b", HOFFSET(PointCloudPoint, color.b), PredType::NATIVE_FLOAT);

            // Create transformation matrix
            glm::mat4 transform = glm::mat4(1.0f);
            transform = glm::translate(transform, pointCloud.position);
            transform = glm::rotate(transform, glm::radians(pointCloud.rotation.x), glm::vec3(1, 0, 0));
            transform = glm::rotate(transform, glm::radians(pointCloud.rotation.y), glm::vec3(0, 1, 0));
            transform = glm::rotate(transform, glm::radians(pointCloud.rotation.z), glm::vec3(0, 0, 1));
            transform = glm::scale(transform, pointCloud.scale);

            // Apply transformations to points if needed
            std::vector<PointCloudPoint> transformedPoints = pointCloud.points;
            for (auto& point : transformedPoints) {
                glm::vec4 transformedPos = transform * glm::vec4(point.position, 1.0f);
                point.position = glm::vec3(transformedPos);
            }

            // Create dataspace
            hsize_t dims[1] = { transformedPoints.size() };
            DataSpace dataspace(1, dims);
            
            // Create dataset
            DataSet dataset = file.createDataSet("points", pointType, dataspace);
            
            // Write data
            dataset.write(transformedPoints.data(), pointType);
            
            // Add metadata attributes
            DataSpace scalarSpace(H5S_SCALAR);
            
            // Add point count attribute
            Attribute pointCountAttr = dataset.createAttribute("point_count", PredType::NATIVE_HSIZE, scalarSpace);
            hsize_t pointCount = transformedPoints.size();
            pointCountAttr.write(PredType::NATIVE_HSIZE, &pointCount);
            
            // Add name attribute
            StrType stringType(PredType::C_S1, pointCloud.name.length() + 1);
            Attribute nameAttr = dataset.createAttribute("name", stringType, scalarSpace);
            nameAttr.write(stringType, pointCloud.name.c_str());
            
            // Add creation timestamp
            auto now = std::chrono::system_clock::now();
            std::time_t time = std::chrono::system_clock::to_time_t(now);
            
            char timeBuffer[26] = {};
            struct tm tmBuf = {};
            localtime_s(&tmBuf, &time);
            std::strftime(timeBuffer, sizeof(timeBuffer), "%c", &tmBuf);
            std::string timeStr(timeBuffer);
            
            StrType timeStringType(PredType::C_S1, timeStr.length() + 1);
            Attribute timeAttr = dataset.createAttribute("created", timeStringType, scalarSpace);
            timeAttr.write(timeStringType, timeStr.c_str());

            file.close();
            
            std::cout << "Successfully exported " << transformedPoints.size() 
                     << " points to HDF5 file: " << filePath << std::endl;
            return true;

        } catch (const H5::Exception& e) {
            std::cerr << "HDF5 error exporting point cloud: " << e.getDetailMsg() << std::endl;
            return false;
        } catch (const std::exception& e) {
            std::cerr << "Error exporting HDF5 point cloud: " << e.what() << std::endl;
            return false;
        }
    }

    // -------------------------------------------------------------------------
    // LAS / LAZ loader  (uses LASzip – handles all LAS 1.0-1.4 and LAZ)
    // Streaming version: points are quantised and uploaded to GPU one batch at
    // a time.  Peak CPU RAM ≈ one batch (~280 KB) + one tiny sample buffer.
    // -------------------------------------------------------------------------
    PointCloud PointCloudLoader::loadFromLAS(const std::string& filePath, size_t downsampleFactor,
                                              const glm::dvec3* globalCenter)
    {
        PointCloud pointCloud;
        pointCloud.name     = std::filesystem::path(filePath).stem().string();
        pointCloud.position = glm::vec3(0.0f);
        pointCloud.rotation = glm::vec3(-90.0f, 0.0f, 0.0f);
        pointCloud.scale    = glm::vec3(0.1f);

        // ── Helper: open a LASzip reader ─────────────────────────────────────
        auto openReader = [&](laszip_POINTER& r, laszip_BOOL& compressed) -> bool {
            if (laszip_create(&r)) {
                std::cerr << "[LAS] laszip_create failed\n";
                return false;
            }
            if (laszip_open_reader(r, filePath.c_str(), &compressed)) {
                laszip_CHAR* msg = nullptr;
                laszip_get_error(r, &msg);
                std::cerr << "[LAS] Cannot open '" << filePath << "': "
                          << (msg ? msg : "unknown") << "\n";
                laszip_destroy(r);
                r = nullptr;
                return false;
            }
            return true;
        };

        // ── Pass 0: open and read header ─────────────────────────────────────
        laszip_POINTER reader      = nullptr;
        laszip_BOOL    is_compressed = 0;
        if (!openReader(reader, is_compressed)) return pointCloud;

        laszip_header_struct* hdr = nullptr;
        laszip_get_header_pointer(reader, &hdr);

        const double sx = hdr->x_scale_factor, sy = hdr->y_scale_factor, sz = hdr->z_scale_factor;
        const double ox = hdr->x_offset,       oy = hdr->y_offset,       oz = hdr->z_offset;

        const laszip_U8 fmt      = hdr->point_data_format;
        const bool      hasColor = (fmt == 2 || fmt == 3 || fmt == 5 ||
                                    fmt == 7 || fmt == 8 || fmt == 10);

        const int64_t nPoints =
            (hdr->version_minor >= 4 && hdr->extended_number_of_point_records > 0)
                ? static_cast<int64_t>(hdr->extended_number_of_point_records)
                : static_cast<int64_t>(hdr->number_of_point_records);

        // Subtract a centre point to keep float coordinates near origin.
        // When loading multiple files as one scene, globalCenter is the shared
        // centre across all files so they stay correctly positioned relative to
        // each other.  For a single-file load it falls back to this file's own
        // bounding-box midpoint.
        const double cx = globalCenter ? globalCenter->x : (hdr->min_x + hdr->max_x) * 0.5;
        const double cy = globalCenter ? globalCenter->y : (hdr->min_y + hdr->max_y) * 0.5;
        const double cz = globalCenter ? globalCenter->z : (hdr->min_z + hdr->max_z) * 0.5;

        std::cout << "[LAS] " << (is_compressed ? "LAZ" : "LAS")
                  << " v1." << static_cast<int>(hdr->version_minor)
                  << "  fmt=" << static_cast<int>(fmt)
                  << "  nPoints=" << nPoints
                  << "  hasColor=" << hasColor << "\n"
                  << "[LAS] Bounds X[" << hdr->min_x << ".." << hdr->max_x << "]"
                  << " Y[" << hdr->min_y << ".." << hdr->max_y << "]"
                  << " Z[" << hdr->min_z << ".." << hdr->max_z << "]\n"
                  << "[LAS] Centre (" << cx << "," << cy << "," << cz
                  << ") – subtracted to bring cloud to origin\n";

        // ── Pass 1 (colour only): sample up to 2000 points to decide whether
        //    RGB channels are 8-bit (0-255) or 16-bit (0-65535).
        //    This is a tiny seek, not a full re-read.
        float colorScale = 255.0f;
        if (hasColor) {
            laszip_point_struct* lp = nullptr;
            laszip_get_point_pointer(reader, &lp);
            const int64_t sampleLimit = std::min((int64_t)2000, nPoints);
            laszip_U16 sampleMax = 0;
            for (int64_t i = 0; i < sampleLimit; i++) {
                if (laszip_read_point(reader)) break;
                sampleMax = std::max({ sampleMax, lp->rgb[0], lp->rgb[1], lp->rgb[2] });
                if (sampleMax > 255) break; // confirmed 16-bit early
            }
            colorScale = (sampleMax > 255) ? 65535.0f : 255.0f;
            std::cout << "[LAS] Colour scale detected: " << colorScale
                      << " (sample max=" << sampleMax << ")\n";
        }

        // Close and reopen so we read from point 0 again
        laszip_close_reader(reader);
        laszip_destroy(reader);
        reader = nullptr;
        if (!openReader(reader, is_compressed)) return pointCloud;
        laszip_get_header_pointer(reader, &hdr); // refresh pointer after reopen

        // ── Pre-allocate SSBOs using header point count ──────────────────────
        const int64_t expectedPts = (downsampleFactor > 1)
                                  ? (nPoints / static_cast<int64_t>(downsampleFactor) + 1)
                                  : nPoints;
        allocateComputeSSBOs(pointCloud, static_cast<size_t>(expectedPts));

        // ── Pass 2: stream-read → batch → upload ─────────────────────────────
        laszip_point_struct* lp = nullptr;
        laszip_get_point_pointer(reader, &lp);

        std::vector<PointCloudPoint> batchBuf;
        batchBuf.reserve(PointCloud::kComputeBatchSize);

        int    batchIndex  = 0;
        size_t totalPoints = 0;

        auto flushBatch = [&]() {
            if (batchBuf.empty()) return;
            uploadComputeBatch(pointCloud,
                               batchBuf.data(),
                               static_cast<int>(batchBuf.size()),
                               batchIndex,
                               static_cast<int>(totalPoints));
            totalPoints += batchBuf.size();
            batchIndex++;
            batchBuf.clear();

            if (batchIndex % 2000 == 0)
                std::cout << "[LAS] " << totalPoints << " points uploaded...\n";
        };

        for (int64_t i = 0; i < nPoints; ++i) {
            if (laszip_read_point(reader)) {
                std::cerr << "[LAS] Read error at point " << i << "\n";
                break;
            }

            if (downsampleFactor > 1 && (static_cast<size_t>(i) % downsampleFactor) != 0)
                continue;

            PointCloudPoint pt;
            pt.position.x = static_cast<float>(lp->X * sx + ox - cx);
            pt.position.y = static_cast<float>(lp->Y * sy + oy - cy);
            pt.position.z = static_cast<float>(lp->Z * sz + oz - cz);
            pt.intensity  = lp->intensity / 65535.0f;

            if (hasColor) {
                pt.color.r = lp->rgb[0] / colorScale;
                pt.color.g = lp->rgb[1] / colorScale;
                pt.color.b = lp->rgb[2] / colorScale;
            } else {
                pt.color = glm::vec3(pt.intensity);
            }

            batchBuf.push_back(pt);
            if (static_cast<int>(batchBuf.size()) == PointCloud::kComputeBatchSize)
                flushBatch();
        }

        flushBatch(); // Upload last partial batch

        // Save bounds BEFORE closing/destroying the reader – hdr is an internal
        // pointer inside the laszip reader and becomes dangling after destroy.
        const glm::vec3 lasMin(
            static_cast<float>(hdr->min_x - cx),
            static_cast<float>(hdr->min_y - cy),
            static_cast<float>(hdr->min_z - cz));
        const glm::vec3 lasMax(
            static_cast<float>(hdr->max_x - cx),
            static_cast<float>(hdr->max_y - cy),
            static_cast<float>(hdr->max_z - cz));

        laszip_close_reader(reader);
        laszip_destroy(reader);
        reader = nullptr; // prevent any accidental re-use

        // Trim if downsampling caused us to use fewer batches than allocated
        if (totalPoints < static_cast<size_t>(expectedPts) * 9 / 10)
            trimComputeSSBOs(pointCloud, totalPoints);

        pointCloud.numBatches      = static_cast<uint32_t>(batchIndex);
        pointCloud.totalPointCount = static_cast<uint32_t>(totalPoints);
        pointCloud.boundsMin       = lasMin;
        pointCloud.boundsMax       = lasMax;

        std::cout << "[LAS] Streamed " << totalPoints << " points into "
                  << batchIndex << " compute batches (no octree, no CPU copy)\n";
        return pointCloud;
    }

    // -------------------------------------------------------------------------
    // Multi-file LAS/LAZ loader
    // Reads all file headers first to compute a single shared bounding-box
    // centre, then loads each file with that centre so the tiles stay
    // correctly positioned relative to each other.
    // -------------------------------------------------------------------------
    std::vector<PointCloud> PointCloudLoader::loadFromLASMultiple(
        const std::vector<std::string>& filePaths, size_t downsampleFactor)
    {
        if (filePaths.empty()) return {};

        // Pass 1: scan every header to build the global bounding box
        double gMinX =  DBL_MAX, gMinY =  DBL_MAX, gMinZ =  DBL_MAX;
        double gMaxX = -DBL_MAX, gMaxY = -DBL_MAX, gMaxZ = -DBL_MAX;
        int    validHeaders = 0;

        for (const auto& path : filePaths) {
            laszip_POINTER r = nullptr;
            laszip_BOOL    compressed = 0;
            if (laszip_create(&r)) continue;
            if (laszip_open_reader(r, path.c_str(), &compressed)) {
                laszip_destroy(r); continue;
            }
            laszip_header_struct* hdr = nullptr;
            laszip_get_header_pointer(r, &hdr);

            gMinX = std::min(gMinX, hdr->min_x);  gMaxX = std::max(gMaxX, hdr->max_x);
            gMinY = std::min(gMinY, hdr->min_y);  gMaxY = std::max(gMaxY, hdr->max_y);
            gMinZ = std::min(gMinZ, hdr->min_z);  gMaxZ = std::max(gMaxZ, hdr->max_z);
            ++validHeaders;

            laszip_close_reader(r);
            laszip_destroy(r);
        }

        if (validHeaders == 0) {
            std::cerr << "[LAS-Multi] No valid headers found\n";
            return {};
        }

        const glm::dvec3 globalCenter((gMinX + gMaxX) * 0.5,
                                       (gMinY + gMaxY) * 0.5,
                                       (gMinZ + gMaxZ) * 0.5);

        std::cout << "[LAS-Multi] " << filePaths.size() << " file(s), "
                  << validHeaders << " valid\n"
                  << "[LAS-Multi] Global bounds X[" << gMinX << ".." << gMaxX << "]"
                  << " Y[" << gMinY << ".." << gMaxY << "]"
                  << " Z[" << gMinZ << ".." << gMaxZ << "]\n"
                  << "[LAS-Multi] Shared centre ("
                  << globalCenter.x << "," << globalCenter.y << "," << globalCenter.z << ")\n";

        // Pass 2: load each file using the shared global centre
        std::vector<PointCloud> result;
        result.reserve(filePaths.size());
        for (const auto& path : filePaths) {
            PointCloud pc = loadFromLAS(path, downsampleFactor, &globalCenter);
            if (pc.isLoaded()) {
                pc.filePath = path;
                result.push_back(std::move(pc));
            } else {
                std::cerr << "[LAS-Multi] Failed to load: " << path << "\n";
            }
        }
        return result;
    }

} // namespace Engine