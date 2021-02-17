// Copyright (c) 2020-2021 Sultim Tsyrendashiev
// 
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#pragma once

#include "RTGL1/RTGL1.h"

#include <functional>

namespace RTGL1
{

enum class VertexCollectorFilterTypeFlagBits : uint32_t
{
    NONE                        = 0,

    CF_STATIC_NON_MOVABLE       = 1 << 0,
    CF_STATIC_MOVABLE           = 2 << 0,
    CF_DYNAMIC                  = 4 << 0,
    MASK_CHANGE_FREQUENCY_GROUP = CF_STATIC_NON_MOVABLE | CF_STATIC_MOVABLE | CF_DYNAMIC,

    PT_OPAQUE                   = 1 << 3,
    PT_ALPHA_TESTED             = 2 << 3,
    /*PT_BLEND_ADDITIVE           = 4 << 3,*/
    PT_BLEND_UNDER              = 4 << 3,
    MASK_PASS_THROUGH_GROUP     = PT_OPAQUE | PT_ALPHA_TESTED | /*PT_BLEND_ADDITIVE |*/ PT_BLEND_UNDER,

    PV_WORLD                    = 1 << 6,
    PV_FIRST_PERSON             = 2 << 6,
    PV_FIRST_PERSON_VIEWER      = 4 << 6,
    PV_SKYBOX                   = 8 << 6,
    MASK_PRIMARY_VISIBILITY_GROUP = PV_WORLD | PV_FIRST_PERSON | PV_FIRST_PERSON_VIEWER | PV_SKYBOX,
};
typedef uint32_t VertexCollectorFilterTypeFlags;

inline VertexCollectorFilterTypeFlags operator|(VertexCollectorFilterTypeFlagBits a, VertexCollectorFilterTypeFlagBits b)
{
    typedef VertexCollectorFilterTypeFlags FL;
    return static_cast<FL>(static_cast<FL>(a) | static_cast<FL>(b));
}

inline VertexCollectorFilterTypeFlags operator|(VertexCollectorFilterTypeFlags a, VertexCollectorFilterTypeFlagBits b)
{
    typedef VertexCollectorFilterTypeFlags FL;
    return static_cast<FL>(a | static_cast<FL>(b));
}

inline VertexCollectorFilterTypeFlags operator|(VertexCollectorFilterTypeFlagBits a, VertexCollectorFilterTypeFlags b)
{
    typedef VertexCollectorFilterTypeFlags FL;
    return static_cast<FL>(static_cast<FL>(a) | b);
}

inline VertexCollectorFilterTypeFlags operator&(VertexCollectorFilterTypeFlagBits a, VertexCollectorFilterTypeFlagBits b)
{
    typedef VertexCollectorFilterTypeFlags FL;
    return static_cast<FL>(static_cast<FL>(a) & static_cast<FL>(b));
}

inline VertexCollectorFilterTypeFlags operator&(VertexCollectorFilterTypeFlags a, VertexCollectorFilterTypeFlagBits b)
{
    typedef VertexCollectorFilterTypeFlags FL;
    return static_cast<FL>(a & static_cast<FL>(b));
}

inline VertexCollectorFilterTypeFlags operator&(VertexCollectorFilterTypeFlagBits a, VertexCollectorFilterTypeFlags b)
{
    typedef VertexCollectorFilterTypeFlags FL;
    return static_cast<FL>(static_cast<FL>(a) & b);
}

uint32_t                        VertexCollectorFilterTypeFlags_ToOffset(VertexCollectorFilterTypeFlags flags);
const char*                     VertexCollectorFilterTypeFlags_GetNameForBLAS(VertexCollectorFilterTypeFlags flags);
VertexCollectorFilterTypeFlags  VertexCollectorFilterTypeFlags_GetForGeometry(const RgGeometryUploadInfo &info);
void                            VertexCollectorFilterTypeFlags_IterateOverFlags(std::function<void(VertexCollectorFilterTypeFlags)> f);

}