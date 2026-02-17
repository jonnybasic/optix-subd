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

#include "dtedReader.h"
#include "texture.h"

#include <fstream>
#include <cstring>
#include <algorithm>
#include <iostream>

namespace dted {

namespace {
    // DTED record sizes (bytes)
    constexpr size_t UHL_SIZE = 80;
    constexpr size_t DSI_SIZE = 648;
    constexpr size_t ACC_SIZE = 2700;
    
    // Missing data sentinel value
    constexpr int16_t MISSING_DATA = -32767;
}

int16_t DTEDReader::readInt16BE(const uint8_t* data) {
    return static_cast<int16_t>((data[0] << 8) | data[1]);
}

int32_t DTEDReader::readInt32BE(const uint8_t* data) {
    return static_cast<int32_t>((data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3]);
}

bool DTEDReader::parseUHL(const uint8_t* data) {
    // Verify UHL sentinel
    if (std::strncmp(reinterpret_cast<const char*>(data), "UHL1", 4) != 0) {
        std::cerr << "Invalid UHL record sentinel\n";
        return false;
    }
    
    // Parse origin (bytes 4-11: longitude, 12-19: latitude)
    // Format: DDDMMSS[H] where H is hemisphere (N/S/E/W)
    char lonStr[9] = {0}, latStr[9] = {0};
    std::memcpy(lonStr, data + 4, 8);
    std::memcpy(latStr, data + 12, 8);
    
    // Validate that position 0-2 and 3-6 are digits
    for (int i = 0; i < 7; ++i) {
        if (!std::isdigit(static_cast<unsigned char>(lonStr[i])) || 
            !std::isdigit(static_cast<unsigned char>(latStr[i]))) {
            std::cerr << "Invalid coordinate format in UHL record\n";
            return false;
        }
    }
    
    // Simple parsing - extract degrees and hemisphere
    int lonDeg = (lonStr[0] - '0') * 100 + (lonStr[1] - '0') * 10 + (lonStr[2] - '0');
    int latDeg = (latStr[0] - '0') * 100 + (latStr[1] - '0') * 10 + (latStr[2] - '0');
    
    char lonHem = lonStr[7];
    char latHem = latStr[7];
    
    m_header.lonOrigin = (lonHem == 'W' || lonHem == 'w') ? -lonDeg : lonDeg;
    m_header.latOrigin = (latHem == 'S' || latHem == 's') ? -latDeg : latDeg;
    
    // Parse intervals (bytes 20-23: lon, 24-27: lat) in arc-seconds * 10
    // Note: intervals are expected to be multiples of 10 arc-seconds for exact conversion
    char intervalStr[5] = {0};
    std::memcpy(intervalStr, data + 20, 4);
    m_header.lonInterval = std::atoi(intervalStr) / 10;
    
    std::memcpy(intervalStr, data + 24, 4);
    m_header.latInterval = std::atoi(intervalStr) / 10;
    
    // Parse counts (bytes 47-50: lat points, 51-54: lon points)
    char countStr[5] = {0};
    std::memcpy(countStr, data + 47, 4);
    m_header.numLatPoints = std::atoi(countStr);
    
    std::memcpy(countStr, data + 51, 4);
    m_header.numLonPoints = std::atoi(countStr);
    
    return true;
}

bool DTEDReader::parseDSI(const uint8_t* data) {
    // Verify DSI sentinel
    if (std::strncmp(reinterpret_cast<const char*>(data), "DSI", 3) != 0) {
        std::cerr << "Invalid DSI record sentinel\n";
        return false;
    }
    
    // DSI contains metadata we don't need for basic elevation access
    return true;
}

bool DTEDReader::parseACC(const uint8_t* data) {
    // Verify ACC sentinel
    if (std::strncmp(reinterpret_cast<const char*>(data), "ACC", 3) != 0) {
        std::cerr << "Invalid ACC record sentinel\n";
        return false;
    }
    
    // ACC contains accuracy statistics we don't need for basic elevation access
    return true;
}

bool DTEDReader::parseDataRecords(const uint8_t* data, size_t fileSize) {
    const size_t headerSize = UHL_SIZE + DSI_SIZE + ACC_SIZE;
    
    if (fileSize < headerSize) {
        std::cerr << "File too small to contain DTED data\n";
        return false;
    }
    
    // Allocate elevation array (row-major for easier access)
    const size_t numPoints = static_cast<size_t>(m_header.numLatPoints) * m_header.numLonPoints;
    m_elevations.resize(numPoints, MISSING_DATA);
    
    // DTED data is stored in column-major order (longitude columns)
    // Each column has: 8 byte header + numLatPoints * 2 bytes + 4 byte checksum
    const size_t recordSize = 8 + m_header.numLatPoints * 2 + 4;
    
    const uint8_t* recordPtr = data + headerSize;
    m_header.minElevation = 32767;
    m_header.maxElevation = -32767;
    
    for (int32_t col = 0; col < m_header.numLonPoints; ++col) {
        if (recordPtr + recordSize > data + fileSize) {
            std::cerr << "Unexpected end of file at column " << col << "\n";
            return false;
        }
        
        // Skip 8-byte data record header
        const uint8_t* elevData = recordPtr + 8;
        
        // Read elevation values (big-endian int16)
        for (int32_t row = 0; row < m_header.numLatPoints; ++row) {
            const int16_t elev = readInt16BE(elevData + row * 2);
            
            // Store in row-major order
            const size_t idx = static_cast<size_t>(row) * m_header.numLonPoints + col;
            m_elevations[idx] = elev;
            
            // Update min/max (ignore missing data)
            if (elev != MISSING_DATA) {
                m_header.minElevation = std::min(m_header.minElevation, elev);
                m_header.maxElevation = std::max(m_header.maxElevation, elev);
            }
        }
        
        recordPtr += recordSize;
    }
    
    return true;
}

bool DTEDReader::load(const std::filesystem::path& dtedFile) {
    // Read entire file into memory
    std::ifstream file(dtedFile, std::ios::binary | std::ios::ate);
    if (!file) {
        std::cerr << "Failed to open DTED file: " << dtedFile << "\n";
        return false;
    }
    
    const std::streamsize fileSize = file.tellg();
    if (fileSize <= 0) {
        std::cerr << "Invalid file size\n";
        return false;
    }
    
    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> buffer(static_cast<size_t>(fileSize));
    
    if (!file.read(reinterpret_cast<char*>(buffer.data()), fileSize)) {
        std::cerr << "Failed to read DTED file\n";
        return false;
    }
    
    // Parse records
    if (!parseUHL(buffer.data())) {
        return false;
    }
    
    if (!parseDSI(buffer.data() + UHL_SIZE)) {
        return false;
    }
    
    if (!parseACC(buffer.data() + UHL_SIZE + DSI_SIZE)) {
        return false;
    }
    
    if (!parseDataRecords(buffer.data(), static_cast<size_t>(fileSize))) {
        return false;
    }
    
    std::cout << "Loaded DTED: " << dtedFile.filename() 
              << " (" << m_header.numLonPoints << "x" << m_header.numLatPoints << " posts, "
              << "elevation range: " << m_header.minElevation << " to " << m_header.maxElevation << "m)\n";
    
    return true;
}

int16_t DTEDReader::getElevation(int row, int col) const {
    if (row < 0 || row >= m_header.numLatPoints || col < 0 || col >= m_header.numLonPoints) {
        return MISSING_DATA;
    }
    
    const size_t idx = static_cast<size_t>(row) * m_header.numLonPoints + col;
    return m_elevations[idx];
}

bool DTEDReader::toTexture(Texture& tex) {
    if (!isLoaded()) {
        std::cerr << "No DTED data loaded\n";
        return false;
    }
    
    // Convert elevation data to normalized float32 texture
    // Map elevation range to [0, 1] for displacement
    const float elevRange = static_cast<float>(m_header.maxElevation - m_header.minElevation);
    
    if (elevRange <= 0.0f) {
        std::cerr << "Invalid elevation range\n";
        return false;
    }
    
    // Populate texture structure
    tex.width = m_header.numLonPoints;
    tex.height = m_header.numLatPoints;
    tex.components = 1;  // Single channel (elevation)
    tex.depth = sizeof(float);
    
    tex.texels.resize(tex.width * tex.height * tex.components * tex.depth);
    float* floatData = reinterpret_cast<float*>(tex.texels.data());
    
    for (size_t i = 0; i < m_elevations.size(); ++i) {
        if (m_elevations[i] == MISSING_DATA) {
            floatData[i] = 0.0f;  // Map missing data to sea level
        } else {
            // Normalize to [0, 1]
            floatData[i] = static_cast<float>(m_elevations[i] - m_header.minElevation) / elevRange;
        }
    }
    
    return true;
}

} // namespace dted
