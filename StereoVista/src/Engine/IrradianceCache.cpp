#include "../../headers/Engine/IrradianceCache.h"
#include <iostream>
#include <cstring>

namespace Engine {

    IrradianceCache::IrradianceCache()
        : m_cacheBufferSSBO(0)
        , m_gridCellBufferSSBO(0)
        , m_indirectionBufferSSBO(0)
        , m_gridResolution(32, 32, 32)
        , m_gridMin(-10.0f, -10.0f, -10.0f)
        , m_gridMax(10.0f, 10.0f, 10.0f)
        , m_maxEntries(10000)
        , m_entryCount(0)
        , m_initialized(false)
    {
    }

    IrradianceCache::~IrradianceCache() {
        destroyBuffers();
    }

    void IrradianceCache::initialize(const glm::ivec3& gridRes, uint32_t maxEntries) {
        if (m_initialized) {
            destroyBuffers();
        }

        m_gridResolution = gridRes;
        m_maxEntries = maxEntries;
        m_entryCount = 0;

        createBuffers();
        m_initialized = true;

        std::cout << "IrradianceCache initialized: Grid="
                  << gridRes.x << "x" << gridRes.y << "x" << gridRes.z
                  << ", MaxEntries=" << maxEntries << std::endl;
    }

    void IrradianceCache::clear() {
        m_entryCount = 0;

        // Clear cache buffer header (entry count)
        if (m_cacheBufferSSBO != 0) {
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_cacheBufferSSBO);
            uint32_t zero = 0;
            glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(uint32_t), &zero);
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
        }

        // Clear grid cells
        if (m_gridCellBufferSSBO != 0) {
            uint32_t cellCount = getGridCellCount();
            std::vector<GridCell> cells(cellCount, {0, 0});
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_gridCellBufferSSBO);
            glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, cellCount * sizeof(GridCell), cells.data());
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
        }

        // CRITICAL: Also clear the indirection buffer to prevent stale index reads
        if (m_indirectionBufferSSBO != 0) {
            const uint32_t maxEntriesPerCell = 16;
            uint32_t indirectionSize = getGridCellCount() * maxEntriesPerCell;
            std::vector<uint32_t> indirectionData(indirectionSize, 0xFFFFFFFF);
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_indirectionBufferSSBO);
            glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, indirectionSize * sizeof(uint32_t), indirectionData.data());
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
        }
    }

    void IrradianceCache::setSceneBounds(const glm::vec3& minBounds, const glm::vec3& maxBounds) {
        m_gridMin = minBounds;
        m_gridMax = maxBounds;
    }

    void IrradianceCache::bindBuffers() {
        if (!m_initialized) return;

        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, m_cacheBufferSSBO);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, m_gridCellBufferSSBO);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, m_indirectionBufferSSBO);
    }

    void IrradianceCache::unbindBuffers() {
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, 0);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, 0);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, 0);
    }

    void IrradianceCache::createBuffers() {
        // Cache buffer layout:
        // - uint32_t entryCount (current number of entries)
        // - uint32_t maxEntries (capacity)
        // - uint32_t padding[2]
        // - IrradianceCacheEntry entries[maxEntries]

        uint32_t cacheBufferSize = 16 + m_maxEntries * sizeof(IrradianceCacheEntry);

        glGenBuffers(1, &m_cacheBufferSSBO);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_cacheBufferSSBO);
        glBufferData(GL_SHADER_STORAGE_BUFFER, cacheBufferSize, nullptr, GL_DYNAMIC_DRAW);

        // Initialize header
        uint32_t header[4] = {0, m_maxEntries, 0, 0};
        glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(header), header);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

        // Grid cell buffer
        uint32_t cellCount = getGridCellCount();
        std::vector<GridCell> cells(cellCount, {0, 0});

        glGenBuffers(1, &m_gridCellBufferSSBO);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_gridCellBufferSSBO);
        glBufferData(GL_SHADER_STORAGE_BUFFER, cellCount * sizeof(GridCell), cells.data(), GL_DYNAMIC_DRAW);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

        // Indirection buffer: pre-allocated fixed layout per grid cell
        // Must match compute shader's allocation: cellIdx * maxEntriesPerCell
        const uint32_t maxEntriesPerCell = 16;  // Must match shader constant!
        uint32_t indirectionSize = cellCount * maxEntriesPerCell;

        // CRITICAL: Initialize with 0xFFFFFFFF (invalid index) to detect uninitialized reads
        std::vector<uint32_t> indirectionData(indirectionSize, 0xFFFFFFFF);

        glGenBuffers(1, &m_indirectionBufferSSBO);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_indirectionBufferSSBO);
        glBufferData(GL_SHADER_STORAGE_BUFFER, indirectionSize * sizeof(uint32_t), indirectionData.data(), GL_DYNAMIC_DRAW);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

        std::cout << "Cache buffers created: "
                  << "CacheBuffer=" << (cacheBufferSize / 1024) << "KB, "
                  << "GridCells=" << cellCount << ", "
                  << "Indirection=" << indirectionSize << std::endl;

        // VALIDATION: Read back header to verify initialization
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_cacheBufferSSBO);
        uint32_t headerReadback[4] = {0};
        glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(headerReadback), headerReadback);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

        std::cout << "Cache buffer header after init:" << std::endl;
        std::cout << "  cacheEntryCount: " << headerReadback[0] << std::endl;
        std::cout << "  maxCacheEntries: " << headerReadback[1] << std::endl;

        if (headerReadback[0] != 0) {
            std::cerr << "ERROR: cacheEntryCount should be 0 after init!" << std::endl;
        }
        if (headerReadback[1] != m_maxEntries) {
            std::cerr << "ERROR: maxCacheEntries mismatch!" << std::endl;
        }
    }

    void IrradianceCache::destroyBuffers() {
        if (m_cacheBufferSSBO != 0) {
            glDeleteBuffers(1, &m_cacheBufferSSBO);
            m_cacheBufferSSBO = 0;
        }
        if (m_gridCellBufferSSBO != 0) {
            glDeleteBuffers(1, &m_gridCellBufferSSBO);
            m_gridCellBufferSSBO = 0;
        }
        if (m_indirectionBufferSSBO != 0) {
            glDeleteBuffers(1, &m_indirectionBufferSSBO);
            m_indirectionBufferSSBO = 0;
        }
        m_initialized = false;
    }

    uint32_t IrradianceCache::getGridCellCount() const {
        return m_gridResolution.x * m_gridResolution.y * m_gridResolution.z;
    }

} // namespace Engine
