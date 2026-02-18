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

#pragma once

#include <filesystem>
#include <memory>
#include <unordered_map>
#include <deque>
#include <cstdint>

namespace otk { class Camera; }
class TextureCuda;

namespace dted {

/**
 * @brief Represents a single DTED tile in the tile cache
 */
struct DTEDTile {
    int latDegree;                              // Tile latitude (SW corner)
    int lonDegree;                              // Tile longitude (SW corner)
    std::unique_ptr<TextureCuda> texture;       // GPU texture
    bool loaded = false;                        // Whether texture is loaded to GPU
    std::filesystem::path filepath;             // Path to DTED file
};

/**
 * @brief Manages dynamic loading and caching of DTED terrain tiles
 * 
 * The tile manager uses an LRU (Least Recently Used) cache to keep frequently
 * accessed tiles in GPU memory while unloading distant tiles. Tiles are mapped
 * to UDIM coordinates for integration with the material/displacement system.
 * 
 * UDIM Mapping: UDIM = 1001 + (lon + 180) + (lat + 90) * 360
 * This provides a unique UDIM for each 1°×1° tile covering the globe.
 */
class DTEDTileManager {
public:
    /**
     * @brief Construct a tile manager
     * 
     * @param dtedDirectory Directory containing DTED files
     */
    explicit DTEDTileManager(const std::filesystem::path& dtedDirectory);
    
    /**
     * @brief Update tile loading based on camera position
     * 
     * Determines which tiles are visible and loads/unloads tiles as needed.
     * 
     * @param camera Current camera state
     * @param lodBias Level-of-detail bias for determining visible range
     */
    void update(const otk::Camera& camera, float lodBias = 1.0f);
    
    /**
     * @brief Set maximum number of tiles to keep in GPU memory
     * 
     * @param maxTiles Maximum loaded tiles (default 64)
     */
    void setMaxLoadedTiles(int maxTiles) { m_maxLoadedTiles = maxTiles; }
    
    /**
     * @brief Get UDIM index for a geographic coordinate
     * 
     * @param lat Latitude in degrees [-90, 90]
     * @param lon Longitude in degrees [-180, 180]
     * @return UDIM index
     */
    uint32_t getUDIMForCoordinate(float lat, float lon) const;
    
    /**
     * @brief Get texture for a specific UDIM tile
     * 
     * @param udim UDIM index
     * @return Pointer to texture, or nullptr if not loaded
     */
    const TextureCuda* getTextureForUDIM(uint32_t udim) const;
    
    /**
     * @brief Convert UDIM to geographic coordinates
     * 
     * @param udim UDIM index
     * @param[out] lat Latitude of SW corner (degrees)
     * @param[out] lon Longitude of SW corner (degrees)
     */
    void udimToLatLon(uint32_t udim, int& lat, int& lon) const;
    
    /**
     * @brief Convert geographic coordinates to UDIM
     * 
     * @param lat Latitude of SW corner (degrees)
     * @param lon Longitude of SW corner (degrees)
     * @return UDIM index
     */
    uint32_t latLonToUDIM(int lat, int lon) const;
    
    /**
     * @brief Get number of currently loaded tiles
     */
    size_t getLoadedTileCount() const;
    
private:
    /**
     * @brief Load a tile from disk to GPU
     */
    void loadTile(int lat, int lon);
    
    /**
     * @brief Unload the least recently used tile
     */
    void unloadLRUTile();
    
    /**
     * @brief Find DTED file(s) for a given tile
     */
    std::vector<std::filesystem::path> findDTEDFiles(int lat, int lon);
    
    /**
     * @brief Check if a tile is visible from the camera
     */
    bool isVisible(const DTEDTile& tile, const otk::Camera& camera) const;
    
    std::filesystem::path m_dtedDirectory;
    std::unordered_map<uint32_t, DTEDTile> m_tiles;
    std::deque<uint32_t> m_lruQueue;
    int m_maxLoadedTiles = 64;
};

} // namespace dted
