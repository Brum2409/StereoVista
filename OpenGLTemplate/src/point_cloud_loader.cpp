// point_cloud_loader.cpp
#include "point_cloud_loader.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <thread>
#include <mutex>
#include <atomic>
#include <glad/glad.h>
#include <filesystem>


namespace Engine {

    PointCloud PointCloudLoader::loadPointCloudFile(const std::string& filePath, size_t downsampleFactor) {
        PointCloud pointCloud;
        pointCloud.name = "PointCloud_" + std::filesystem::path(filePath).filename().string();
        pointCloud.position = glm::vec3(0.0f);
        pointCloud.rotation = glm::vec3(0.0f);
        pointCloud.scale = glm::vec3(1.0f);

        std::ifstream file(filePath, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "Failed to open point cloud file: " << filePath << std::endl;
            return pointCloud;
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
                    float x, y, z;
                    int intensity, r, g, b;
                    if (sscanf(line.c_str(), "%f %f %f %d %d %d %d", &x, &y, &z, &intensity, &r, &g, &b) == 7) {
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

        return pointCloud;
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

} // namespace Engine