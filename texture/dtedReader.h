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

#include <cstdint>
#include <filesystem>
#include <vector>

struct Texture;

namespace dted {

/**
 * @brief DTED file header information
 */
struct DTEDHeader {
    int16_t latOrigin;       // Origin latitude in degrees (SW corner)
    int16_t lonOrigin;       // Origin longitude in degrees (SW corner)
    int32_t latInterval;     // Latitude spacing in arc-seconds
    int32_t lonInterval;     // Longitude spacing in arc-seconds
    int32_t numLatPoints;    // Number of latitude lines
    int32_t numLonPoints;    // Number of longitude lines (columns)
    int16_t minElevation;    // Minimum elevation in meters
    int16_t maxElevation;    // Maximum elevation in meters
};

/**
 * @brief DTED file reader supporting Level 0, 1, and 2 formats
 * 
 * DTED (Digital Terrain Elevation Data) is a standard format for terrain elevation data
 * used by military and civilian applications. The format stores elevation posts in 
 * column-major order.
 * 
 * Reference: MIL-PRF-89020B
 */
class DTEDReader {
public:
    DTEDReader() = default;
    ~DTEDReader() = default;
    
    /**
     * @brief Load a DTED file from disk
     * 
     * @param dtedFile Path to .dt0, .dt1, or .dt2 file
     * @return true on success, false on failure
     */
    bool load(const std::filesystem::path& dtedFile);
    
    /**
     * @brief Convert elevation data to a texture
     * 
     * Converts elevation values to normalized floating-point format suitable
     * for GPU displacement mapping.
     * 
     * @param tex Output texture to populate
     * @return true on success, false on failure
     */
    bool toTexture(Texture& tex);
    
    /**
     * @brief Get elevation value at specific row and column
     * 
     * @param row Row index (latitude)
     * @param col Column index (longitude)
     * @return Elevation in meters, or -32767 for missing data
     */
    int16_t getElevation(int row, int col) const;
    
    /**
     * @brief Get the parsed header information
     */
    const DTEDHeader& getHeader() const { return m_header; }
    
    /**
     * @brief Check if data has been successfully loaded
     */
    bool isLoaded() const { return !m_elevations.empty(); }
    
private:
    /**
     * @brief Parse User Header Label (UHL) record
     */
    bool parseUHL(const uint8_t* data);
    
    /**
     * @brief Parse Data Set Identification (DSI) record
     */
    bool parseDSI(const uint8_t* data);
    
    /**
     * @brief Parse Accuracy (ACC) record
     */
    bool parseACC(const uint8_t* data);
    
    /**
     * @brief Parse elevation data records
     */
    bool parseDataRecords(const uint8_t* data, size_t fileSize);
    
    /**
     * @brief Convert big-endian int16 to host byte order
     */
    static int16_t readInt16BE(const uint8_t* data);
    
    /**
     * @brief Convert big-endian int32 to host byte order
     */
    static int32_t readInt32BE(const uint8_t* data);
    
    DTEDHeader m_header{};
    std::vector<int16_t> m_elevations;
};

} // namespace dted
