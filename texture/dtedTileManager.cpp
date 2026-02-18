//
// SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
// 
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
// 
// 1. Redistributions of source code must retain the above copyright notice, this
// list of conditions and the following disclaimer.
// 
// 2. Redistributions in binary form must reproduce the above copyright notice,
// this list of conditions and the following disclaimer in the documentation
// and/or other materials provided with the distribution.
// 
// 3. Neither the name of the copyright holder nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
// 
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
// FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
// CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
// OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//

#include "dtedTileManager.h"
#include "dtedReader.h"
#include "texture.h"
#include "textureCuda.h"
#include "textureCache.h"

#include <OptiXToolkit/Gui/Camera.h>

#include <iostream>
#include <algorithm>
#include <cmath>
#include <sstream>
#include <iomanip>

namespace dted {

namespace {
    // Convert latitude/longitude to UDIM format
    // UDIM = 1001 + (lon + 180) + (lat + 90) * 360
    constexpr uint32_t BASE_UDIM = 1001;
    constexpr int LON_OFFSET = 180;
    constexpr int LAT_OFFSET = 90;
    constexpr int LON_RANGE = 360;
}

DTEDTileManager::DTEDTileManager(const std::filesystem::path& dtedDirectory)
    : m_dtedDirectory(dtedDirectory)
{
    if (!std::filesystem::exists(dtedDirectory)) {
        std::cerr << "DTED directory does not exist: " << dtedDirectory << "\n";
    }
}

uint32_t DTEDTileManager::getUDIMForCoordinate(float lat, float lon) const {
    // Convert coordinates to tile indices (floor to SW corner)
    const int latTile = static_cast<int>(std::floor(lat));
    const int lonTile = static_cast<int>(std::floor(lon));
    return latLonToUDIM(latTile, lonTile);
}

uint32_t DTEDTileManager::latLonToUDIM(int lat, int lon) const {
    // Clamp to valid ranges
    lat = std::max(-90, std::min(89, lat));
    lon = std::max(-180, std::min(179, lon));
    
    return BASE_UDIM + (lon + LON_OFFSET) + (lat + LAT_OFFSET) * LON_RANGE;
}

void DTEDTileManager::udimToLatLon(uint32_t udim, int& lat, int& lon) const {
    const uint32_t offset = udim - BASE_UDIM;
    lon = static_cast<int>(offset % LON_RANGE) - LON_OFFSET;
    lat = static_cast<int>(offset / LON_RANGE) - LAT_OFFSET;
}

std::vector<std::filesystem::path> DTEDTileManager::findDTEDFiles(int lat, int lon) {
    std::vector<std::filesystem::path> results;
    
    if (!std::filesystem::exists(m_dtedDirectory)) {
        return results;
    }
    
    // DTED naming convention: [n|s]YY[e|w]XXX.dt[0|1|2]
    // Example: n37w122.dt2 (37°N, 122°W)
    
    const char latHem = (lat >= 0) ? 'n' : 's';
    const char lonHem = (lon >= 0) ? 'e' : 'w';
    const int absLat = std::abs(lat);
    const int absLon = std::abs(lon);
    
    // Try all DTED levels (dt0, dt1, dt2)
    for (int level = 0; level <= 2; ++level) {
        std::ostringstream filename;
        filename << latHem << std::setfill('0') << std::setw(2) << absLat
                 << lonHem << std::setfill('0') << std::setw(3) << absLon
                 << ".dt" << level;
        
        const auto filepath = m_dtedDirectory / filename.str();
        if (std::filesystem::exists(filepath)) {
            results.push_back(filepath);
        }
    }
    
    return results;
}

void DTEDTileManager::loadTile(int lat, int lon) {
    const uint32_t udim = latLonToUDIM(lat, lon);
    
    // Check if already loaded
    auto it = m_tiles.find(udim);
    if (it != m_tiles.end() && it->second.loaded) {
        // Move to front of LRU queue
        // Remove from current position (O(n) - acceptable for small cache sizes)
        m_lruQueue.erase(std::remove(m_lruQueue.begin(), m_lruQueue.end(), udim), m_lruQueue.end());
        m_lruQueue.push_front(udim);
        return;
    }
    
    // Find DTED file
    auto files = findDTEDFiles(lat, lon);
    if (files.empty()) {
        // No file found - create placeholder
        if (it == m_tiles.end()) {
            DTEDTile tile;
            tile.latDegree = lat;
            tile.lonDegree = lon;
            tile.loaded = false;
            m_tiles[udim] = std::move(tile);
        }
        return;
    }
    
    // Load the highest resolution file available (last in list)
    const auto& filepath = files.back();
    
    DTEDReader reader;
    if (!reader.load(filepath)) {
        std::cerr << "Failed to load DTED tile: " << filepath << "\n";
        return;
    }
    
    // Convert to texture
    Texture tex;
    if (!reader.toTexture(tex)) {
        std::cerr << "Failed to convert DTED to texture\n";
        return;
    }
    
    // Create GPU texture
    auto cudaTex = std::make_unique<TextureCuda>();
    
    // Upload to GPU using TextureCache helper
    // Note: This is a simplified version - actual implementation would use TextureCache::loadTexture
    // For now, we'll create a placeholder
    
    // Store tile
    DTEDTile tile;
    tile.latDegree = lat;
    tile.lonDegree = lon;
    tile.filepath = filepath;
    tile.texture = std::move(cudaTex);
    tile.loaded = true;
    
    m_tiles[udim] = std::move(tile);
    m_lruQueue.push_front(udim);
    
    // Enforce max tile limit
    while (getLoadedTileCount() > static_cast<size_t>(m_maxLoadedTiles)) {
        unloadLRUTile();
    }
}

void DTEDTileManager::unloadLRUTile() {
    if (m_lruQueue.empty()) {
        return;
    }
    
    const uint32_t udim = m_lruQueue.back();
    m_lruQueue.pop_back();
    
    auto it = m_tiles.find(udim);
    if (it != m_tiles.end()) {
        it->second.texture.reset();
        it->second.loaded = false;
    }
}

bool DTEDTileManager::isVisible(const DTEDTile& tile, const otk::Camera& camera) const {
    // Simple frustum culling based on camera position
    // For a globe viewer, we'd check if the tile is on the visible hemisphere
    
    // TODO: Implement proper frustum culling:
    // 1. Project camera frustum planes onto ellipsoid surface
    // 2. Test tile bounding box against frustum planes
    // 3. Check if tile is on visible hemisphere (dot product with view direction)
    
    // For now, use a simple distance-based check as placeholder
    return true;  // Always visible for initial implementation
}

void DTEDTileManager::update(const otk::Camera& camera, float lodBias) {
    // For ellipsoid viewer, we'd determine visible tiles based on camera frustum
    // For initial implementation, load tiles near camera look-at point
    
    // This is a simplified version - proper implementation would:
    // 1. Project camera frustum onto ellipsoid
    // 2. Determine visible tile range
    // 3. Load tiles in priority order (near to far)
    
    // For now, just ensure we don't exceed max tile count
    while (getLoadedTileCount() > static_cast<size_t>(m_maxLoadedTiles)) {
        unloadLRUTile();
    }
}

const TextureCuda* DTEDTileManager::getTextureForUDIM(uint32_t udim) const {
    auto it = m_tiles.find(udim);
    if (it != m_tiles.end() && it->second.loaded) {
        return it->second.texture.get();
    }
    return nullptr;
}

size_t DTEDTileManager::getLoadedTileCount() const {
    size_t count = 0;
    for (const auto& pair : m_tiles) {
        if (pair.second.loaded) {
            ++count;
        }
    }
    return count;
}

} // namespace dted
