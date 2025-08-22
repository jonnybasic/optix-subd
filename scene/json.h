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

// clang-format off
#include <OptiXToolkit/ShaderUtil/vec_math.h>
#include <OptiXToolkit/ShaderUtil/Quaternion.h>

#include <cassert>
#include <filesystem>
#include <string>
#include <fstream>

#include <nlohmann/json.hpp>

namespace nlohmann
{
    // These helper functions let us call node.value() and get back CUDA vector types
    template <>
    struct adl_serializer<float3>
    {
        static void to_json( nlohmann::json& j, const float3& val )
        {
            j = {val.x, val.y, val.z};
        }

        static void from_json( const nlohmann::json& j, float3& val )
        {
            if( j.is_array() && j.size() == 3 )
            {
                j.at( 0 ).get_to( val.x );
                j.at( 1 ).get_to( val.y );
                j.at( 2 ).get_to( val.z );
            }
            else if( j.is_number() )
            {
                const float v = j.get<float>();
                val = make_float3( v );
            }
        }
    };

    template <>
    struct adl_serializer<float2>
    {
        static void to_json( nlohmann::json& j, const float2& val )
        {
            j = {val.x, val.y};
        }

        static void from_json( const nlohmann::json& j, float2& val )
        {
            if( j.is_array() && j.size() == 2 )
            {
                j.at( 0 ).get_to( val.x );
                j.at( 1 ).get_to( val.y );
            }
            else if( j.is_number() )
            {
                const float v = j.get<float>();
                val = make_float2( v );
            }
        }
    };

    template <>
    struct adl_serializer<float4>
    {
        static void to_json( nlohmann::json& j, const float4& val )
        {
            j = {val.x, val.y, val.z, val.w};
        }

        static void from_json( const nlohmann::json& j, float4& val )
        {
            if( j.is_array() && j.size() == 4 )
            {
                j.at( 0 ).get_to( val.x );
                j.at( 1 ).get_to( val.y );
                j.at( 2 ).get_to( val.z );
                j.at( 3 ).get_to( val.w );
            }
            else if( j.is_number() )
            {
                const float v = j.get<float>();
                val = make_float4( v );
            }
        }
    };

    template <>
    struct adl_serializer<int2>
    {
        static void from_json( const nlohmann::json& j, int2& val )
        {
            if( j.is_array() && j.size() == 2 )
            {
                j.at( 0 ).get_to( val.x );
                j.at( 1 ).get_to( val.y );
            }
        }
    };

    template <>
    struct adl_serializer<uint2>
    {
        static void from_json( const nlohmann::json& j, uint2& val )
        {
            if( j.is_array() && j.size() == 2 )
            {
                j.at( 0 ).get_to( val.x );
                j.at( 1 ).get_to( val.y );
            }
        }
    };

    template <>
    struct adl_serializer<otk::quat>
    {
        static void to_json( nlohmann::json& j, const otk::quat& val )
        {
            j = {val.w, val.x, val.y, val.z};
        }

        static void from_json( const nlohmann::json& j, otk::quat& val )
        {
            if( j.is_array() && j.size() == 4 )
            {
                j.at( 0 ).get_to( val.w );
                j.at( 1 ).get_to( val.x );
                j.at( 2 ).get_to( val.y );
                j.at( 3 ).get_to( val.z );
            }
        }
    };
}  // namespace nlohmann

namespace json = nlohmann;


inline json::json readFile( const std::filesystem::path& filepath )
{
    namespace fs = std::filesystem;
    std::string   fp  = filepath.generic_string();
    std::ifstream ifs( fp );
    if( !ifs )
        throw std::runtime_error( std::string( "Cannot find: " ) + fp );

    json::json root = json::json::parse( ifs, nullptr, true, true );
    if( root.is_null() )
        throw std::runtime_error( "error reading '" + fp + "'" );

    return root;
}

// clang-format on
