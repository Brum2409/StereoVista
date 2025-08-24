#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "../../headers/Engine/BVH.h"
#include <algorithm>
#include <numeric>
#include <iostream>
#include <climits>

namespace Engine {

    void BVHBuilder::build(const std::vector<BVHTriangle>& inputTriangles) {
        if (inputTriangles.empty()) {
            return;
        }
        
        // Initialize data structures
        triangles = inputTriangles;
        nodes.clear();
        triangleIndices.clear();
        
        // Estimate node count (typically ~2x triangle count for binary tree)
        nodes.reserve(triangles.size() * 2);
        triangleIndices.resize(triangles.size());
        
        // Initialize triangle indices array
        std::iota(triangleIndices.begin(), triangleIndices.end(), 0);
        
        // Reset counters
        rootNodeIdx = 0;
        nodesUsed = 1;
        
        // Create root node
        nodes.emplace_back();
        
        // Calculate root bounds
        AABB rootBounds = calculateBounds(0, static_cast<uint32_t>(triangles.size()));
        nodes[rootNodeIdx].setBounds(rootBounds);
        nodes[rootNodeIdx].leftFirst = 0;  // First triangle index
        nodes[rootNodeIdx].triCount = static_cast<uint32_t>(triangles.size());
        
        // Start recursive subdivision
        subdivide(rootNodeIdx);
        
        std::cout << "BVH built with " << nodesUsed << " nodes for " << triangles.size() << " triangles" << std::endl;
    }

    void BVHBuilder::subdivide(uint32_t nodeIdx) {
        BVHNode& node = nodes[nodeIdx];
        
        // Stop subdivision if we have few triangles
        if (node.triCount <= MAX_TRIANGLES_PER_LEAF) {
            return;
        }
        
        // Calculate node bounds
        AABB nodeBounds = calculateBounds(node.leftFirst, node.triCount);
        
        // Find the best split using SAH
        SplitResult split = findBestSplit(node.leftFirst, node.triCount, nodeBounds);
        
        // If no good split found, make it a leaf
        if (split.leftCount == 0 || split.leftCount == node.triCount) {
            return;
        }
        
        // Calculate cost of not splitting (making this a leaf)
        float leafCost = node.triCount * INTERSECTION_COST;
        
        // If split cost is not better than leaf cost, don't split
        // Add small epsilon to avoid splitting when gains are minimal
        if (split.cost >= leafCost * 0.95f) {
            return;
        }
        
        // Partition triangles based on the split
        uint32_t leftCount = partition(node.leftFirst, node.triCount, split.axis, split.position);
        
        // Ensure we actually got a valid partition
        if (leftCount == 0 || leftCount == node.triCount) {
            return;
        }
        
        // Create child nodes
        uint32_t leftChildIdx = nodesUsed++;
        uint32_t rightChildIdx = nodesUsed++;
        
        // Ensure we have space for new nodes
        if (nodes.size() <= rightChildIdx) {
            nodes.resize(rightChildIdx + 1);
        }
        
        // Set up left child
        nodes[leftChildIdx].leftFirst = node.leftFirst;
        nodes[leftChildIdx].triCount = leftCount;
        nodes[leftChildIdx].setBounds(calculateBounds(nodes[leftChildIdx].leftFirst, nodes[leftChildIdx].triCount));
        
        // Set up right child  
        nodes[rightChildIdx].leftFirst = node.leftFirst + leftCount;
        nodes[rightChildIdx].triCount = node.triCount - leftCount;
        nodes[rightChildIdx].setBounds(calculateBounds(nodes[rightChildIdx].leftFirst, nodes[rightChildIdx].triCount));
        
        // Convert current node to interior node
        node.leftFirst = leftChildIdx;
        node.triCount = 0;  // Mark as interior node
        
        // Recursively subdivide children
        subdivide(leftChildIdx);
        subdivide(rightChildIdx);
    }

    AABB BVHBuilder::calculateBounds(uint32_t first, uint32_t count) {
        AABB bounds;
        if (count == 0 || first >= triangleIndices.size()) {
            return bounds; // Return empty bounds for invalid input
        }
        
        for (uint32_t i = 0; i < count && (first + i) < triangleIndices.size(); i++) {
            uint32_t triIdx = triangleIndices[first + i];
            if (triIdx < triangles.size()) {
                const BVHTriangle& tri = triangles[triIdx];
                bounds.expand(tri.bounds);
            }
        }
        return bounds;
    }

    BVHBuilder::SplitResult BVHBuilder::findBestSplit(uint32_t first, uint32_t count, const AABB& nodeBounds) {
        SplitResult bestSplit;
        bestSplit.cost = FLT_MAX;
        bestSplit.axis = -1;
        bestSplit.position = 0.0f;
        bestSplit.leftCount = 0;
        
        // Try each axis
        for (int axis = 0; axis < 3; axis++) {
            float boundsMin = nodeBounds.minBounds[axis];
            float boundsMax = nodeBounds.maxBounds[axis];
            
            if (boundsMin == boundsMax) continue; // No extent in this axis
            
            // Use binned SAH for efficiency
            struct Bin {
                AABB bounds;
                uint32_t count = 0;
            };
            
            std::array<Bin, SAH_BINS> bins;
            
            // Populate bins
            float scale = SAH_BINS / (boundsMax - boundsMin);
            for (uint32_t i = 0; i < count; i++) {
                const BVHTriangle& tri = triangles[triangleIndices[first + i]];
                float centroid = tri.centroid[axis];
                
                int binIdx = static_cast<int>((centroid - boundsMin) * scale);
                if (binIdx < 0) binIdx = 0;
                if (binIdx >= static_cast<int>(SAH_BINS)) binIdx = static_cast<int>(SAH_BINS - 1);
                
                bins[binIdx].count++;
                bins[binIdx].bounds.expand(tri.bounds);
            }
            
            // Evaluate split candidates
            for (int splitBin = 1; splitBin < SAH_BINS; splitBin++) {
                AABB leftBounds, rightBounds;
                uint32_t leftCount = 0, rightCount = 0;
                
                // Accumulate left side
                for (int i = 0; i < splitBin; i++) {
                    leftBounds.expand(bins[i].bounds);
                    leftCount += bins[i].count;
                }
                
                // Accumulate right side
                for (int i = splitBin; i < SAH_BINS; i++) {
                    rightBounds.expand(bins[i].bounds);
                    rightCount += bins[i].count;
                }
                
                // Skip if one side is empty
                if (leftCount == 0 || rightCount == 0) continue;
                
                // Calculate split cost
                float cost = evaluateSAH(leftCount, rightCount, leftBounds, rightBounds, nodeBounds);
                
                if (cost < bestSplit.cost) {
                    bestSplit.cost = cost;
                    bestSplit.axis = axis;
                    bestSplit.leftCount = leftCount;
                    bestSplit.position = boundsMin + (splitBin / float(SAH_BINS)) * (boundsMax - boundsMin);
                }
            }
        }
        
        return bestSplit;
    }

    float BVHBuilder::evaluateSAH(uint32_t leftCount, uint32_t rightCount, 
                                  const AABB& leftBounds, const AABB& rightBounds, 
                                  const AABB& nodeBounds) {
        float parentArea = nodeBounds.getSurfaceArea();
        if (parentArea <= 0.0f) return FLT_MAX;
        
        float leftArea = leftBounds.getSurfaceArea();
        float rightArea = rightBounds.getSurfaceArea();
        
        float leftProb = leftArea / parentArea;
        float rightProb = rightArea / parentArea;
        
        return TRAVERSAL_COST + (leftProb * leftCount + rightProb * rightCount) * INTERSECTION_COST;
    }

    uint32_t BVHBuilder::partition(uint32_t first, uint32_t count, int axis, float splitPos) {
        if (count == 0) return 0;
        
        uint32_t left = first;
        uint32_t right = first + count - 1;
        
        while (left <= right && right != UINT32_MAX) { // Prevent overflow
            // Find triangle on left that should be on right
            while (left <= right && triangles[triangleIndices[left]].centroid[axis] < splitPos) {
                left++;
            }
            
            // Find triangle on right that should be on left
            while (left <= right && triangles[triangleIndices[right]].centroid[axis] >= splitPos) {
                right--;
            }
            
            // Swap if we found a pair
            if (left < right) {
                std::swap(triangleIndices[left], triangleIndices[right]);
                left++;
                right--;
            }
        }
        
        return left - first;
    }

} // namespace Engine