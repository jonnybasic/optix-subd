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

#include "ellipsoid.h"
#include "shapeUtils.h"
#include "vertex.h"

#include <cmath>
#include <algorithm>

namespace ellipsoid {

namespace {
    constexpr double PI = 3.14159265358979323846264338327950288;
    
    // Convert degrees to radians
    inline double degToRad(double degrees) {
        return degrees * PI / 180.0;
    }
}

void geographicToCartesian(double lat, double lon, double height,
                          const EllipsoidConfig& config,
                          double& x, double& y, double& z)
{
    const double latRad = degToRad(lat);
    const double lonRad = degToRad(lon);
    
    const double a = config.semiMajorAxis;
    const double b = config.semiMinorAxis;
    
    // Compute ellipsoidal coordinates with height
    const double e2 = 1.0 - (b * b) / (a * a);  // First eccentricity squared
    const double N = a / std::sqrt(1.0 - e2 * std::sin(latRad) * std::sin(latRad));
    
    const double cosLat = std::cos(latRad);
    const double sinLat = std::sin(latRad);
    const double cosLon = std::cos(lonRad);
    const double sinLon = std::sin(lonRad);
    
    x = (N + height) * cosLat * cosLon;
    y = (N + height) * cosLat * sinLon;
    z = (N * (1.0 - e2) + height) * sinLat;
}

void computeEllipsoidNormal(double lat, double lon,
                           const EllipsoidConfig& config,
                           double& nx, double& ny, double& nz)
{
    const double latRad = degToRad(lat);
    const double lonRad = degToRad(lon);
    
    const double a = config.semiMajorAxis;
    const double b = config.semiMinorAxis;
    
    const double cosLat = std::cos(latRad);
    const double sinLat = std::sin(latRad);
    const double cosLon = std::cos(lonRad);
    const double sinLon = std::sin(lonRad);
    
    // Ellipsoidal surface normal (not unit sphere normal)
    nx = cosLat * cosLon / (a * a);
    ny = cosLat * sinLon / (a * a);
    nz = sinLat / (b * b);
    
    // Normalize
    const double len = std::sqrt(nx * nx + ny * ny + nz * nz);
    nx /= len;
    ny /= len;
    nz /= len;
}

std::unique_ptr<Shape> generateEllipsoidMesh(const EllipsoidConfig& config)
{
    auto shape = std::make_unique<Shape>();
    
    shape->scheme = kCatmark;  // Catmull-Clark subdivision
    shape->isLeftHanded = false;
    
    const int numLon = config.longitudeSegments;
    const int numLat = config.latitudeSegments;
    
    // Generate vertices with UV coordinates
    // Special handling for poles to avoid degenerate faces
    const int numVerts = (numLat - 1) * numLon + 2;  // +2 for north and south poles
    shape->verts.reserve(numVerts);
    shape->uvs.reserve(numVerts);
    
    // North pole
    {
        double x, y, z;
        geographicToCartesian(90.0, 0.0, 0.0, config, x, y, z);
        shape->verts.push_back({static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)});
        shape->uvs.push_back({0.5f, 1.0f});  // Center top of UV map
    }
    
    // Latitude rings (excluding poles)
    for (int iLat = 1; iLat < numLat; ++iLat) {
        const double v = static_cast<double>(iLat) / numLat;
        const double lat = 90.0 - v * 180.0;  // 90° to -90°
        
        for (int iLon = 0; iLon < numLon; ++iLon) {
            const double u = static_cast<double>(iLon) / numLon;
            const double lon = -180.0 + u * 360.0;  // -180° to 180°
            
            double x, y, z;
            geographicToCartesian(lat, lon, 0.0, config, x, y, z);
            
            shape->verts.push_back({static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)});
            shape->uvs.push_back({static_cast<float>(u), static_cast<float>(v)});
        }
    }
    
    // South pole
    {
        double x, y, z;
        geographicToCartesian(-90.0, 0.0, 0.0, config, x, y, z);
        shape->verts.push_back({static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)});
        shape->uvs.push_back({0.5f, 0.0f});  // Center bottom of UV map
    }
    
    // Generate quad faces
    if (config.generateQuads) {
        // North polar cap (triangles converted to quads by duplicating a vertex)
        for (int iLon = 0; iLon < numLon; ++iLon) {
            const int next = (iLon + 1) % numLon;
            
            shape->nvertsPerFace.push_back(3);  // Triangle for pole
            shape->faceverts.push_back(0);       // North pole
            shape->faceverts.push_back(1 + iLon);
            shape->faceverts.push_back(1 + next);
            
            shape->faceuvs.push_back(0);
            shape->faceuvs.push_back(1 + iLon);
            shape->faceuvs.push_back(1 + next);
        }
        
        // Middle latitude quads
        for (int iLat = 1; iLat < numLat - 1; ++iLat) {
            const int baseIdx = 1 + (iLat - 1) * numLon;
            const int nextBaseIdx = 1 + iLat * numLon;
            
            for (int iLon = 0; iLon < numLon; ++iLon) {
                const int next = (iLon + 1) % numLon;
                
                shape->nvertsPerFace.push_back(4);  // Quad
                shape->faceverts.push_back(baseIdx + iLon);
                shape->faceverts.push_back(baseIdx + next);
                shape->faceverts.push_back(nextBaseIdx + next);
                shape->faceverts.push_back(nextBaseIdx + iLon);
                
                shape->faceuvs.push_back(baseIdx + iLon);
                shape->faceuvs.push_back(baseIdx + next);
                shape->faceuvs.push_back(nextBaseIdx + next);
                shape->faceuvs.push_back(nextBaseIdx + iLon);
            }
        }
        
        // South polar cap
        const int southPoleIdx = numVerts - 1;
        const int lastRingBase = 1 + (numLat - 2) * numLon;
        
        for (int iLon = 0; iLon < numLon; ++iLon) {
            const int next = (iLon + 1) % numLon;
            
            shape->nvertsPerFace.push_back(3);  // Triangle for pole
            shape->faceverts.push_back(lastRingBase + iLon);
            shape->faceverts.push_back(southPoleIdx);
            shape->faceverts.push_back(lastRingBase + next);
            
            shape->faceuvs.push_back(lastRingBase + iLon);
            shape->faceuvs.push_back(southPoleIdx);
            shape->faceuvs.push_back(lastRingBase + next);
        }
    }
    
    // Compute AABB
    shape->aabb.invalidate();
    for (const auto& v : shape->verts) {
        shape->aabb.include({v.point.x, v.point.y, v.point.z});
    }
    
    // Create a default material
    auto defaultMat = std::make_unique<Shape::material>();
    defaultMat->name = "ellipsoid_default";
    defaultMat->kd[0] = 0.7f;
    defaultMat->kd[1] = 0.7f;
    defaultMat->kd[2] = 0.7f;
    defaultMat->Pr = 0.5f;  // Medium roughness
    defaultMat->Pm = 0.0f;  // Non-metallic
    shape->mtls.push_back(std::move(defaultMat));
    
    // Bind all faces to the default material
    shape->mtlbind.resize(shape->nvertsPerFace.size(), 0);
    
    return shape;
}

} // namespace ellipsoid
