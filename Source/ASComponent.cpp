// Copyright (c) 2021 Sultim Tsyrendashiev
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

#include "ASComponent.h"

RTGL1::ASComponent::ASComponent(VkDevice _device)
:
    device(_device),
    as(VK_NULL_HANDLE),
    buffer{},
    isEmpty(true)
{}

RTGL1::BLASComponent::BLASComponent(VkDevice _device, VertexCollectorFilterTypeFlags _filter)
:
    ASComponent(_device),
    filter(_filter)
{}

RTGL1::TLASComponent::TLASComponent(VkDevice _device)
: 
    ASComponent(_device)
{}

RTGL1::ASComponent::~ASComponent()
{
    Destroy();
}

void RTGL1::ASComponent::CreateBuffer(const std::shared_ptr<MemoryAllocator> &allocator, VkDeviceSize size, const char *debugName)
{
    assert(!buffer.IsInitted());

    buffer.Init(
        allocator, size,
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        debugName
    );
}

void RTGL1::ASComponent::Destroy()
{
    assert(device != VK_NULL_HANDLE);

    isEmpty = true;
    buffer.Destroy();

    if (as != VK_NULL_HANDLE)
    {
        svkDestroyAccelerationStructureKHR(device, as, nullptr);
        as = VK_NULL_HANDLE;
    }
}

void RTGL1::ASComponent::RegisterGeometries(const std::vector<VkAccelerationStructureGeometryKHR> &geoms)
{
    isEmpty = geoms.empty();
}

void RTGL1::ASComponent::RecreateIfNotValid(const VkAccelerationStructureBuildSizesInfoKHR &buildSizes, const std::shared_ptr<MemoryAllocator> &allocator)
{
    if (!IsValid(buildSizes))
    {
        // destroy
        Destroy();

        // create
        CreateBuffer(allocator, buildSizes.accelerationStructureSize, GetBufferDebugName());
        CreateAS(buildSizes.accelerationStructureSize);

        isEmpty = false;
    }
}

void RTGL1::BLASComponent::CreateAS(VkDeviceSize size)
{
    assert(device != VK_NULL_HANDLE);

    VkAccelerationStructureCreateInfoKHR info = {};
    info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    info.size = size;
    info.buffer = buffer.GetBuffer();

    VkResult r = svkCreateAccelerationStructureKHR(device, &info, nullptr, &as);
    VK_CHECKERROR(r);

    const char *debugName = VertexCollectorFilterTypeFlags_GetNameForBLAS(filter);
    SET_DEBUG_NAME(device, as, VK_DEBUG_REPORT_OBJECT_TYPE_ACCELERATION_STRUCTURE_KHR_EXT, debugName);
}

void RTGL1::TLASComponent::CreateAS(VkDeviceSize size)
{
    assert(device != VK_NULL_HANDLE);

    VkAccelerationStructureCreateInfoKHR tlasInfo = {};
    tlasInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    tlasInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    tlasInfo.size = size;
    tlasInfo.buffer = buffer.GetBuffer();

    VkResult r = svkCreateAccelerationStructureKHR(device, &tlasInfo, nullptr, &as);
    VK_CHECKERROR(r);

    SET_DEBUG_NAME(device, as, VK_DEBUG_REPORT_OBJECT_TYPE_ACCELERATION_STRUCTURE_KHR_EXT, "TLAS");
}

bool RTGL1::ASComponent::IsValid(const VkAccelerationStructureBuildSizesInfoKHR &buildSizes) const
{
    return buffer.IsInitted() && buffer.GetSize() >= buildSizes.accelerationStructureSize;
}

bool RTGL1::ASComponent::IsEmpty() const
{
    return isEmpty;
}

VkAccelerationStructureKHR RTGL1::ASComponent::GetAS() const
{
    return as;
}

VkDeviceAddress RTGL1::ASComponent::GetASAddress() const
{
    assert(buffer.IsInitted());
    return GetASAddress(as);
}

VkDeviceAddress RTGL1::ASComponent::GetASAddress(VkAccelerationStructureKHR as) const
{
    assert(device != VK_NULL_HANDLE);
    assert(as != VK_NULL_HANDLE);

    VkAccelerationStructureDeviceAddressInfoKHR addressInfo = {};
    addressInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    addressInfo.accelerationStructure = as;

    return svkGetAccelerationStructureDeviceAddressKHR(device, &addressInfo);
}

const char *RTGL1::BLASComponent::GetBufferDebugName() const
{
    return "BLAS buffer";
}

const char *RTGL1::TLASComponent::GetBufferDebugName() const
{
    return "TLAS buffer";
}

RTGL1::VertexCollectorFilterTypeFlags RTGL1::BLASComponent::GetFilter() const
{
    return filter;
}