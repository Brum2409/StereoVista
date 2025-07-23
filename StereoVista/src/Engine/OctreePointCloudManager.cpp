#include "../../headers/Engine/OctreePointCloudManager.h"
#include <iostream>
#include <algorithm>
#include <random>

namespace Engine {

    // Static member definitions for async loading system
    std::vector<std::thread> OctreePointCloudManager::s_workerThreads;
    std::queue<OctreePointCloudManager::LoadingTask> OctreePointCloudManager::s_loadingQueue;
    std::mutex OctreePointCloudManager::s_queueMutex;
    std::condition_variable OctreePointCloudManager::s_queueCondition;
    std::atomic<bool> OctreePointCloudManager::s_shutdownRequested{false};
    std::vector<std::future<bool>> OctreePointCloudManager::s_completedTasks;
    std::mutex OctreePointCloudManager::s_completedMutex;
    std::mutex OctreePointCloudManager::s_hdf5Mutex;

    void OctreePointCloudManager::initializeAsyncSystem() {
        s_shutdownRequested = false;
        size_t numThreads = std::max(2u, std::thread::hardware_concurrency() / 2); // Use half the available cores
        
        for (size_t i = 0; i < numThreads; ++i) {
            s_workerThreads.emplace_back(workerThreadFunction);
        }
    }

    void OctreePointCloudManager::shutdownAsyncSystem() {
        s_shutdownRequested = true;
        s_queueCondition.notify_all();
        
        for (auto& thread : s_workerThreads) {
            if (thread.joinable()) {
                thread.join();
            }
        }
        
        s_workerThreads.clear();
    }

    void OctreePointCloudManager::workerThreadFunction() {
        while (!s_shutdownRequested) {
            OctreePointCloudManager::LoadingTask task;
            bool hasTask = false;
            
            {
                std::unique_lock<std::mutex> lock(s_queueMutex);
                s_queueCondition.wait(lock, [] { return !s_loadingQueue.empty() || s_shutdownRequested; });
                
                if (s_shutdownRequested && s_loadingQueue.empty()) {
                    break;
                }
                
                if (!s_loadingQueue.empty()) {
                    task = std::move(s_loadingQueue.front());
                    s_loadingQueue.pop();
                    hasTask = true;
                }
            }
            
            if (hasTask && task.node) {
                try {
                    // Perform the actual disk loading
                    loadNodeFromHDF5(task.node, task.node->diskFilePath);
                    task.node->isLoaded = true;
                    task.node->memoryUsage = task.node->points.size() * sizeof(PointCloudPoint);
                    markNodeAccessed(task.node);
                    
                    task.promise.set_value(true);
                } catch (const std::exception& e) {
                    task.promise.set_value(false);
                }
            }
        }
    }

    void OctreePointCloudManager::requestAsyncLoad(PointCloudOctreeNode* node, const std::string& cacheDirectory) {
        if (!node || !node->isOnDisk || node->isLoaded || s_workerThreads.empty()) {
            return;
        }
        
        if (node->diskFilePath.empty() || !std::filesystem::exists(node->diskFilePath)) {
            return;
        }
        
        OctreePointCloudManager::LoadingTask task;
        task.node = node;
        task.cacheDirectory = cacheDirectory;
        
        auto future = task.promise.get_future();
        
        {
            std::lock_guard<std::mutex> lock(s_queueMutex);
            s_loadingQueue.push(std::move(task));
        }
        
        {
            std::lock_guard<std::mutex> lock(s_completedMutex);
            s_completedTasks.push_back(std::move(future));
        }
        
        s_queueCondition.notify_one();
    }

    void OctreePointCloudManager::processCompletedLoads() {
        // Check if async system is initialized
        if (s_workerThreads.empty()) {
            return;
        }
        
        std::lock_guard<std::mutex> lock(s_completedMutex);
        
        auto it = s_completedTasks.begin();
        while (it != s_completedTasks.end()) {
            if (it->wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                bool success = it->get();
                it = s_completedTasks.erase(it);
            } else {
                ++it;
            }
        }
    }

    void OctreePointCloudManager::buildOctree(PointCloud& pointCloud) {
        if (pointCloud.points.empty()) {
            return;
        }

        // Check initial memory usage - if raw points exceed 90% of limit, we need to be careful
        size_t rawPointsMemoryMB = (pointCloud.points.size() * sizeof(PointCloudPoint)) / (1024 * 1024);
        if (rawPointsMemoryMB > pointCloud.chunkCache.maxMemoryMB * 0.9f) {
            // For very large point clouds, we need extremely aggressive memory management
            // Reduce max points per node to create more, smaller chunks
            pointCloud.maxPointsPerNode = std::min(pointCloud.maxPointsPerNode, size_t(1000));
        }

        // Calculate bounds
        OctreeBounds::calculateBounds(pointCloud.points, 
                                    pointCloud.octreeBoundsMin, 
                                    pointCloud.octreeBoundsMax, 
                                    pointCloud.octreeCenter, 
                                    pointCloud.octreeSize);

        // Create cache directory
        createCacheDirectory(pointCloud.chunkCache.cacheDirectory);

        // Initialize build context
        BuildContext context;
        context.nextNodeId = 1;
        context.cacheDirectory = pointCloud.chunkCache.cacheDirectory;
        context.maxPointsPerNode = pointCloud.maxPointsPerNode;
        context.maxDepth = pointCloud.maxOctreeDepth;

        // Create root node
        pointCloud.octreeRoot = std::make_unique<PointCloudOctreeNode>();
        pointCloud.octreeRoot->nodeId = context.nextNodeId++;
        pointCloud.octreeRoot->depth = 0;
        pointCloud.octreeRoot->center = pointCloud.octreeCenter;
        pointCloud.octreeRoot->bounds = glm::vec3(pointCloud.octreeSize * 0.5f);

        // Create point indices vector
        std::vector<size_t> allIndices(pointCloud.points.size());
        std::iota(allIndices.begin(), allIndices.end(), 0);

        // Build octree recursively with memory monitoring
        buildOctreeRecursive(
            pointCloud.octreeRoot.get(),
            pointCloud.points,
            allIndices,
            pointCloud.octreeCenter,
            pointCloud.octreeRoot->bounds,
            0,
            context,
            pointCloud
        );

        // Final memory check and cleanup after build
        ensureMemoryLimit(pointCloud);
        
        // Clear raw points to save memory (they're now in the octree)
        pointCloud.points.clear();
        pointCloud.points.shrink_to_fit();
    }

    void OctreePointCloudManager::buildOctreeRecursive(
        PointCloudOctreeNode* node,
        const std::vector<PointCloudPoint>& points,
        const std::vector<size_t>& pointIndices,
        const glm::vec3& center,
        const glm::vec3& bounds,
        int depth,
        BuildContext& context,
        PointCloud& pointCloud
    ) {
        node->totalPointCount = pointIndices.size();
        
        // Check memory usage before processing this node
        size_t currentMemoryMB = getMemoryUsage(pointCloud) / (1024 * 1024);
        if (currentMemoryMB > pointCloud.chunkCache.maxMemoryMB * 0.8f) { // Use 80% threshold
            // Aggressively unload nodes to free memory
            unloadOldestNodes(pointCloud, pointCloud.chunkCache.maxMemoryMB * 0.5f); // Target 50% usage
        }
        
        // Check if we should create a leaf node
        if (pointIndices.size() <= context.maxPointsPerNode || depth >= context.maxDepth) {
            // Create leaf node
            node->isLeaf = true;
            node->points.reserve(pointIndices.size());
            
            for (size_t idx : pointIndices) {
                node->points.push_back(points[idx]);
            }
            
            // Generate LOD levels for this node
            generateLODForNode(node);
            
            // Calculate memory usage
            node->memoryUsage = node->points.size() * sizeof(PointCloudPoint);
            node->isLoaded = true;
            
            // Save ALL nodes to disk IMMEDIATELY during build
            saveToDisk(node, context.cacheDirectory);
            
            // ALWAYS unload from memory after saving during build to prevent overflow
            if (node->isOnDisk) {
                // Keep the LOD counts but clear the actual point data
                node->points.clear();
                node->points.shrink_to_fit();
                node->isLoaded = false;
                node->memoryUsage = 0;
            }
            
            return;
        }

        // Create internal node - subdivide into 8 children
        node->isLeaf = false;
        std::array<std::vector<size_t>, 8> childIndices;

        // Distribute points to children
        for (size_t idx : pointIndices) {
            int childIndex = OctreeBounds::getChildIndex(points[idx].position, center);
            childIndices[childIndex].push_back(idx);
        }

        // Create children that have points
        for (int i = 0; i < 8; i++) {
            if (!childIndices[i].empty()) {
                glm::vec3 childCenter, childBounds;
                OctreeBounds::getChildBounds(center, bounds, i, childCenter, childBounds);

                node->children[i] = std::make_unique<PointCloudOctreeNode>();
                node->children[i]->nodeId = context.nextNodeId++;
                node->children[i]->depth = depth + 1;
                node->children[i]->center = childCenter;
                node->children[i]->bounds = childBounds;

                buildOctreeRecursive(
                    node->children[i].get(),
                    points,
                    childIndices[i],
                    childCenter,
                    childBounds,
                    depth + 1,
                    context,
                    pointCloud
                );
                
                // Check memory after each child to prevent overflow
                size_t memoryAfterChild = getMemoryUsage(pointCloud) / (1024 * 1024);
                if (memoryAfterChild > pointCloud.chunkCache.maxMemoryMB * 0.9f) { // 90% threshold
                    // Emergency memory cleanup
                    unloadOldestNodes(pointCloud, pointCloud.chunkCache.maxMemoryMB * 0.3f); // Target 30% usage
                }
            }
        }
    }

    void OctreePointCloudManager::generateLODForNode(PointCloudOctreeNode* node) {
        if (node->points.empty()) return;

        const int numLODLevels = 5;
        node->lodPointCounts.resize(numLODLevels);
        
        size_t totalPoints = node->points.size();
        
        // Calculate point density using octree spatial information
        float nodeVolume = (node->bounds.x * 2.0f) * (node->bounds.y * 2.0f) * (node->bounds.z * 2.0f);
        float pointDensity = static_cast<float>(totalPoints) / nodeVolume;
        
        // Use octree depth to understand the spatial resolution
        // Deeper nodes = smaller volumes = potentially higher local density
        float depthFactor = 1.0f + (node->depth * 0.1f);
        float adjustedDensity = pointDensity * depthFactor;
        
        // Density-aware LOD generation
        if (adjustedDensity < 10.0f) {
            // Very low density - minimal reduction to maintain visibility
            const float reductionFactors[] = { 1.0f, 1.0f, 0.9f, 0.8f, 0.7f };
            for (int lod = 0; lod < numLODLevels; lod++) {
                node->lodPointCounts[lod] = std::max(static_cast<size_t>(totalPoints * reductionFactors[lod]), size_t(1));
            }
        } else if (adjustedDensity < 50.0f) {
            // Low density - gentle reduction
            const float reductionFactors[] = { 1.0f, 0.9f, 0.7f, 0.5f, 0.3f };
            for (int lod = 0; lod < numLODLevels; lod++) {
                node->lodPointCounts[lod] = std::max(static_cast<size_t>(totalPoints * reductionFactors[lod]), size_t(1));
            }
        } else if (adjustedDensity < 200.0f) {
            // Medium density - moderate reduction
            const float reductionFactors[] = { 1.0f, 0.7f, 0.4f, 0.2f, 0.08f };
            for (int lod = 0; lod < numLODLevels; lod++) {
                node->lodPointCounts[lod] = std::max(static_cast<size_t>(totalPoints * reductionFactors[lod]), size_t(2));
            }
        } else if (adjustedDensity < 1000.0f) {
            // High density - aggressive reduction (points are packed, can afford to lose many)
            const float reductionFactors[] = { 1.0f, 0.5f, 0.2f, 0.05f, 0.01f };
            for (int lod = 0; lod < numLODLevels; lod++) {
                node->lodPointCounts[lod] = std::max(static_cast<size_t>(totalPoints * reductionFactors[lod]), size_t(3));
            }
        } else {
            // Extremely high density - very aggressive reduction (densely packed cloud)
            const float reductionFactors[] = { 1.0f, 0.3f, 0.08f, 0.015f, 0.003f };
            for (int lod = 0; lod < numLODLevels; lod++) {
                node->lodPointCounts[lod] = std::max(static_cast<size_t>(totalPoints * reductionFactors[lod]), size_t(5));
            }
        }
        
        // Additional safety: ensure very small chunks always keep some points at all LOD levels
        if (totalPoints <= 20) {
            for (int lod = 0; lod < numLODLevels; lod++) {
                node->lodPointCounts[lod] = std::max(node->lodPointCounts[lod], static_cast<size_t>(std::max(1, static_cast<int>(totalPoints * 0.3f))));
            }
        }
    }

    void OctreePointCloudManager::createVBOsForNode(PointCloudOctreeNode* node) {
        if (node->vbosGenerated || node->points.empty()) return;

        const int numLODLevels = 5;
        
        for (int lod = 0; lod < numLODLevels; lod++) {
            size_t pointCount = node->lodPointCounts[lod];
            if (pointCount == 0) continue;

            // Create VBO
            GLuint vbo;
            glGenBuffers(1, &vbo);
            glBindBuffer(GL_ARRAY_BUFFER, vbo);

            // Subsample points for this LOD level
            std::vector<PointCloudPoint> lodPoints;
            if (pointCount >= node->points.size()) {
                lodPoints = node->points;
            } else {
                // Use random sampling for LOD
                std::vector<size_t> indices(node->points.size());
                std::iota(indices.begin(), indices.end(), 0);
                
                std::random_device rd;
                std::mt19937 g(rd());
                std::shuffle(indices.begin(), indices.end(), g);
                
                lodPoints.reserve(pointCount);
                for (size_t i = 0; i < pointCount; i++) {
                    lodPoints.push_back(node->points[indices[i]]);
                }
            }

            // Upload to GPU
            glBufferData(GL_ARRAY_BUFFER, 
                        lodPoints.size() * sizeof(PointCloudPoint), 
                        lodPoints.data(), 
                        GL_STATIC_DRAW);

            node->lodVBOs[lod] = vbo;
        }

        glBindBuffer(GL_ARRAY_BUFFER, 0);
        node->vbosGenerated = true;
    }

    void OctreePointCloudManager::updateLOD(PointCloud& pointCloud, const glm::vec3& cameraPosition) {
        if (!pointCloud.octreeRoot) {
            return;
        }

        // Process any completed async loads first
        processCompletedLoads();
        
        // Update nodes that need to be loaded/unloaded based on camera position
        updateNodeRecursive(
            pointCloud.octreeRoot.get(),
            cameraPosition,
            pointCloud.lodDistances,
            pointCloud.lodMultiplier,
            pointCloud.chunkCache.cacheDirectory
        );

        // Manage memory usage
        ensureMemoryLimit(pointCloud);
    }

    void OctreePointCloudManager::updateNodeRecursive(
        PointCloudOctreeNode* node,
        const glm::vec3& cameraPosition,
        const float lodDistances[5],
        float lodMultiplier,
        const std::string& cacheDirectory
    ) {
        if (!node) return;

        float distance = calculateNodeDistance(node, cameraPosition);
        float adjustedDistance = distance / lodMultiplier;
        
        // Distance culling - don't process nodes that are too far away
        if (adjustedDistance > lodDistances[4] * 2.0f) {
            return;
        }

        // Use the same hierarchical decision logic as rendering
        bool shouldSubdivide = false;
        
        if (!node->isLeaf) {
            // Use same density-aware logic as rendering for consistency
            float baseThreshold = lodDistances[2];
            
            float nodeVolume = (node->bounds.x * 2.0f) * (node->bounds.y * 2.0f) * (node->bounds.z * 2.0f);
            float estimatedDensity = static_cast<float>(node->totalPointCount) / nodeVolume;
            
            float sizeMultiplier = std::max(0.2f, std::min(3.0f, glm::length(node->bounds) / 5.0f));
            
            float densityMultiplier = 1.0f;
            if (estimatedDensity > 500.0f) {
                densityMultiplier = 1.8f;
            } else if (estimatedDensity > 100.0f) {
                densityMultiplier = 1.4f;
            } else if (estimatedDensity < 20.0f) {
                densityMultiplier = 0.6f;
            }
            
            float depthMultiplier = 1.0f + (node->depth * 0.15f);
            
            float subdivisionThreshold = baseThreshold * sizeMultiplier * densityMultiplier * depthMultiplier;
            shouldSubdivide = (adjustedDistance < subdivisionThreshold);
        }
        
        if (shouldSubdivide) {
            // Camera is close - we'll render children, so update them
            for (auto& child : node->children) {
                if (child) {
                    updateNodeRecursive(child.get(), cameraPosition, lodDistances, lodMultiplier, cacheDirectory);
                }
            }
        } else {
            // We'll render at this level - ensure it's loaded
            if (node->totalPointCount > 0) {
                markNodeAccessed(node);
                
                if (!node->isLoaded && node->isOnDisk) {
                    requestAsyncLoad(node, cacheDirectory);
                } else if (node->isLoaded && !node->vbosGenerated) {
                    createVBOsForNode(node);
                }
            }
        }
    }

    float OctreePointCloudManager::calculateNodeDistance(const PointCloudOctreeNode* node, const glm::vec3& cameraPos) {
        // Calculate distance from camera to closest point on the node's bounding box
        glm::vec3 nodeMin = node->center - node->bounds;
        glm::vec3 nodeMax = node->center + node->bounds;
        
        glm::vec3 closest = glm::clamp(cameraPos, nodeMin, nodeMax);
        return glm::length(cameraPos - closest);
    }

    int OctreePointCloudManager::calculateRequiredLOD(float distance, const float lodDistances[5]) {
        for (int i = 0; i < 5; i++) {
            if (distance < lodDistances[i]) {
                return i;
            }
        }
        return 4; // Lowest quality LOD
    }

    void OctreePointCloudManager::renderVisible(PointCloud& pointCloud, const glm::vec3& cameraPosition) {
        if (!pointCloud.octreeRoot) {
            return;
        }

        renderNodeRecursive(
            pointCloud.octreeRoot.get(),
            cameraPosition,
            pointCloud.lodDistances,
            pointCloud.basePointSize
        );
    }

    void OctreePointCloudManager::renderNodeRecursive(
        PointCloudOctreeNode* node,
        const glm::vec3& cameraPosition,
        const float lodDistances[5],
        float basePointSize
    ) {
        if (!node) {
            return;
        }

        float distance = calculateNodeDistance(node, cameraPosition);
        
        // Hierarchical LOD decision: decide whether to render at this level or subdivide
        bool shouldSubdivide = false;
        
        if (!node->isLeaf) {
            // For internal nodes, use density and octree structure to make subdivision decisions
            float baseThreshold = lodDistances[2]; // Middle LOD distance as base
            
            // Calculate estimated density for this internal node based on children
            float nodeVolume = (node->bounds.x * 2.0f) * (node->bounds.y * 2.0f) * (node->bounds.z * 2.0f);
            float estimatedDensity = static_cast<float>(node->totalPointCount) / nodeVolume;
            
            // Size-based factor: larger nodes can be rendered at current level from further away
            float sizeMultiplier = std::max(0.2f, std::min(3.0f, glm::length(node->bounds) / 5.0f));
            
            // Density-based factor: high-density areas benefit more from subdivision
            float densityMultiplier = 1.0f;
            if (estimatedDensity > 500.0f) {
                densityMultiplier = 1.8f; // Subdivide high-density areas more aggressively
            } else if (estimatedDensity > 100.0f) {
                densityMultiplier = 1.4f; // Moderate subdivision for medium density
            } else if (estimatedDensity < 20.0f) {
                densityMultiplier = 0.6f; // Less subdivision for sparse areas
            }
            
            // Depth factor: deeper nodes represent higher detail, need closer approach
            float depthMultiplier = 1.0f + (node->depth * 0.15f);
            
            float subdivisionThreshold = baseThreshold * sizeMultiplier * densityMultiplier * depthMultiplier;
            shouldSubdivide = (distance < subdivisionThreshold);
        }
        
        if (shouldSubdivide) {
            // Camera is close enough - render children for more detail
            for (auto& child : node->children) {
                if (child) {
                    renderNodeRecursive(child.get(), cameraPosition, lodDistances, basePointSize);
                }
            }
        } else {
            // Render at this level with appropriate LOD
            if (node->isLeaf) {
                // Leaf node - render directly if loaded
                if (node->isLoaded && node->vbosGenerated) {
                    renderNodeAtLOD(node, distance, lodDistances, basePointSize);
                }
            } else {
                // Internal node - render all leaf descendants with appropriate LOD
                renderLeafDescendants(node, distance, lodDistances, basePointSize);
            }
        }
    }
    
    void OctreePointCloudManager::renderNodeAtLOD(
        PointCloudOctreeNode* node,
        float distance,
        const float lodDistances[5],
        float basePointSize
    ) {
        // Determine LOD level based on distance - same logic as legacy system
        int lodLevel = 4;  // Start with lowest detail
        for (int i = 0; i < 5; ++i) {
            if (distance < lodDistances[i] * 1.0f) {
                lodLevel = i;
                break;
            }
        }
        
        if (node->lodVBOs[lodLevel] == 0 || node->lodPointCounts[lodLevel] == 0) {
            return;
        }
        
        // Bind and render this LOD level
        glBindBuffer(GL_ARRAY_BUFFER, node->lodVBOs[lodLevel]);
        
        // Set up vertex attributes (position, color, intensity) - matching main.cpp order
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(PointCloudPoint), (void*)0);
        glEnableVertexAttribArray(0);
        
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(PointCloudPoint), (void*)offsetof(PointCloudPoint, color));
        glEnableVertexAttribArray(1);
        
        glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(PointCloudPoint), (void*)offsetof(PointCloudPoint, intensity));
        glEnableVertexAttribArray(2);
        
        // Density-aware point size scaling
        float nodeVolume = (node->bounds.x * 2.0f) * (node->bounds.y * 2.0f) * (node->bounds.z * 2.0f);
        float pointDensity = static_cast<float>(node->points.size()) / nodeVolume;
        
        // Base LOD scaling
        float lodMultiplier = 1.0f + (lodLevel) * 1.2f;
        
        // Density-based scaling: high density = larger points to compensate for aggressive reduction
        float densityMultiplier = 1.0f;
        if (pointDensity > 1000.0f) {
            densityMultiplier = 1.8f; // Much larger points for very dense areas
        } else if (pointDensity > 200.0f) {
            densityMultiplier = 1.4f; // Larger points for dense areas
        } else if (pointDensity < 20.0f) {
            densityMultiplier = 0.8f; // Smaller points for sparse areas (they're already preserved)
        }
        
        float adjustedPointSize = basePointSize * lodMultiplier * densityMultiplier;
        
        // Reasonable limits
        adjustedPointSize = std::min(adjustedPointSize, 25.0f);
        adjustedPointSize = std::max(adjustedPointSize, 1.0f);
        
        glPointSize(adjustedPointSize);
        
        // Render points
        glDrawArrays(GL_POINTS, 0, static_cast<GLsizei>(node->lodPointCounts[lodLevel]));
    }
    
    void OctreePointCloudManager::renderLeafDescendants(
        PointCloudOctreeNode* node,
        float distance,
        const float lodDistances[5],
        float basePointSize
    ) {
        if (!node) return;
        
        if (node->isLeaf) {
            // Found a leaf - render it if loaded
            if (node->isLoaded && node->vbosGenerated) {
                renderNodeAtLOD(node, distance, lodDistances, basePointSize);
            }
        } else {
            // Internal node - recurse to children
            for (auto& child : node->children) {
                if (child) {
                    renderLeafDescendants(child.get(), distance, lodDistances, basePointSize);
                }
            }
        }
    }

    void OctreePointCloudManager::ensureMemoryLimit(PointCloud& pointCloud) {
        size_t currentMemoryMB = getMemoryUsage(pointCloud) / (1024 * 1024);
        
        if (currentMemoryMB > pointCloud.chunkCache.maxMemoryMB) {
            std::cout << "Memory limit exceeded (" << currentMemoryMB << "MB > " << pointCloud.chunkCache.maxMemoryMB << "MB), unloading nodes..." << std::endl;
            unloadOldestNodes(pointCloud, pointCloud.chunkCache.maxMemoryMB * 0.8f); // Target 80% of limit
        }
    }

    size_t OctreePointCloudManager::getMemoryUsage(const PointCloud& pointCloud) {
        size_t totalMemory = 0;
        if (pointCloud.octreeRoot) {
            collectMemoryUsage(pointCloud.octreeRoot.get(), totalMemory);
        }
        return totalMemory;
    }

    void OctreePointCloudManager::collectMemoryUsage(PointCloudOctreeNode* node, size_t& totalMemory) {
        if (!node) return;
        
        if (node->isLoaded) {
            totalMemory += node->memoryUsage;
        }
        
        for (auto& child : node->children) {
            if (child) {
                collectMemoryUsage(child.get(), totalMemory);
            }
        }
    }

    void OctreePointCloudManager::collectLoadedNodes(PointCloudOctreeNode* node, 
                                                   std::vector<std::pair<std::chrono::steady_clock::time_point, PointCloudOctreeNode*>>& nodesByAge) {
        if (!node) return;
        
        if (node->isLoaded) {
            nodesByAge.emplace_back(node->lastAccessed, node);
        }
        
        for (auto& child : node->children) {
            if (child) {
                collectLoadedNodes(child.get(), nodesByAge);
            }
        }
    }

    void OctreePointCloudManager::markNodeAccessed(PointCloudOctreeNode* node) {
        node->lastAccessed = std::chrono::steady_clock::now();
    }

    void OctreePointCloudManager::saveToDisk(PointCloudOctreeNode* node, const std::string& cacheDir) {
        if (node->points.empty()) return;

        try {
            std::string filePath = getNodeFilePath(cacheDir, node->nodeId);
            saveNodeToHDF5(node, filePath);
            
            node->isOnDisk = true;
            node->diskFilePath = filePath;
            
            // Can unload from memory after saving to disk
            // node->points.clear(); // Uncomment to free memory immediately
            
        } catch (const std::exception& e) {
            std::cerr << "Failed to save node " << node->nodeId << " to disk: " << e.what() << std::endl;
        }
    }

    void OctreePointCloudManager::loadFromDisk(PointCloudOctreeNode* node, const std::string& cacheDir) {
        std::cout << "[DEBUG] loadFromDisk() called for node " << node->nodeId 
                  << ", isOnDisk: " << (node->isOnDisk ? "true" : "false")
                  << ", isLoaded: " << (node->isLoaded ? "true" : "false") << std::endl;
                  
        if (!node->isOnDisk || node->isLoaded) {
            std::cout << "[DEBUG] Node " << node->nodeId << " not on disk or already loaded, skipping" << std::endl;
            return;
        }

        try {
            std::cout << "[DEBUG] Loading node " << node->nodeId << " from file: " << node->diskFilePath << std::endl;
            loadNodeFromHDF5(node, node->diskFilePath);
            node->isLoaded = true;
            markNodeAccessed(node);
            std::cout << "[DEBUG] Successfully loaded node " << node->nodeId << " from disk with " << node->points.size() << " points" << std::endl;
            
        } catch (const std::exception& e) {
            std::cerr << "[ERROR] Failed to load node " << node->nodeId << " from disk: " << e.what() << std::endl;
        }
    }

    void OctreePointCloudManager::createCacheDirectory(const std::string& cacheDir) {
        try {
            std::filesystem::create_directories(cacheDir);
        } catch (const std::exception& e) {
            std::cerr << "Failed to create cache directory: " << e.what() << std::endl;
        }
    }

    std::string OctreePointCloudManager::getNodeFilePath(const std::string& cacheDir, uint64_t nodeId) {
        return cacheDir + "/node_" + std::to_string(nodeId) + ".h5";
    }

    void OctreePointCloudManager::saveNodeToHDF5(const PointCloudOctreeNode* node, const std::string& filePath) {
        std::lock_guard<std::mutex> hdf5Lock(s_hdf5Mutex);
        H5::H5File file(filePath, H5F_ACC_TRUNC);
        
        // Create compound datatype for PointCloudPoint
        H5::CompType pointType(sizeof(PointCloudPoint));
        pointType.insertMember("position_x", HOFFSET(PointCloudPoint, position.x), H5::PredType::NATIVE_FLOAT);
        pointType.insertMember("position_y", HOFFSET(PointCloudPoint, position.y), H5::PredType::NATIVE_FLOAT);
        pointType.insertMember("position_z", HOFFSET(PointCloudPoint, position.z), H5::PredType::NATIVE_FLOAT);
        pointType.insertMember("intensity", HOFFSET(PointCloudPoint, intensity), H5::PredType::NATIVE_FLOAT);
        pointType.insertMember("color_r", HOFFSET(PointCloudPoint, color.r), H5::PredType::NATIVE_FLOAT);
        pointType.insertMember("color_g", HOFFSET(PointCloudPoint, color.g), H5::PredType::NATIVE_FLOAT);
        pointType.insertMember("color_b", HOFFSET(PointCloudPoint, color.b), H5::PredType::NATIVE_FLOAT);
        
        // Create dataspace
        hsize_t dims[1] = { node->points.size() };
        H5::DataSpace dataspace(1, dims);
        
        // Create dataset and write points
        H5::DataSet dataset = file.createDataSet("points", pointType, dataspace);
        dataset.write(node->points.data(), pointType);
        
        file.close();
    }

    void OctreePointCloudManager::loadNodeFromHDF5(PointCloudOctreeNode* node, const std::string& filePath) {
        if (!node || filePath.empty() || !std::filesystem::exists(filePath)) {
            return;
        }
        
        try {
            std::lock_guard<std::mutex> hdf5Lock(s_hdf5Mutex);
            H5::H5File file(filePath, H5F_ACC_RDONLY);
            
            // Open dataset
            H5::DataSet dataset = file.openDataSet("points");
            H5::DataSpace dataspace = dataset.getSpace();
            
            // Get dimensions
            hsize_t dims[1];
            dataspace.getSimpleExtentDims(dims, NULL);
            
            // Resize points vector and read data
            node->points.resize(dims[0]);
            
            // Create compound datatype
            H5::CompType pointType(sizeof(PointCloudPoint));
            pointType.insertMember("position_x", HOFFSET(PointCloudPoint, position.x), H5::PredType::NATIVE_FLOAT);
            pointType.insertMember("position_y", HOFFSET(PointCloudPoint, position.y), H5::PredType::NATIVE_FLOAT);
            pointType.insertMember("position_z", HOFFSET(PointCloudPoint, position.z), H5::PredType::NATIVE_FLOAT);
            pointType.insertMember("intensity", HOFFSET(PointCloudPoint, intensity), H5::PredType::NATIVE_FLOAT);
            pointType.insertMember("color_r", HOFFSET(PointCloudPoint, color.r), H5::PredType::NATIVE_FLOAT);
            pointType.insertMember("color_g", HOFFSET(PointCloudPoint, color.g), H5::PredType::NATIVE_FLOAT);
            pointType.insertMember("color_b", HOFFSET(PointCloudPoint, color.b), H5::PredType::NATIVE_FLOAT);
            
            dataset.read(node->points.data(), pointType);
            
            // Update memory usage
            node->memoryUsage = node->points.size() * sizeof(PointCloudPoint);
            
            file.close();
            
        } catch (const std::exception& e) {
            // Silent failure - just don't load the node
        }
    }

    void OctreePointCloudManager::unloadOldestNodes(PointCloud& pointCloud, size_t targetMemoryMB) {
        size_t targetMemoryBytes = targetMemoryMB * 1024 * 1024;
        size_t currentMemory = getMemoryUsage(pointCloud);
        
        if (!pointCloud.octreeRoot || currentMemory <= targetMemoryBytes) {
            return;
        }
        
        std::cout << "Unloading nodes to reduce memory from " << (currentMemory / (1024 * 1024)) 
                  << "MB to target " << targetMemoryMB << "MB" << std::endl;
        
        // Collect all loaded leaf nodes with their access times
        std::vector<std::pair<std::chrono::steady_clock::time_point, PointCloudOctreeNode*>> nodesByAge;
        collectLoadedNodes(pointCloud.octreeRoot.get(), nodesByAge);
        
        // Sort by access time (oldest first)
        std::sort(nodesByAge.begin(), nodesByAge.end());
        
        // Unload oldest nodes until we reach target memory
        for (auto& [accessTime, node] : nodesByAge) {
            if (getMemoryUsage(pointCloud) <= targetMemoryBytes) {
                break;
            }
            
            if (node->isLoaded) {
                // Save to disk first if not already saved
                if (!node->isOnDisk) {
                    saveToDisk(node, pointCloud.chunkCache.cacheDirectory);
                }
                
                // Clean up VBOs
                node->cleanup();
                
                // Unload from memory
                node->points.clear();
                node->points.shrink_to_fit();
                node->isLoaded = false;
                node->vbosGenerated = false;
                node->memoryUsage = 0;
                
                std::cout << "Unloaded node " << node->nodeId << " from memory" << std::endl;
            }
        }
        
        std::cout << "Memory after cleanup: " << (getMemoryUsage(pointCloud) / (1024 * 1024)) << "MB" << std::endl;
    }

    // OctreeBounds utility functions
    void OctreeBounds::calculateBounds(const std::vector<PointCloudPoint>& points, 
                                     glm::vec3& min, glm::vec3& max, glm::vec3& center, float& size) {
        if (points.empty()) {
            min = max = center = glm::vec3(0.0f);
            size = 1.0f;
            return;
        }

        min = max = points[0].position;
        
        for (const auto& point : points) {
            min = glm::min(min, point.position);
            max = glm::max(max, point.position);
        }

        center = (min + max) * 0.5f;
        glm::vec3 extent = max - min;
        size = std::max({extent.x, extent.y, extent.z});
        
        // Add some padding
        size *= 1.1f;
    }

    void OctreeBounds::getChildBounds(const glm::vec3& parentCenter, const glm::vec3& parentBounds,
                                    int childIndex, glm::vec3& childCenter, glm::vec3& childBounds) {
        childBounds = parentBounds * 0.5f;
        
        childCenter = parentCenter;
        if (childIndex & 1) childCenter.x += childBounds.x;
        else childCenter.x -= childBounds.x;
        
        if (childIndex & 2) childCenter.y += childBounds.y;
        else childCenter.y -= childBounds.y;
        
        if (childIndex & 4) childCenter.z += childBounds.z;
        else childCenter.z -= childBounds.z;
    }

    int OctreeBounds::getChildIndex(const glm::vec3& point, const glm::vec3& center) {
        int index = 0;
        if (point.x >= center.x) index |= 1;
        if (point.y >= center.y) index |= 2;
        if (point.z >= center.z) index |= 4;
        return index;
    }

    void OctreePointCloudManager::generateOctreeVisualization(PointCloud& pointCloud, int depth) {
        if (!pointCloud.octreeRoot) return;

        pointCloud.chunkOutlineVertices.clear();
        generateOctreeVisualizationRecursive(pointCloud.octreeRoot.get(), depth, 0, pointCloud.chunkOutlineVertices);

        // Create and bind VAO and VBO for octree visualization
        if (pointCloud.chunkOutlineVAO == 0) {
            glGenVertexArrays(1, &pointCloud.chunkOutlineVAO);
        }
        if (pointCloud.chunkOutlineVBO == 0) {
            glGenBuffers(1, &pointCloud.chunkOutlineVBO);
        }

        glBindVertexArray(pointCloud.chunkOutlineVAO);
        glBindBuffer(GL_ARRAY_BUFFER, pointCloud.chunkOutlineVBO);
        glBufferData(GL_ARRAY_BUFFER, pointCloud.chunkOutlineVertices.size() * sizeof(glm::vec3), 
                     pointCloud.chunkOutlineVertices.data(), GL_STATIC_DRAW);

        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void*)0);
        glEnableVertexAttribArray(0);

        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);
    }

    void OctreePointCloudManager::generateOctreeVisualizationRecursive(
        PointCloudOctreeNode* node, int targetDepth, int currentDepth, 
        std::vector<glm::vec3>& vertices) {
        
        if (!node || currentDepth > targetDepth) return;

        if (currentDepth == targetDepth || node->isLeaf) {
            // Generate bounding box lines for this node
            glm::vec3 minBound = node->center - node->bounds;
            glm::vec3 maxBound = node->center + node->bounds;

            // Front face
            vertices.push_back(minBound);
            vertices.push_back(glm::vec3(maxBound.x, minBound.y, minBound.z));

            vertices.push_back(glm::vec3(maxBound.x, minBound.y, minBound.z));
            vertices.push_back(glm::vec3(maxBound.x, maxBound.y, minBound.z));

            vertices.push_back(glm::vec3(maxBound.x, maxBound.y, minBound.z));
            vertices.push_back(glm::vec3(minBound.x, maxBound.y, minBound.z));

            vertices.push_back(glm::vec3(minBound.x, maxBound.y, minBound.z));
            vertices.push_back(minBound);

            // Back face
            vertices.push_back(glm::vec3(minBound.x, minBound.y, maxBound.z));
            vertices.push_back(glm::vec3(maxBound.x, minBound.y, maxBound.z));

            vertices.push_back(glm::vec3(maxBound.x, minBound.y, maxBound.z));
            vertices.push_back(maxBound);

            vertices.push_back(maxBound);
            vertices.push_back(glm::vec3(minBound.x, maxBound.y, maxBound.z));

            vertices.push_back(glm::vec3(minBound.x, maxBound.y, maxBound.z));
            vertices.push_back(glm::vec3(minBound.x, minBound.y, maxBound.z));

            // Connecting lines
            vertices.push_back(minBound);
            vertices.push_back(glm::vec3(minBound.x, minBound.y, maxBound.z));

            vertices.push_back(glm::vec3(maxBound.x, minBound.y, minBound.z));
            vertices.push_back(glm::vec3(maxBound.x, minBound.y, maxBound.z));

            vertices.push_back(glm::vec3(maxBound.x, maxBound.y, minBound.z));
            vertices.push_back(maxBound);

            vertices.push_back(glm::vec3(minBound.x, maxBound.y, minBound.z));
            vertices.push_back(glm::vec3(minBound.x, maxBound.y, maxBound.z));
        } else {
            // Recurse to children
            for (auto& child : node->children) {
                if (child) {
                    generateOctreeVisualizationRecursive(child.get(), targetDepth, currentDepth + 1, vertices);
                }
            }
        }
    }

}