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

#include "SamplerManager.h"
#include "RgException.h"
#include <string>

using namespace RTGL1;

SamplerManager::SamplerManager(VkDevice device)
{
    this->device = device;

    VkSamplerCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    info.mipLodBias = 0;
    info.anisotropyEnable = VK_TRUE;
    info.maxAnisotropy = 8;
    info.compareEnable = VK_FALSE;
    info.minLod = 0.0f;
    info.maxLod = FLT_MAX;
    info.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    info.unnormalizedCoordinates = VK_FALSE;

    VkFilter filters[] = 
    {
        VK_FILTER_NEAREST,
        VK_FILTER_LINEAR
    };

    VkSamplerAddressMode modes[] =
    {
        VK_SAMPLER_ADDRESS_MODE_REPEAT,
        VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT,
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER ,
        VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE
    };

    for (auto filter : filters)
    {
        for (auto modeU : modes)
        {
            for (auto modeV : modes)
            {
                info.minFilter = info.magFilter = filter;
                info.addressModeU = modeU;
                info.addressModeV = modeV;

                uint32_t index = ToIndex(filter, modeU, modeV);
                VkSampler sampler;

                VkResult r = vkCreateSampler(device, &info, nullptr, &sampler);
                VK_CHECKERROR(r);

                assert(samplers.find(index) == samplers.end());

                samplers[index] = sampler;
            }
        }
    }
}

SamplerManager::~SamplerManager()
{
    for (auto &p : samplers)
    {
        vkDestroySampler(device, p.second, nullptr);
    }
}

VkSampler SamplerManager::GetSampler(
    RgSamplerFilter filter, RgSamplerAddressMode addressModeU, RgSamplerAddressMode addressModeV) const
{
    uint32_t index = ToIndex(filter, addressModeU, addressModeV);
    
    auto f = samplers.find(index);

    if (f != samplers.end())
    {
        return f->second;
    }
    else
    {
        throw RgException(RG_WRONG_MATERIAL_PARAMETER, 
                          "Wrong RgSamplerFilter(" + std::to_string(filter) + 
                          ") or RgSamplerAddressMode (U: " + std::to_string(addressModeU) +
                          ", V: " + std::to_string(addressModeV) + ") value");
        return VK_NULL_HANDLE;
    }
}


#define FILTER_LINEAR                           (0 << 0)
#define FILTER_NEAREST                          (1 << 0)
#define FILTER_MASK                             (1 << 0)

#define ADDRESS_MODE_U_REPEAT                   (0 << 1)
#define ADDRESS_MODE_U_MIRRORED_REPEAT          (1 << 1)
#define ADDRESS_MODE_U_CLAMP_TO_EDGE            (2 << 1)
#define ADDRESS_MODE_U_CLAMP_TO_BORDER          (3 << 1)
#define ADDRESS_MODE_U_MIRROR_CLAMP_TO_EDGE     (4 << 1)
#define ADDRESS_MODE_U_MASK                     (7 << 1)

#define ADDRESS_MODE_V_REPEAT                   (0 << 4)
#define ADDRESS_MODE_V_MIRRORED_REPEAT          (1 << 4)
#define ADDRESS_MODE_V_CLAMP_TO_EDGE            (2 << 4)
#define ADDRESS_MODE_V_CLAMP_TO_BORDER          (3 << 4)
#define ADDRESS_MODE_V_MIRROR_CLAMP_TO_EDGE     (4 << 4)
#define ADDRESS_MODE_V_MASK                     (7 << 4)

uint32_t SamplerManager::ToIndex(
    RgSamplerFilter filter, RgSamplerAddressMode addressModeU, RgSamplerAddressMode addressModeV)
{
    uint32_t index = 0;

    switch (filter)
    {
        case RG_SAMPLER_FILTER_NEAREST: index |= FILTER_NEAREST;    break;
        case RG_SAMPLER_FILTER_LINEAR:  index |= FILTER_LINEAR;     break;
    }

    switch (addressModeU)
    {
        case RG_SAMPLER_ADDRESS_MODE_REPEAT:                index |= ADDRESS_MODE_U_REPEAT;                 break;
        case RG_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT:       index |= ADDRESS_MODE_U_MIRRORED_REPEAT;        break;
        case RG_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE:         index |= ADDRESS_MODE_U_CLAMP_TO_EDGE;          break;
        case RG_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER:       index |= ADDRESS_MODE_U_CLAMP_TO_BORDER;        break;
        case RG_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE:  index |= ADDRESS_MODE_U_MIRROR_CLAMP_TO_EDGE;   break;
    }

    switch (addressModeV)
    {
        case RG_SAMPLER_ADDRESS_MODE_REPEAT:                index |= ADDRESS_MODE_V_REPEAT;                 break;
        case RG_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT:       index |= ADDRESS_MODE_V_MIRRORED_REPEAT;        break;
        case RG_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE:         index |= ADDRESS_MODE_V_CLAMP_TO_EDGE;          break;
        case RG_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER:       index |= ADDRESS_MODE_V_CLAMP_TO_BORDER;        break;
        case RG_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE:  index |= ADDRESS_MODE_V_MIRROR_CLAMP_TO_EDGE;   break;
    }

    return index;
}

uint32_t SamplerManager::ToIndex(
    VkFilter filter, VkSamplerAddressMode addressModeU, VkSamplerAddressMode addressModeV)
{
    uint32_t index = 0;

    switch (filter)
    {
        case VK_FILTER_NEAREST: index |= FILTER_NEAREST;    break;
        case VK_FILTER_LINEAR:  index |= FILTER_LINEAR;     break;
        default: break;
    }

    switch (addressModeU)
    {
        case VK_SAMPLER_ADDRESS_MODE_REPEAT:                index |= ADDRESS_MODE_U_REPEAT;                 break;
        case VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT:       index |= ADDRESS_MODE_U_MIRRORED_REPEAT;        break;
        case VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE:         index |= ADDRESS_MODE_U_CLAMP_TO_EDGE;          break;
        case VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER:       index |= ADDRESS_MODE_U_CLAMP_TO_BORDER;        break;
        case VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE:  index |= ADDRESS_MODE_U_MIRROR_CLAMP_TO_EDGE;   break;
        default: break;
    }

    switch (addressModeV)
    {
        case VK_SAMPLER_ADDRESS_MODE_REPEAT:                index |= ADDRESS_MODE_V_REPEAT;                 break;
        case VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT:       index |= ADDRESS_MODE_V_MIRRORED_REPEAT;        break;
        case VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE:         index |= ADDRESS_MODE_V_CLAMP_TO_EDGE;          break;
        case VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER:       index |= ADDRESS_MODE_V_CLAMP_TO_BORDER;        break;
        case VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE:  index |= ADDRESS_MODE_V_MIRROR_CLAMP_TO_EDGE;   break;
        default: break;
    }

    return index;
}
