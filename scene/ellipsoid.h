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

#include <memory>
#include <string>

struct Shape;

namespace ellipsoid {

// WGS84 ellipsoid constants (in meters)
constexpr double WGS84_SEMI_MAJOR_AXIS = 6378137.0;        // a - equatorial radius
constexpr double WGS84_SEMI_MINOR_AXIS = 6356752.314245;   // b - polar radius
constexpr double WGS84_FLATTENING = 1.0 / 298.257223563;   // f = (a-b)/a

/**
 * @brief Configuration for ellipsoid mesh generation
 */
struct EllipsoidConfig {
    double semiMajorAxis = WGS84_SEMI_MAJOR_AXIS;  // Equatorial radius (meters)
    double semiMinorAxis = WGS84_SEMI_MINOR_AXIS;  // Polar radius (meters)
    int longitudeSegments = 64;                      // Number of longitude divisions
    int latitudeSegments = 32;                       // Number of latitude divisions
    bool generateQuads = true;                       // true=quads for Catmull-Clark, false=triangles
};

/**
 * @brief Generate a WGS84 ellipsoid mesh suitable for subdivision surfaces
 * 
 * Creates a parametric ellipsoid with UV coordinates mapping to lat/lon:
 * - U maps to longitude: [0,1] -> [-180°, 180°]
 * - V maps to latitude:  [0,1] -> [-90°, 90°]
 * 
 * The mesh topology is quad-based for optimal Catmull-Clark subdivision.
 * Normals are computed as ellipsoidal surface normals (not sphere normals).
 * 
 * @param config Configuration parameters for mesh generation
 * @return std::unique_ptr<Shape> Generated mesh in Shape format
 */
std::unique_ptr<Shape> generateEllipsoidMesh(const EllipsoidConfig& config = EllipsoidConfig());

/**
 * @brief Convert geographic coordinates to ellipsoidal Cartesian coordinates
 * 
 * @param lat Latitude in degrees [-90, 90]
 * @param lon Longitude in degrees [-180, 180]
 * @param height Height above ellipsoid in meters (default 0)
 * @param config Ellipsoid parameters
 * @param[out] x Cartesian X coordinate
 * @param[out] y Cartesian Y coordinate
 * @param[out] z Cartesian Z coordinate
 */
void geographicToCartesian(double lat, double lon, double height,
                          const EllipsoidConfig& config,
                          double& x, double& y, double& z);

/**
 * @brief Compute ellipsoidal surface normal at a given geographic location
 * 
 * @param lat Latitude in degrees [-90, 90]
 * @param lon Longitude in degrees [-180, 180]
 * @param config Ellipsoid parameters
 * @param[out] nx Normal X component
 * @param[out] ny Normal Y component
 * @param[out] nz Normal Z component
 */
void computeEllipsoidNormal(double lat, double lon,
                           const EllipsoidConfig& config,
                           double& nx, double& ny, double& nz);

} // namespace ellipsoid
