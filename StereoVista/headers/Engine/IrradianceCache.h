#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <vector>

namespace Engine {

// Irradiance cache entry structure (GPU layout)
struct IrradianceCacheEntry {
  glm::vec3 position;     // World-space position
  float harmonicMeanDist; // R₀ value for Ward weighting
  glm::vec3 normal;       // Surface normal
  float padding1;
  glm::vec3 irradiance; // Stored RGB irradiance
  float padding2;
};

// Grid cell structure for spatial lookup
struct GridCell {
  uint32_t entryStart; // Index into indirection list
  uint32_t entryCount; // Number of entries in this cell
};

// Main irradiance cache manager class
class IrradianceCache {
public:
  IrradianceCache();
  ~IrradianceCache();

  // Initialize cache with grid resolution and max entries
  void initialize(const glm::ivec3 &gridRes, uint32_t maxEntries);

  // Clear all cache entries
  void clear();

  // Setup scene bounds for grid mapping
  void setSceneBounds(const glm::vec3 &minBounds, const glm::vec3 &maxBounds);

  // Bind buffers for compute/fragment shaders
  void bindBuffers();

  // Unbind buffers
  void unbindBuffers();

  // Get cache statistics
  uint32_t getEntryCount() const { return m_entryCount; }
  uint32_t getMaxEntries() const { return m_maxEntries; }
  glm::ivec3 getGridResolution() const { return m_gridResolution; }

  // Check if initialized
  bool isInitialized() const { return m_initialized; }

  // Get buffer handles for debugging
  GLuint getCacheBufferSSBO() const { return m_cacheBufferSSBO; }
  GLuint getGridCellsSSBO() const { return m_gridCellBufferSSBO; }
  GLuint getIndirectionSSBO() const { return m_indirectionBufferSSBO; }

private:
  // OpenGL buffer objects
  GLuint m_cacheBufferSSBO;       // Main cache entries (binding = 3)
  GLuint m_gridCellBufferSSBO;    // Grid cells (binding = 4)
  GLuint m_indirectionBufferSSBO; // Indirection list (binding = 5)

  // Cache parameters
  glm::ivec3 m_gridResolution;
  glm::vec3 m_gridMin;
  glm::vec3 m_gridMax;
  uint32_t m_maxEntries;
  uint32_t m_entryCount;

  bool m_initialized;

  // Helper functions
  void createBuffers();
  void destroyBuffers();
  uint32_t getGridCellCount() const;
};

} // namespace Engine
