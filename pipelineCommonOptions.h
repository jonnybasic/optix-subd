//
// SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
// See LICENSE.txt for license information.
//

#pragma once

#include <optix.h>

struct PipelineCommonOptions
{
    // Compile options we want consistent across all pipelines
    bool usesMotionBlur = false;
    bool allowClusteredGeometry = true;

    // Traversable graph policy for the whole app
    OptixTraversableGraphFlags traversableGraph = OPTIX_TRAVERSABLE_GRAPH_FLAG_ALLOW_SINGLE_LEVEL_INSTANCING;

    // Derived helpers
    int maxTraversalDepth() const
    {
        if (traversableGraph & OPTIX_TRAVERSABLE_GRAPH_FLAG_ALLOW_ANY)
            return 3; // allow any depth
        if (traversableGraph & OPTIX_TRAVERSABLE_GRAPH_FLAG_ALLOW_SINGLE_LEVEL_INSTANCING)
            return 2; // IAS -> GAS
        return 1;      // single GAS only
    }
};


