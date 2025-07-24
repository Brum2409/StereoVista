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


namespace Engine {

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

        // Default handling for XYZ, PLY, and other text formats
        PointCloud pointCloud;
        pointCloud.name = "PointCloud_" + std::filesystem::path(filePath).filename().string();
        pointCloud.position = glm::vec3(0.0f);
        pointCloud.rotation = glm::vec3(0.0f);
        pointCloud.scale = glm::vec3(1.0f);

        std::ifstream file(filePath, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "Failed to open point cloud file: " << filePath << std::endl;
            return std::move(pointCloud);
        }

        std::cout << "Loading point cloud from: " << filePath << std::endl;

        constexpr size_t bufferSize = 1024 * 1024; // 1 MB buffer
        std::vector<char> buffer(bufferSize);

        std::mutex pointCloudMutex;
        std::atomic<size_t> totalPointsProcessed(0);

        const size_t numThreads = std::thread::hardware_concurrency();
        std::vector<std::thread> threads;

        auto processChunk = [&](const std::vector<char>& chunk, size_t chunkSize) {
            std::vector<PointCloudPoint> localPoints;
            std::istringstream iss(std::string(chunk.data(), chunkSize));
            std::string line;
            size_t pointCounter = 0;

            while (std::getline(iss, line)) {
                if (!line.empty() && pointCounter % downsampleFactor == 0) {
                    float x, y, z, intensity;
                    int r, g, b;
                    if (sscanf_s(line.c_str(), "%f %f %f %f %d %d %d", &x, &y, &z, &intensity, &r, &g, &b) == 7) {
                        PointCloudPoint point;
                        point.position = glm::vec3(x, y, z);
                        point.intensity = 1.0f;
                        point.color = glm::vec3(r / 255.0f, g / 255.0f, b / 255.0f);
                        localPoints.push_back(point);
                    }
                }
                pointCounter++;
            }

            {
                std::lock_guard<std::mutex> lock(pointCloudMutex);
                pointCloud.points.insert(pointCloud.points.end(), localPoints.begin(), localPoints.end());
            }

            totalPointsProcessed += pointCounter;
            };

        while (file) {
            file.read(buffer.data(), bufferSize);
            std::streamsize bytesRead = file.gcount();

            if (bytesRead > 0) {
                threads.emplace_back(processChunk, buffer, bytesRead);

                if (threads.size() >= numThreads) {
                    for (auto& thread : threads) {
                        thread.join();
                    }
                    threads.clear();
                }
            }
        }

        for (auto& thread : threads) {
            thread.join();
        }

        file.close();

        std::cout << "Total points in file: " << totalPointsProcessed << std::endl;
        std::cout << "Points loaded after downsampling: " << pointCloud.points.size() << std::endl;

        setupPointCloudGLBuffers(pointCloud);


        if (pointCloud.useOctree) {
            OctreePointCloudManager::buildOctree(pointCloud);
        } else {
            generateChunks(pointCloud, 2.0f);
        }


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
        std::cout << "[DEBUG] loadFromBinary() called with file: " << filePath << std::endl;
        
        PointCloud pointCloud;
        pointCloud.name = "PointCloud_" + std::filesystem::path(filePath).filename().string();
        pointCloud.position = glm::vec3(0.0f);
        pointCloud.rotation = glm::vec3(0.0f);
        pointCloud.scale = glm::vec3(1.0f);
        
        std::cout << "[DEBUG] PointCloud initialized with name: " << pointCloud.name << std::endl;
        
        std::ifstream file(filePath, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "[ERROR] Failed to open file for reading: " << filePath << std::endl;
            return std::move(pointCloud);
        }
        
        std::cout << "[DEBUG] File opened successfully" << std::endl;

        try {
            // Read and verify header
            std::cout << "[DEBUG] Reading magic number..." << std::endl;
            char magic[4];
            file.read(magic, 4);
            std::cout << "[DEBUG] Magic number read: " << magic[0] << magic[1] << magic[2] << magic[3] << std::endl;
            
            if (std::memcmp(magic, BINARY_MAGIC_NUMBER, 4) != 0) {
                std::cerr << "[ERROR] Invalid binary point cloud file format" << std::endl;
                throw std::runtime_error("Invalid binary point cloud file format");
            }
            
            std::cout << "[DEBUG] Magic number verified successfully" << std::endl;

            uint32_t numPoints;
            file.read(reinterpret_cast<char*>(&numPoints), sizeof(numPoints));
            std::cout << "[DEBUG] Number of points in file: " << numPoints << std::endl;

            pointCloud.points.reserve(numPoints);
            std::cout << "[DEBUG] Reserved space for " << numPoints << " points" << std::endl;

            const size_t pointSize = sizeof(glm::vec3) + sizeof(uint32_t) + sizeof(glm::u8vec3);
            constexpr size_t bufferSize = 1024 * 1024; // 1 MB buffer
            const size_t pointsPerBuffer = bufferSize / pointSize;

            std::vector<char> buffer(bufferSize);
            std::mutex pointCloudMutex;
            std::atomic<size_t> totalPointsProcessed(0);

            const size_t numThreads = std::thread::hardware_concurrency();
            std::vector<std::future<void>> futures;

            auto processChunk = [&](const std::vector<char>& chunk, size_t numPoints) {
                std::vector<PointCloudPoint> localPoints;
                localPoints.reserve(numPoints);

                const char* data = chunk.data();
                for (size_t i = 0; i < numPoints; ++i) {
                    PointCloudPoint point;
                    std::memcpy(&point.position, data, sizeof(point.position));
                    data += sizeof(point.position);

                    uint32_t intensity;
                    std::memcpy(&intensity, data, sizeof(intensity));
                    point.intensity = intensity / 1000.0f;
                    data += sizeof(intensity);

                    glm::u8vec3 color;
                    std::memcpy(&color, data, sizeof(color));
                    point.color = glm::vec3(color) / 255.0f;
                    data += sizeof(color);

                    localPoints.push_back(point);
                }

                {
                    std::lock_guard<std::mutex> lock(pointCloudMutex);
                    pointCloud.points.insert(pointCloud.points.end(), localPoints.begin(), localPoints.end());
                }

                totalPointsProcessed += localPoints.size();
                };

            while (totalPointsProcessed < numPoints) {
                size_t remainingPoints = numPoints - totalPointsProcessed;
                size_t pointsToRead = std::min(pointsPerBuffer, remainingPoints);
                size_t bytesToRead = pointsToRead * pointSize;

                buffer.resize(bytesToRead);
                file.read(buffer.data(), bytesToRead);
                std::streamsize bytesRead = file.gcount();

                if (bytesRead > 0) {
                    size_t actualPointsRead = bytesRead / pointSize;
                    futures.push_back(std::async(std::launch::async, processChunk, buffer, actualPointsRead));

                    if (futures.size() >= numThreads) {
                        for (auto& future : futures) {
                            future.wait();
                        }
                        futures.clear();
                    }
                }
            }

            for (auto& future : futures) {
                future.wait();
            }
            
            std::cout << "[DEBUG] All futures completed" << std::endl;

            file.close();
            std::cout << "[DEBUG] File closed" << std::endl;

            // Initialize transformation values
            pointCloud.position = glm::vec3(0.0f);
            pointCloud.rotation = glm::vec3(0.0f);
            pointCloud.scale = glm::vec3(1.0f);
            
            std::cout << "[DEBUG] Transformation values initialized" << std::endl;

            std::cout << "[DEBUG] Setting up GL buffers..." << std::endl;
            setupPointCloudGLBuffers(pointCloud);
            std::cout << "[DEBUG] GL buffers setup complete" << std::endl;

            std::cout << "Successfully loaded point cloud from: " << filePath << std::endl;
            std::cout << "Loaded " << pointCloud.points.size() << " points" << std::endl;

        }
        catch (const std::exception& e) {
            std::cerr << "Error loading point cloud: " << e.what() << std::endl;
            pointCloud = std::move(PointCloud{}); // Reset to empty point cloud
        }


        std::cout << "[DEBUG] useOctree flag: " << (pointCloud.useOctree ? "true" : "false") << std::endl;
        if (pointCloud.useOctree) {
            std::cout << "[DEBUG] Starting octree build..." << std::endl;
            OctreePointCloudManager::buildOctree(pointCloud);
            std::cout << "[DEBUG] Octree build completed" << std::endl;
        } else {
            std::cout << "[DEBUG] Using legacy chunk generation..." << std::endl;
            generateChunks(pointCloud, 2.0f);
            std::cout << "[DEBUG] Legacy chunk generation completed" << std::endl;
        }


        std::cout << "[DEBUG] loadFromBinary() returning successfully" << std::endl;
        return std::move(pointCloud);
    }


    void generateChunkOutlineVertices(PointCloud& pointCloud) {
        pointCloud.chunkOutlineVertices.clear();

        for (const auto& chunk : pointCloud.chunks) {
            glm::vec3 minBound = chunk.centerPosition - glm::vec3(pointCloud.chunkSize / 2.0f);
            glm::vec3 maxBound = chunk.centerPosition + glm::vec3(pointCloud.chunkSize / 2.0f);

            // Front face
            pointCloud.chunkOutlineVertices.push_back(minBound);
            pointCloud.chunkOutlineVertices.push_back(glm::vec3(maxBound.x, minBound.y, minBound.z));

            pointCloud.chunkOutlineVertices.push_back(glm::vec3(maxBound.x, minBound.y, minBound.z));
            pointCloud.chunkOutlineVertices.push_back(glm::vec3(maxBound.x, maxBound.y, minBound.z));

            pointCloud.chunkOutlineVertices.push_back(glm::vec3(maxBound.x, maxBound.y, minBound.z));
            pointCloud.chunkOutlineVertices.push_back(glm::vec3(minBound.x, maxBound.y, minBound.z));

            pointCloud.chunkOutlineVertices.push_back(glm::vec3(minBound.x, maxBound.y, minBound.z));
            pointCloud.chunkOutlineVertices.push_back(minBound);

            // Back face
            pointCloud.chunkOutlineVertices.push_back(glm::vec3(minBound.x, minBound.y, maxBound.z));
            pointCloud.chunkOutlineVertices.push_back(glm::vec3(maxBound.x, minBound.y, maxBound.z));

            pointCloud.chunkOutlineVertices.push_back(glm::vec3(maxBound.x, minBound.y, maxBound.z));
            pointCloud.chunkOutlineVertices.push_back(maxBound);

            pointCloud.chunkOutlineVertices.push_back(maxBound);
            pointCloud.chunkOutlineVertices.push_back(glm::vec3(minBound.x, maxBound.y, maxBound.z));

            pointCloud.chunkOutlineVertices.push_back(glm::vec3(minBound.x, maxBound.y, maxBound.z));
            pointCloud.chunkOutlineVertices.push_back(glm::vec3(minBound.x, minBound.y, maxBound.z));

            // Connecting lines
            pointCloud.chunkOutlineVertices.push_back(minBound);
            pointCloud.chunkOutlineVertices.push_back(glm::vec3(minBound.x, minBound.y, maxBound.z));

            pointCloud.chunkOutlineVertices.push_back(glm::vec3(maxBound.x, minBound.y, minBound.z));
            pointCloud.chunkOutlineVertices.push_back(glm::vec3(maxBound.x, minBound.y, maxBound.z));

            pointCloud.chunkOutlineVertices.push_back(glm::vec3(maxBound.x, maxBound.y, minBound.z));
            pointCloud.chunkOutlineVertices.push_back(maxBound);

            pointCloud.chunkOutlineVertices.push_back(glm::vec3(minBound.x, maxBound.y, minBound.z));
            pointCloud.chunkOutlineVertices.push_back(glm::vec3(minBound.x, maxBound.y, maxBound.z));
        }

        // Create and bind VAO and VBO for chunk outlines
        glGenVertexArrays(1, &pointCloud.chunkOutlineVAO);
        glGenBuffers(1, &pointCloud.chunkOutlineVBO);

        glBindVertexArray(pointCloud.chunkOutlineVAO);
        glBindBuffer(GL_ARRAY_BUFFER, pointCloud.chunkOutlineVBO);
        glBufferData(GL_ARRAY_BUFFER, pointCloud.chunkOutlineVertices.size() * sizeof(glm::vec3), pointCloud.chunkOutlineVertices.data(), GL_STATIC_DRAW);

        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void*)0);
        glEnableVertexAttribArray(0);

        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);
    }

    glm::vec3 calculateTransformedChunkPosition(const glm::vec3& chunkPos, const glm::mat4& modelMatrix) {
        glm::vec4 transformedPos = modelMatrix * glm::vec4(chunkPos, 1.0f);
        return glm::vec3(transformedPos);
    }

    void generateChunks(PointCloud& pointCloud, float chunkSize) {
        // Clean up old chunks
        for (auto& chunk : pointCloud.chunks) {
            for (GLuint vbo : chunk.lodVBOs) {
                if (vbo != 0) {
                    glDeleteBuffers(1, &vbo);
                }
            }
        }

        pointCloud.chunkSize = chunkSize;
        pointCloud.chunks.clear();

        // Use an unordered_map with a custom hash function for 3D indices
        struct ChunkIndexHash {
            std::size_t operator()(const glm::ivec3& index) const {
                std::size_t h1 = std::hash<int>()(index.x);
                std::size_t h2 = std::hash<int>()(index.y);
                std::size_t h3 = std::hash<int>()(index.z);
                return h1 ^ (h2 << 1) ^ (h3 << 2);
            }
        };

        std::unordered_map<glm::ivec3, std::vector<PointCloudPoint>, ChunkIndexHash> chunkMap;

        // Create transformation matrix
        glm::mat4 modelMatrix = glm::mat4(1.0f);
        modelMatrix = glm::translate(modelMatrix, pointCloud.position);
        modelMatrix = glm::rotate(modelMatrix, glm::radians(pointCloud.rotation.x), glm::vec3(1, 0, 0));
        modelMatrix = glm::rotate(modelMatrix, glm::radians(pointCloud.rotation.y), glm::vec3(0, 1, 0));
        modelMatrix = glm::rotate(modelMatrix, glm::radians(pointCloud.rotation.z), glm::vec3(0, 0, 1));
        modelMatrix = glm::scale(modelMatrix, pointCloud.scale);

        // Populate the chunk map based on point positions
        for (const auto& point : pointCloud.points) {
            // Transform the point position
            glm::vec3 transformedPos = glm::vec3(modelMatrix * glm::vec4(point.position, 1.0f));
            glm::ivec3 chunkIndex = glm::floor(transformedPos / chunkSize);
            chunkMap[chunkIndex].push_back(point);
        }

        // Create chunks from the map
        for (auto& [chunkIndex, points] : chunkMap) {
            PointCloudChunk chunk;
            chunk.points = std::move(points);

            // Calculate chunk center position
            chunk.centerPosition = glm::vec3(
                (chunkIndex.x + 0.5f) * chunkSize,
                (chunkIndex.y + 0.5f) * chunkSize,
                (chunkIndex.z + 0.5f) * chunkSize
            );

            // Calculate bounding radius
            float maxDistSq = 0.0f;
            for (const auto& point : chunk.points) {
                glm::vec3 diff = point.position - chunk.centerPosition;
                float distSq = glm::dot(diff, diff);
                maxDistSq = std::max(maxDistSq, distSq);
            }
            chunk.boundingRadius = std::sqrt(maxDistSq);

            // Generate LOD levels for the chunk
            generateLODLevels(chunk);

            pointCloud.chunks.push_back(std::move(chunk));
        }

        generateChunkOutlineVertices(pointCloud);
    }


    struct OctreeNodeData {
        // Store indices instead of full points
        std::vector<size_t> pointIndices;
    };

    void generateLODLevels(PointCloudChunk& chunk) {
        const int numLODLevels = 5;
        chunk.lodVBOs.resize(numLODLevels);
        chunk.lodPointCounts.resize(numLODLevels);

        // Only points in dense areas should be reduced
        const size_t baseThresholds[] = {
            std::numeric_limits<size_t>::max(),
            40000, 15000, 5000, 2500
        };

        // Calculate bounds in a single pass
        glm::vec3 chunkMin(std::numeric_limits<float>::max());
        glm::vec3 chunkMax(std::numeric_limits<float>::lowest());

        for (const auto& point : chunk.points) {
            chunkMin = glm::min(chunkMin, point.position);
            chunkMax = glm::max(chunkMax, point.position);
        }

        // Process points in batches for each LOD level
        const size_t BATCH_SIZE = 1000000; // Process 1M points at a time

        // First level is always original data
        chunk.lodPointCounts[0] = chunk.points.size();
        glGenBuffers(1, &chunk.lodVBOs[0]);
        glBindBuffer(GL_ARRAY_BUFFER, chunk.lodVBOs[0]);
        glBufferData(GL_ARRAY_BUFFER, chunk.points.size() * sizeof(PointCloudPoint),
            chunk.points.data(), GL_STATIC_DRAW);

        // For remaining levels
        for (int level = 1; level < numLODLevels; level++) {
            if (chunk.points.size() <= baseThresholds[level]) {
                // Just reference the original data if below threshold
                chunk.lodVBOs[level] = chunk.lodVBOs[0];
                chunk.lodPointCounts[level] = chunk.points.size();
                continue;
            }

            // Calculate target size for this level
            size_t targetCount = baseThresholds[level];
            float selectionRatio = float(targetCount) / float(chunk.points.size());

            // Process in batches to keep memory usage down
            std::vector<PointCloudPoint> selectedPoints;
            selectedPoints.reserve(targetCount);

            for (size_t offset = 0; offset < chunk.points.size(); offset += BATCH_SIZE) {
                size_t batchSize = std::min(BATCH_SIZE, chunk.points.size() - offset);
                size_t batchTargetCount = std::max(size_t(1), size_t(batchSize * selectionRatio));

                // Create indices for this batch
                std::vector<size_t> indices(batchSize);
                std::iota(indices.begin(), indices.end(), 0);

                // Randomly select points from this batch
                std::random_device rd;
                std::mt19937 gen(rd());

                if (batchTargetCount < batchSize) {
                    // Reservoir sampling for the batch
                    for (size_t i = 0; i < batchTargetCount; i++) {
                        std::uniform_int_distribution<size_t> dist(i, batchSize - 1);
                        size_t j = dist(gen);
                        std::swap(indices[i], indices[j]);
                    }
                    indices.resize(batchTargetCount);
                }

                // Add selected points to result
                for (size_t idx : indices) {
                    selectedPoints.push_back(chunk.points[offset + idx]);
                }

                // If we've collected enough points, stop
                if (selectedPoints.size() >= targetCount) {
                    selectedPoints.resize(targetCount);
                    break;
                }
            }

            // Create VBO for this level
            glGenBuffers(1, &chunk.lodVBOs[level]);
            glBindBuffer(GL_ARRAY_BUFFER, chunk.lodVBOs[level]);
            glBufferData(GL_ARRAY_BUFFER, selectedPoints.size() * sizeof(PointCloudPoint),
                selectedPoints.data(), GL_STATIC_DRAW);
            chunk.lodPointCounts[level] = selectedPoints.size();

            // Clear the temporary buffer immediately
            selectedPoints.clear();
            selectedPoints.shrink_to_fit();
        }

        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }


   

    void PointCloudLoader::setupPointCloudGLBuffers(PointCloud& pointCloud) {
        glGenVertexArrays(1, &pointCloud.vao);
        glGenBuffers(1, &pointCloud.vbo);

        glBindVertexArray(pointCloud.vao);
        glBindBuffer(GL_ARRAY_BUFFER, pointCloud.vbo);
        glBufferData(GL_ARRAY_BUFFER, pointCloud.points.size() * sizeof(PointCloudPoint), pointCloud.points.data(), GL_STATIC_DRAW);

        // Position attribute
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(PointCloudPoint), (void*)0);
        glEnableVertexAttribArray(0);

        // Color attribute
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(PointCloudPoint), (void*)offsetof(PointCloudPoint, color));
        glEnableVertexAttribArray(1);

        // Intensity attribute
        glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(PointCloudPoint), (void*)offsetof(PointCloudPoint, intensity));
        glEnableVertexAttribArray(2);

        glBindVertexArray(0);
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
                // Compound type - try to read as structured data
                pointCloud.points.resize(pointsToRead);
                
                if (downsampleFactor > 1) {
                    // Read with stride for downsampling
                    hsize_t start[1] = {0};
                    hsize_t count[1] = {pointsToRead};
                    hsize_t stride[1] = {downsampleFactor};
                    hsize_t block[1] = {1};
                    
                    DataSpace memspace(1, count);
                    dataspace.selectHyperslab(H5S_SELECT_SET, count, start, stride, block);
                    
                    dataset.read(pointCloud.points.data(), pointType, memspace, dataspace);
                } else {
                    dataset.read(pointCloud.points.data(), pointType);
                }
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
                        
                        // If we successfully read coordinate data from f5, create the point cloud
                        if (!xCoords.empty() && !yCoords.empty() && !zCoords.empty()) {
                            hsize_t numCoordPoints = xCoords.size();
                            hsize_t pointsToRead = numCoordPoints;
                            
                            if (downsampleFactor > 1) {
                                pointsToRead = numCoordPoints / downsampleFactor;
                            }
                            
                            pointCloud.points.resize(pointsToRead);
                            
                            for (hsize_t i = 0; i < pointsToRead; i++) {
                                hsize_t sourceIndex = i * downsampleFactor;
                                
                                pointCloud.points[i].position.x = xCoords[sourceIndex];
                                pointCloud.points[i].position.y = yCoords[sourceIndex];
                                pointCloud.points[i].position.z = zCoords[sourceIndex];
                                
                                // Set color if available
                                if (!rColors.empty() && !gColors.empty() && !bColors.empty() && sourceIndex < rColors.size()) {
                                    pointCloud.points[i].color.r = rColors[sourceIndex];
                                    pointCloud.points[i].color.g = gColors[sourceIndex];
                                    pointCloud.points[i].color.b = bColors[sourceIndex];
                                } else {
                                    pointCloud.points[i].color = glm::vec3(1.0f, 1.0f, 1.0f); // Default white
                                }
                                
                                // Set intensity if available
                                if (!intensities.empty() && sourceIndex < intensities.size()) {
                                    pointCloud.points[i].intensity = intensities[sourceIndex];
                                } else {
                                    pointCloud.points[i].intensity = 1.0f; // Default intensity
                                }
                            }
                            
                            std::cout << "Successfully created point cloud with " << pointsToRead << " points from f5 data" << std::endl;
                            
                            // Skip the regular dataset processing since we already have our data
                            file.close();
                            
                            // Set up OpenGL buffers and build octree
                            setupPointCloudGLBuffers(pointCloud);
                            
                            
                            if (pointCloud.useOctree) {
                                OctreePointCloudManager::buildOctree(pointCloud);
                            } else {
                                generateChunks(pointCloud, 2.0f);
                            }

                            
                            std::cout << "Successfully loaded " << pointCloud.points.size() << " points from f5 file" << std::endl;
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

            std::cout << "Successfully loaded " << pointCloud.points.size() << " points from HDF5 file" << std::endl;

        } catch (const H5::Exception& e) {
            std::cerr << "HDF5 error loading point cloud: " << e.getDetailMsg() << std::endl;
            return std::move(pointCloud);
        } catch (const std::exception& e) {
            std::cerr << "Error loading HDF5 point cloud: " << e.what() << std::endl;
            return std::move(pointCloud);
        }

        // Set up OpenGL buffers and build octree
        setupPointCloudGLBuffers(pointCloud);
        
        
        if (pointCloud.useOctree) {
            OctreePointCloudManager::buildOctree(pointCloud);
        } else {
            // Fallback to legacy chunking system
            generateChunks(pointCloud, 2.0f);
        }


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
            
            // Use safer ctime_s function
            char timeBuffer[26];
            ctime_s(timeBuffer, sizeof(timeBuffer), &time);
            std::string timeStr(timeBuffer);
            timeStr.pop_back(); // Remove newline
            
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

} // namespace Engine