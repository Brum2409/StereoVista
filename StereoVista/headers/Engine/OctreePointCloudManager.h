#pragma once
#include "Data.h"
#include "../Utils/octree.h"
#include <hdf5/H5Cpp.h>
#include <filesystem>
#include <chrono>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <future>
#include <atomic>

namespace Engine {

    class OctreePointCloudManager {
    public:
        static void buildOctree(PointCloud& pointCloud);
        static void updateLOD(PointCloud& pointCloud, const glm::vec3& cameraPosition);
        static void renderVisible(PointCloud& pointCloud, const glm::vec3& cameraPosition);
        
        // Memory management
        static void ensureMemoryLimit(PointCloud& pointCloud);
        static void unloadDistantNodes(PointCloud& pointCloud, const glm::vec3& cameraPosition);
        static size_t getMemoryUsage(const PointCloud& pointCloud);
        
        // Disk storage
        static void saveToDisk(PointCloudOctreeNode* node, const std::string& cacheDir);
        static void loadFromDisk(PointCloudOctreeNode* node, const std::string& cacheDir);
        static void createCacheDirectory(const std::string& cacheDir);
        
        // Async loading system
        static void initializeAsyncSystem();
        static void shutdownAsyncSystem();
        static void requestAsyncLoad(PointCloudOctreeNode* node, const std::string& cacheDirectory);
        static void processCompletedLoads();
        
        // Visualization
        static void generateOctreeVisualization(PointCloud& pointCloud, int depth);
        
    private:
        struct BuildContext {
            size_t nextNodeId;
            std::string cacheDirectory;
            size_t maxPointsPerNode;
            int maxDepth;
        };
        
        // Async loading task structure
        struct LoadingTask {
            PointCloudOctreeNode* node = nullptr;
            std::string cacheDirectory;
            std::promise<bool> promise;
        };
        
        static void buildOctreeRecursive(
            PointCloudOctreeNode* node,
            const std::vector<PointCloudPoint>& points,
            const std::vector<size_t>& pointIndices,
            const glm::vec3& center,
            const glm::vec3& bounds,
            int depth,
            BuildContext& context,
            PointCloud& pointCloud
        );
        
        static void generateLODForNode(PointCloudOctreeNode* node);
        static void createVBOsForNode(PointCloudOctreeNode* node);
        
        static float calculateNodeDistance(const PointCloudOctreeNode* node, const glm::vec3& cameraPos);
        static int calculateRequiredLOD(float distance, const float lodDistances[5]);
        
        static void updateNodeRecursive(
            PointCloudOctreeNode* node,
            const glm::vec3& cameraPosition,
            const float lodDistances[5],
            float lodMultiplier,
            const std::string& cacheDirectory
        );
        
        static void renderNodeRecursive(
            PointCloudOctreeNode* node,
            const glm::vec3& cameraPosition,
            const float lodDistances[5],
            float basePointSize
        );
        
        static void renderNodeAtLOD(
            PointCloudOctreeNode* node,
            float distance,
            const float lodDistances[5],
            float basePointSize
        );
        
        static void renderLeafDescendants(
            PointCloudOctreeNode* node,
            float distance,
            const float lodDistances[5],
            float basePointSize
        );
        
        // Disk I/O helpers
        static std::string getNodeFilePath(const std::string& cacheDir, uint64_t nodeId);
        static void saveNodeToHDF5(const PointCloudOctreeNode* node, const std::string& filePath);
        static void loadNodeFromHDF5(PointCloudOctreeNode* node, const std::string& filePath);
        
        // Memory management helpers
        static void markNodeAccessed(PointCloudOctreeNode* node);
        static void collectMemoryUsage(PointCloudOctreeNode* node, size_t& totalMemory);
        static void collectLoadedNodes(PointCloudOctreeNode* node, 
                                     std::vector<std::pair<std::chrono::steady_clock::time_point, PointCloudOctreeNode*>>& nodesByAge);
        static void unloadOldestNodes(PointCloud& pointCloud, size_t targetMemoryMB);
        
        // Visualization helpers
        static void generateOctreeVisualizationRecursive(PointCloudOctreeNode* node, int targetDepth, 
                                                       int currentDepth, std::vector<glm::vec3>& vertices);
        
        // Async loading worker function
        static void workerThreadFunction();
        
        // Static members for async loading system
        static std::vector<std::thread> s_workerThreads;
        static std::queue<LoadingTask> s_loadingQueue;
        static std::mutex s_queueMutex;
        static std::condition_variable s_queueCondition;
        static std::atomic<bool> s_shutdownRequested;
        static std::vector<std::future<bool>> s_completedTasks;
        static std::mutex s_completedMutex;
        static std::mutex s_hdf5Mutex; // Serialize HDF5 operations for thread safety
    };

    // Utility functions for octree bounds calculation
    struct OctreeBounds {
        static void calculateBounds(const std::vector<PointCloudPoint>& points, 
                                  glm::vec3& min, glm::vec3& max, glm::vec3& center, float& size);
        static void getChildBounds(const glm::vec3& parentCenter, const glm::vec3& parentBounds,
                                 int childIndex, glm::vec3& childCenter, glm::vec3& childBounds);
        static int getChildIndex(const glm::vec3& point, const glm::vec3& center);
    };

}