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

#include "ASManager.h"

#include <array>

#include "Utils.h"
#include "Generated/ShaderCommonC.h"

constexpr bool ONLY_MAIN_TLAS = false;

using namespace RTGL1;

ASManager::ASManager(
    VkDevice _device,
    std::shared_ptr<MemoryAllocator> _allocator,
    std::shared_ptr<CommandBufferManager> _cmdManager,
    std::shared_ptr<TextureManager> _textureManager,
    std::shared_ptr<GeomInfoManager> _geomInfoManager,
    const VertexBufferProperties &_properties)
:
    device(_device),
    allocator(std::move(_allocator)),
    staticCopyFence(VK_NULL_HANDLE),
    cmdManager(std::move(_cmdManager)),
    textureMgr(std::move(_textureManager)),
    geomInfoMgr(std::move(_geomInfoManager)),
    descPool(VK_NULL_HANDLE),
    buffersDescSetLayout(VK_NULL_HANDLE),
    asDescSetLayout(VK_NULL_HANDLE),
    properties(_properties)
{
    typedef VertexCollectorFilterTypeFlags FL;
    typedef VertexCollectorFilterTypeFlagBits FT;


    // init AS structs for each dimension
    VertexCollectorFilterTypeFlags_IterateOverFlags([this] (FL filter)
    {
        if (filter & FT::CF_DYNAMIC)
        {
            for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
            {
                allDynamicBlas[i].emplace_back(std::make_unique<BLASComponent>(device, filter));
            }
        }
        else
        {
            allStaticBlas.emplace_back(std::make_unique<BLASComponent>(device, filter));
        }
    });

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        tlas[i] = std::make_unique<TLASComponent>(device, "TLAS main");
        skyboxTlas[i] = std::make_unique<TLASComponent>(device, "TLAS skybox");
    }


    scratchBuffer = std::make_shared<ScratchBuffer>(allocator);
    asBuilder = std::make_shared<ASBuilder>(device, scratchBuffer);


    // static and movable static vertices share the same buffer as their data won't be changing
    collectorStatic = std::make_shared<VertexCollector>(
        device, allocator, geomInfoMgr,
        sizeof(ShVertexBufferStatic), properties,
        FT::CF_STATIC_NON_MOVABLE | FT::CF_STATIC_MOVABLE | 
        FT::MASK_PASS_THROUGH_GROUP | 
        FT::MASK_PRIMARY_VISIBILITY_GROUP);

    // subscribe to texture manager only static collector,
    // as static geometries aren't updating its material info (in ShGeometryInstance)
    // every frame unlike dynamic ones
    textureMgr->Subscribe(collectorStatic);


    // dynamic vertices
    collectorDynamic[0] = std::make_shared<VertexCollector>(
        device, allocator, geomInfoMgr,
        sizeof(ShVertexBufferDynamic), properties,
        FT::CF_DYNAMIC | 
        FT::MASK_PASS_THROUGH_GROUP | 
        FT::MASK_PRIMARY_VISIBILITY_GROUP);

    // other dynamic vertex collectors should share the same device local buffers as the first one
    for (uint32_t i = 1; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        collectorDynamic[i] = std::make_shared<VertexCollector>(collectorDynamic[0], allocator);
    }

    previousDynamicPositions.Init(
        allocator, sizeof(ShVertexBufferDynamic::positions),
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, 
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, "Previous frame's vertex data");
    previousDynamicIndices.Init(
        allocator, sizeof(ShVertexBufferDynamic::positions),
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, 
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, "Previous frame's index data");


    // instance buffer for TLAS
    instanceBuffer = std::make_unique<AutoBuffer>(device, allocator, "TLAS instance buffer staging", "TLAS instance buffer");

    // multiplying by 2 for main/skybox
    VkDeviceSize instanceBufferSize = 2 * MAX_TOP_LEVEL_INSTANCE_COUNT * sizeof(VkAccelerationStructureInstanceKHR);
    instanceBuffer->Create(instanceBufferSize, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);


    CreateDescriptors();

    // buffers won't be changing, update once
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        UpdateBufferDescriptors(i);
    }


    VkFenceCreateInfo fenceInfo = {};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = 0;
    VkResult r = vkCreateFence(device, &fenceInfo, nullptr, &staticCopyFence);
    VK_CHECKERROR(r);

    SET_DEBUG_NAME(device, staticCopyFence, VK_DEBUG_REPORT_OBJECT_TYPE_FENCE_EXT, "Static BLAS fence");
}

#pragma region AS descriptors

void ASManager::CreateDescriptors()
{
    VkResult r;

    {
        std::array<VkDescriptorSetLayoutBinding, 7> bindings{};

        // static vertex data
        bindings[0].binding = BINDING_VERTEX_BUFFER_STATIC;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = VK_SHADER_STAGE_ALL;

        // dynamic vertex data
        bindings[1].binding = BINDING_VERTEX_BUFFER_DYNAMIC;
        bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = VK_SHADER_STAGE_ALL;

        bindings[2].binding = BINDING_INDEX_BUFFER_STATIC;
        bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[2].descriptorCount = 1;
        bindings[2].stageFlags = VK_SHADER_STAGE_ALL;

        bindings[3].binding = BINDING_INDEX_BUFFER_DYNAMIC;
        bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[3].descriptorCount = 1;
        bindings[3].stageFlags = VK_SHADER_STAGE_ALL;

        bindings[4].binding = BINDING_GEOMETRY_INSTANCES;
        bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[4].descriptorCount = 1;
        bindings[4].stageFlags = VK_SHADER_STAGE_ALL;

        bindings[5].binding = BINDING_PREV_POSITIONS_BUFFER_DYNAMIC;
        bindings[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[5].descriptorCount = 1;
        bindings[5].stageFlags = VK_SHADER_STAGE_ALL;

        bindings[6].binding = BINDING_PREV_INDEX_BUFFER_DYNAMIC;
        bindings[6].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[6].descriptorCount = 1;
        bindings[6].stageFlags = VK_SHADER_STAGE_ALL;

        static_assert(sizeof(bindings) / sizeof(bindings[0]) == 7, "");

        VkDescriptorSetLayoutCreateInfo layoutInfo = {};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = bindings.size();
        layoutInfo.pBindings = bindings.data();

        r = vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &buffersDescSetLayout);
        VK_CHECKERROR(r);
    }

    {
        std::array<VkDescriptorSetLayoutBinding, 2> bindings{};

        bindings[0].binding = BINDING_ACCELERATION_STRUCTURE_MAIN;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

        bindings[1].binding = BINDING_ACCELERATION_STRUCTURE_SKYBOX;
        bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

        VkDescriptorSetLayoutCreateInfo layoutInfo = {};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = ONLY_MAIN_TLAS ? 1 : bindings.size();
        layoutInfo.pBindings = bindings.data();

        r = vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &asDescSetLayout);
        VK_CHECKERROR(r);
    }

    std::array<VkDescriptorPoolSize, 2> poolSizes{};

    poolSizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[0].descriptorCount = MAX_FRAMES_IN_FLIGHT;

    poolSizes[1].type = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    poolSizes[1].descriptorCount = MAX_FRAMES_IN_FLIGHT;

    VkDescriptorPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = poolSizes.size();
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = MAX_FRAMES_IN_FLIGHT * 2;

    r = vkCreateDescriptorPool(device, &poolInfo, nullptr, &descPool);
    VK_CHECKERROR(r);

    SET_DEBUG_NAME(device, descPool, VK_DEBUG_REPORT_OBJECT_TYPE_DESCRIPTOR_POOL_EXT, "AS manager Desc pool");

    VkDescriptorSetAllocateInfo descSetInfo = {};
    descSetInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    descSetInfo.descriptorPool = descPool;
    descSetInfo.descriptorSetCount = 1;

    SET_DEBUG_NAME(device, buffersDescSetLayout, VK_DEBUG_REPORT_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT_EXT, "Vertex data Desc set layout");
    SET_DEBUG_NAME(device, asDescSetLayout, VK_DEBUG_REPORT_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT_EXT, "TLAS Desc set layout");

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        descSetInfo.pSetLayouts = &buffersDescSetLayout;
        r = vkAllocateDescriptorSets(device, &descSetInfo, &buffersDescSets[i]);
        VK_CHECKERROR(r);

        descSetInfo.pSetLayouts = &asDescSetLayout;
        r = vkAllocateDescriptorSets(device, &descSetInfo, &asDescSets[i]);
        VK_CHECKERROR(r);

        SET_DEBUG_NAME(device, buffersDescSets[i], VK_DEBUG_REPORT_OBJECT_TYPE_DESCRIPTOR_SET_EXT, "Vertex data Desc set");
        SET_DEBUG_NAME(device, asDescSets[i], VK_DEBUG_REPORT_OBJECT_TYPE_DESCRIPTOR_SET_EXT, "TLAS Desc set");
    }
}

void ASManager::UpdateBufferDescriptors(uint32_t frameIndex)
{
    const uint32_t bindingCount = 7;

    std::array<VkDescriptorBufferInfo, bindingCount> bufferInfos{};
    std::array<VkWriteDescriptorSet, bindingCount> writes{};

    // buffer infos
    VkDescriptorBufferInfo &stVertsBufInfo = bufferInfos[BINDING_VERTEX_BUFFER_STATIC];
    stVertsBufInfo.buffer = collectorStatic->GetVertexBuffer();
    stVertsBufInfo.offset = 0;
    stVertsBufInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo &dnVertsBufInfo = bufferInfos[BINDING_VERTEX_BUFFER_DYNAMIC];
    dnVertsBufInfo.buffer = collectorDynamic[frameIndex]->GetVertexBuffer();
    dnVertsBufInfo.offset = 0;
    dnVertsBufInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo &stIndexBufInfo = bufferInfos[BINDING_INDEX_BUFFER_STATIC];
    stIndexBufInfo.buffer = collectorStatic->GetIndexBuffer();
    stIndexBufInfo.offset = 0;
    stIndexBufInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo &dnIndexBufInfo = bufferInfos[BINDING_INDEX_BUFFER_DYNAMIC];
    dnIndexBufInfo.buffer = collectorDynamic[frameIndex]->GetIndexBuffer();
    dnIndexBufInfo.offset = 0;
    dnIndexBufInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo &gsBufInfo = bufferInfos[BINDING_GEOMETRY_INSTANCES];
    gsBufInfo.buffer = geomInfoMgr->GetBuffer();
    gsBufInfo.offset = 0;
    gsBufInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo &ppBufInfo = bufferInfos[BINDING_PREV_POSITIONS_BUFFER_DYNAMIC];
    ppBufInfo.buffer = previousDynamicPositions.GetBuffer();
    ppBufInfo.offset = 0;
    ppBufInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo &piBufInfo = bufferInfos[BINDING_PREV_INDEX_BUFFER_DYNAMIC];
    piBufInfo.buffer = previousDynamicIndices.GetBuffer();
    piBufInfo.offset = 0;
    piBufInfo.range = VK_WHOLE_SIZE;


    // writes
    VkWriteDescriptorSet &stVertWrt = writes[BINDING_VERTEX_BUFFER_STATIC];
    stVertWrt.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    stVertWrt.dstSet = buffersDescSets[frameIndex];
    stVertWrt.dstBinding = BINDING_VERTEX_BUFFER_STATIC;
    stVertWrt.dstArrayElement = 0;
    stVertWrt.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    stVertWrt.descriptorCount = 1;
    stVertWrt.pBufferInfo = &stVertsBufInfo;

    VkWriteDescriptorSet &dnVertWrt = writes[BINDING_VERTEX_BUFFER_DYNAMIC];
    dnVertWrt.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    dnVertWrt.dstSet = buffersDescSets[frameIndex];
    dnVertWrt.dstBinding = BINDING_VERTEX_BUFFER_DYNAMIC;
    dnVertWrt.dstArrayElement = 0;
    dnVertWrt.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    dnVertWrt.descriptorCount = 1;
    dnVertWrt.pBufferInfo = &dnVertsBufInfo;

    VkWriteDescriptorSet &stIndexWrt = writes[BINDING_INDEX_BUFFER_STATIC];
    stIndexWrt.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    stIndexWrt.dstSet = buffersDescSets[frameIndex];
    stIndexWrt.dstBinding = BINDING_INDEX_BUFFER_STATIC;
    stIndexWrt.dstArrayElement = 0;
    stIndexWrt.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    stIndexWrt.descriptorCount = 1;
    stIndexWrt.pBufferInfo = &stIndexBufInfo;

    VkWriteDescriptorSet &dnIndexWrt = writes[BINDING_INDEX_BUFFER_DYNAMIC];
    dnIndexWrt.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    dnIndexWrt.dstSet = buffersDescSets[frameIndex];
    dnIndexWrt.dstBinding = BINDING_INDEX_BUFFER_DYNAMIC;
    dnIndexWrt.dstArrayElement = 0;
    dnIndexWrt.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    dnIndexWrt.descriptorCount = 1;
    dnIndexWrt.pBufferInfo = &dnIndexBufInfo;

    VkWriteDescriptorSet &gmWrt = writes[BINDING_GEOMETRY_INSTANCES];
    gmWrt.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    gmWrt.dstSet = buffersDescSets[frameIndex];
    gmWrt.dstBinding = BINDING_GEOMETRY_INSTANCES;
    gmWrt.dstArrayElement = 0;
    gmWrt.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    gmWrt.descriptorCount = 1;
    gmWrt.pBufferInfo = &gsBufInfo;
    
    VkWriteDescriptorSet &ppWrt = writes[BINDING_PREV_POSITIONS_BUFFER_DYNAMIC];
    ppWrt.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    ppWrt.dstSet = buffersDescSets[frameIndex];
    ppWrt.dstBinding = BINDING_PREV_POSITIONS_BUFFER_DYNAMIC;
    ppWrt.dstArrayElement = 0;
    ppWrt.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    ppWrt.descriptorCount = 1;
    ppWrt.pBufferInfo = &ppBufInfo;

    VkWriteDescriptorSet &piWrt = writes[BINDING_PREV_INDEX_BUFFER_DYNAMIC];
    piWrt.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    piWrt.dstSet = buffersDescSets[frameIndex];
    piWrt.dstBinding = BINDING_PREV_INDEX_BUFFER_DYNAMIC;
    piWrt.dstArrayElement = 0;
    piWrt.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    piWrt.descriptorCount = 1;
    piWrt.pBufferInfo = &piBufInfo;

    vkUpdateDescriptorSets(device, writes.size(), writes.data(), 0, nullptr);
}

void ASManager::UpdateASDescriptors(uint32_t frameIndex)
{
    uint32_t bindings[] =
    {
        BINDING_ACCELERATION_STRUCTURE_MAIN,
        BINDING_ACCELERATION_STRUCTURE_SKYBOX,
    };

    TLASComponent *allTLAS[] =
    {
        tlas[frameIndex].get(),
        skyboxTlas[frameIndex].get(),
    };
    constexpr uint32_t allTLASCount = ONLY_MAIN_TLAS ? 1 : sizeof(allTLAS) / sizeof(allTLAS[0]);

    VkAccelerationStructureKHR asHandles[allTLASCount] = {};
    VkWriteDescriptorSetAccelerationStructureKHR asInfos[allTLASCount] = {};
    VkWriteDescriptorSet writes[allTLASCount] = {};

    for (uint32_t i = 0; i < allTLASCount; i++)
    {
        asHandles[i] = allTLAS[i]->GetAS();
        assert(asHandles[i] != VK_NULL_HANDLE);

        VkWriteDescriptorSetAccelerationStructureKHR &asInfo = asInfos[i];
        asInfo.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
        asInfo.accelerationStructureCount = 1;
        asInfo.pAccelerationStructures = &asHandles[i];

        VkWriteDescriptorSet &wrt = writes[i];
        wrt.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        wrt.pNext = &asInfo;
        wrt.dstSet = asDescSets[frameIndex];
        wrt.dstBinding = bindings[i];
        wrt.dstArrayElement = 0;
        wrt.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
        wrt.descriptorCount = 1;
    }

    vkUpdateDescriptorSets(device, allTLASCount, writes, 0, nullptr);
}

#pragma endregion 

ASManager::~ASManager()
{
    for (auto &as : allStaticBlas)
    {
        as->Destroy();
    }

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        for (auto &as : allDynamicBlas[i])
        {
            as->Destroy();
        }

        tlas[i]->Destroy();
        skyboxTlas[i]->Destroy();
    }

    vkDestroyDescriptorPool(device, descPool, nullptr);
    vkDestroyDescriptorSetLayout(device, buffersDescSetLayout, nullptr);
    vkDestroyDescriptorSetLayout(device, asDescSetLayout, nullptr);
    vkDestroyFence(device, staticCopyFence, nullptr);
}

void ASManager::SetupBLAS(BLASComponent &blas, const std::shared_ptr<VertexCollector> &vertCollector)
{
    auto filter = blas.GetFilter();
    const std::vector<VkAccelerationStructureGeometryKHR> &geoms = vertCollector->GetASGeometries(filter);

    blas.SetGeometryCount((uint32_t)geoms.size());

    if (blas.IsEmpty())
    {
        return;
    }

    const std::vector<VkAccelerationStructureBuildRangeInfoKHR> &ranges = vertCollector->GetASBuildRangeInfos(filter);
    const std::vector<uint32_t> &primCounts = vertCollector->GetPrimitiveCounts(filter);

    const bool fastTrace = !IsFastBuild(filter);
    const bool update = false;

    // get AS size and create buffer for AS
    const auto buildSizes = asBuilder->GetBottomBuildSizes(geoms.size(), geoms.data(), primCounts.data(), fastTrace);

    // if no buffer, or it was created, but its size is too small for current AS
    blas.RecreateIfNotValid(buildSizes, allocator);

    assert(blas.GetAS() != VK_NULL_HANDLE);

    // add BLAS, all passed arrays must be alive until BuildBottomLevel() call
    asBuilder->AddBLAS(blas.GetAS(), geoms.size(),
                       geoms.data(), ranges.data(),
                       buildSizes,
                       fastTrace, update);
}

void ASManager::UpdateBLAS(BLASComponent &blas, const std::shared_ptr<VertexCollector> &vertCollector)
{
    auto filter = blas.GetFilter();
    const std::vector<VkAccelerationStructureGeometryKHR> &geoms = vertCollector->GetASGeometries(filter);

    blas.SetGeometryCount((uint32_t)geoms.size());

    if (blas.IsEmpty())
    {
        return;
    }

    const std::vector<VkAccelerationStructureBuildRangeInfoKHR> &ranges = vertCollector->GetASBuildRangeInfos(filter);
    const std::vector<uint32_t> &primCounts = vertCollector->GetPrimitiveCounts(filter);

    const bool fastTrace = !IsFastBuild(filter);

    // must be just updated
    const bool update = true;

    const auto buildSizes = asBuilder->GetBottomBuildSizes(
        geoms.size(), geoms.data(), primCounts.data(), fastTrace);

    assert(blas.IsValid(buildSizes));
    assert(blas.GetAS() != VK_NULL_HANDLE);

    // add BLAS, all passed arrays must be alive until BuildBottomLevel() call
    asBuilder->AddBLAS(blas.GetAS(), geoms.size(),
                       geoms.data(), ranges.data(),
                       buildSizes,
                       fastTrace, update);
}

// separate functions to make adding between Begin..Geometry() and Submit..Geometry() a bit clearer

uint32_t ASManager::AddStaticGeometry(uint32_t frameIndex, const RgGeometryUploadInfo &info)
{
    if (info.geomType == RG_GEOMETRY_TYPE_STATIC || info.geomType == RG_GEOMETRY_TYPE_STATIC_MOVABLE)
    {
        MaterialTextures materials[3] =
        {
            textureMgr->GetMaterialTextures(info.geomMaterial.layerMaterials[0]),
            textureMgr->GetMaterialTextures(info.geomMaterial.layerMaterials[1]),
            textureMgr->GetMaterialTextures(info.geomMaterial.layerMaterials[2])
        };

        return collectorStatic->AddGeometry(frameIndex, info, materials);
    }

    assert(0);
    return UINT32_MAX;
}

uint32_t ASManager::AddDynamicGeometry(uint32_t frameIndex, const RgGeometryUploadInfo &info)
{
    if (info.geomType == RG_GEOMETRY_TYPE_DYNAMIC)
    {
        MaterialTextures materials[3] =
        {
            textureMgr->GetMaterialTextures(info.geomMaterial.layerMaterials[0]),
            textureMgr->GetMaterialTextures(info.geomMaterial.layerMaterials[1]),
            textureMgr->GetMaterialTextures(info.geomMaterial.layerMaterials[2])
        };

        return collectorDynamic[frameIndex]->AddGeometry(frameIndex, info, materials);
    }

    assert(0);
    return UINT32_MAX;
}

void ASManager::ResetStaticGeometry()
{
    collectorStatic->Reset();
    geomInfoMgr->ResetWithStatic();
}

void ASManager::BeginStaticGeometry()
{
    // the whole static vertex data must be recreated, clear previous data
    collectorStatic->Reset();
    geomInfoMgr->ResetWithStatic();

    collectorStatic->BeginCollecting(true);
}

void ASManager::SubmitStaticGeometry()
{
    collectorStatic->EndCollecting();

    typedef VertexCollectorFilterTypeFlagBits FT;

    auto staticFlags = FT::CF_STATIC_NON_MOVABLE | FT::CF_STATIC_MOVABLE;

    // destroy previous static
    for (auto &staticBlas : allStaticBlas)
    {
        assert(!(staticBlas->GetFilter() & FT::CF_DYNAMIC));

        // if flags have any of static bits
        if (staticBlas->GetFilter() & staticFlags)
        {
            staticBlas->Destroy();
            staticBlas->SetGeometryCount(0);
        }
    }

    assert(asBuilder->IsEmpty());

    // skip if all static geometries are empty
    if (collectorStatic->AreGeometriesEmpty(staticFlags))
    {
        return;
    }

    VkCommandBuffer cmd = cmdManager->StartGraphicsCmd();

    // copy from staging with barrier
    collectorStatic->CopyFromStaging(cmd, true);

    // setup static blas
    for (auto &staticBlas : allStaticBlas)
    {
        // if flags have any of static bits
        if (staticBlas->GetFilter() & staticFlags)
        {
            SetupBLAS(*staticBlas, collectorStatic);
        }
    }
    
    // build AS
    asBuilder->BuildBottomLevel(cmd);

    // submit and wait
    cmdManager->Submit(cmd, staticCopyFence);
    Utils::WaitAndResetFence(device, staticCopyFence);
}

void ASManager::BeginDynamicGeometry(uint32_t frameIndex)
{
    // dynamic AS must be recreated
    collectorDynamic[frameIndex]->Reset();
    collectorDynamic[frameIndex]->BeginCollecting(false);
}

void ASManager::SubmitDynamicGeometry(VkCommandBuffer cmd, uint32_t frameIndex)
{
    typedef VertexCollectorFilterTypeFlagBits FT;

    const auto &colDyn = collectorDynamic[frameIndex];

    colDyn->EndCollecting();
    colDyn->CopyFromStaging(cmd, false);

    assert(asBuilder->IsEmpty());

    if (colDyn->AreGeometriesEmpty(FT::CF_DYNAMIC))
    {
        return;
    }

    // recreate dynamic blas
    for (auto &dynamicBlas : allDynamicBlas[frameIndex])
    {
        // must be dynamic
        assert(dynamicBlas->GetFilter() & FT::CF_DYNAMIC);

        SetupBLAS(*dynamicBlas, colDyn);
    }

    // build BLAS
    asBuilder->BuildBottomLevel(cmd);
}

void ASManager::UpdateStaticMovableTransform(uint32_t geomIndex, const RgUpdateTransformInfo &updateInfo)
{
    collectorStatic->UpdateTransform(geomIndex, updateInfo);
}

void RTGL1::ASManager::UpdateStaticTexCoords(uint32_t geomIndex, const RgUpdateTexCoordsInfo &texCoordsInfo)
{
    collectorStatic->UpdateTexCoords(geomIndex, texCoordsInfo);
}

void RTGL1::ASManager::ResubmitStaticTexCoords(VkCommandBuffer cmd)
{
    typedef VertexCollectorFilterTypeFlagBits FT;

    if (collectorStatic->AreGeometriesEmpty(FT::CF_STATIC_NON_MOVABLE | FT::CF_STATIC_MOVABLE))
    {
        return;
    }

    collectorStatic->RecopyTexCoordsFromStaging(cmd);
}

void ASManager::ResubmitStaticMovable(VkCommandBuffer cmd)
{
    typedef VertexCollectorFilterTypeFlagBits FT;

    if (collectorStatic->AreGeometriesEmpty(FT::CF_STATIC_MOVABLE))
    {
        return;
    }

    assert(asBuilder->IsEmpty());

    // update movable blas
    for (auto &blas : allStaticBlas)
    {
        assert(!(blas->GetFilter() & FT::CF_DYNAMIC));

        // if flags have any of static bits
        if (blas->GetFilter() & FT::CF_STATIC_MOVABLE)
        {
            auto &movableBlas = blas;

            UpdateBLAS(*blas, collectorStatic);
        }
    }

    // copy transforms to device-local memory
    collectorStatic->RecopyTransformsFromStaging(cmd);

    asBuilder->BuildBottomLevel(cmd);
}

bool ASManager::SetupTLASInstanceFromBLAS(const BLASComponent &blas, VkAccelerationStructureInstanceKHR &instance)
{
    typedef VertexCollectorFilterTypeFlagBits FT;

    if (blas.GetAS() == VK_NULL_HANDLE || blas.IsEmpty())
    {
        return false;
    }

    auto filter = blas.GetFilter();

    instance.accelerationStructureReference = blas.GetASAddress();

    instance.transform = 
    {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f
    };

    instance.instanceCustomIndex = 0;

    if (filter & FT::CF_DYNAMIC)
    {
        // for choosing buffers with dynamic data
        instance.instanceCustomIndex = INSTANCE_CUSTOM_INDEX_FLAG_DYNAMIC;
    }
    // blended geometry doesn't have indirect illumination

    if (filter & (/*FT::PT_BLEND_ADDITIVE |*/ FT::PT_BLEND_UNDER))
    {
        instance.mask = INSTANCE_MASK_BLENDED;
    }
    else if (filter & FT::PV_FIRST_PERSON)
    {
        instance.mask = INSTANCE_MASK_FIRST_PERSON;
        instance.instanceCustomIndex |= INSTANCE_CUSTOM_INDEX_FLAG_FIRST_PERSON;
    }
    else if (filter & FT::PV_FIRST_PERSON_VIEWER)
    {
        instance.mask = INSTANCE_MASK_FIRST_PERSON_VIEWER;
        instance.instanceCustomIndex |= INSTANCE_CUSTOM_INDEX_FLAG_FIRST_PERSON_VIEWER;
    }
    else if (filter & FT::PV_SKYBOX)
    {
        instance.mask = INSTANCE_MASK_SKYBOX;
        instance.instanceCustomIndex |= INSTANCE_CUSTOM_INDEX_FLAG_SKYBOX;
    }
    else
    {
        instance.mask = INSTANCE_MASK_WORLD;
    }

    if (filter & FT::PT_OPAQUE)
    {
        instance.instanceShaderBindingTableRecordOffset = SBT_INDEX_HITGROUP_FULLY_OPAQUE;
        instance.flags =
            VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR |
            VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
    }
    else 
    {
        if (filter & FT::PT_ALPHA_TESTED)
        {
            instance.instanceShaderBindingTableRecordOffset = SBT_INDEX_HITGROUP_ALPHA_TESTED;
        }
        /*else if (filter &FT::PT_BLEND_ADDITIVE)
        {
            instance.instanceShaderBindingTableRecordOffset = SBT_INDEX_HITGROUP_BLEND_ADDITIVE;
        }*/
        else if (filter & FT::PT_BLEND_UNDER)
        {
            instance.instanceShaderBindingTableRecordOffset = SBT_INDEX_HITGROUP_BLEND_UNDER;
        }
        
        instance.flags =
            VK_GEOMETRY_INSTANCE_FORCE_NO_OPAQUE_BIT_KHR |
            VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
    }

    return true;
}

static void WriteInstanceGeomInfo(int32_t *instanceGeomInfoOffset, int32_t *instanceGeomCount, uint32_t index, const BLASComponent &blas)
{
    assert(index < MAX_TOP_LEVEL_INSTANCE_COUNT);

    uint32_t arrayOffset = VertexCollectorFilterTypeFlags_ToOffset(blas.GetFilter()) * MAX_BOTTOM_LEVEL_GEOMETRIES_COUNT;
    uint32_t geomCount = blas.GetGeomCount();

    // BLAS must not be empty, if it's added to TLAS
    assert(geomCount > 0);

    bool isSkybox = blas.GetFilter() & VertexCollectorFilterTypeFlagBits::PV_SKYBOX;

    if (isSkybox)
    {
        // special offset for skybox, as it's contained in other TLAS
        const uint32_t skyboxStartIndex = MAX_TOP_LEVEL_INSTANCE_COUNT;

        index += skyboxStartIndex;
    }

    instanceGeomInfoOffset[index] = arrayOffset;
    instanceGeomCount[index] = geomCount;
}

bool ASManager::PrepareForBuildingTLAS(
    uint32_t frameIndex,
    const std::shared_ptr<GlobalUniform> &refUniform,
    bool ignoreSkyboxTLAS, 
    ShVertPreprocessing *outPush,
    TLASPrepareResult *outResult)
{
    typedef VertexCollectorFilterTypeFlagBits FT;

    static_assert(sizeof(TLASPrepareResult::instances) / sizeof(TLASPrepareResult::instances[0]) == MAX_TOP_LEVEL_INSTANCE_COUNT, "Change TLASPrepareResult sizes");
    static_assert(sizeof(TLASPrepareResult::skyboxInstances) / sizeof(TLASPrepareResult::skyboxInstances[0]) == MAX_TOP_LEVEL_INSTANCE_COUNT, "Change TLASPrepareResult sizes");


    *outResult = {};
    *outPush = {};


    auto &r = *outResult;


    // write geometry offsets to uniform to access geomInfos
    // with instance ID and local (in terms of BLAS) geometry index in shaders;
    // Note: std140 requires elements to be aligned by sizeof(vec4)
    int32_t *instanceGeomInfoOffset = refUniform->GetData()->instanceGeomInfoOffset;

    // write geometry counts of each BLAS for iterating in vertex preprocessing 
    int32_t *instanceGeomCount = refUniform->GetData()->instanceGeomCount;

    std::vector<std::unique_ptr<BLASComponent>> *blasArrays[] =
    {
        &allStaticBlas,
        &allDynamicBlas[frameIndex],
    };

    for (auto *blasArr : blasArrays)
    {
        for (auto &blas : *blasArr)
        {
            bool isSkybox = blas->GetFilter() & FT::PV_SKYBOX;
            bool isDynamic = blas->GetFilter() & FT::CF_DYNAMIC;

            // add to appropriate TLAS instances array
            if (!isSkybox)
            {
                bool isAdded = ASManager::SetupTLASInstanceFromBLAS(*blas, r.instances[r.instanceCount]);

                if (isAdded)
                {
                    // mark bit if dynamic
                    if (isDynamic)
                    {
                        outPush->tlasInstanceIsDynamicBits[r.instanceCount / MAX_TOP_LEVEL_INSTANCE_COUNT] |= 1 << (r.instanceCount % MAX_TOP_LEVEL_INSTANCE_COUNT);
                    }

                    WriteInstanceGeomInfo(instanceGeomInfoOffset, instanceGeomCount, r.instanceCount, *blas);
                    r.instanceCount++;
                }
            }
            else
            {
                bool isAdded = ASManager::SetupTLASInstanceFromBLAS(*blas, r.skyboxInstances[r.skyboxInstanceCount]);

                if (isAdded)
                {
                    // if skybox TLAS is ignored, skybox geometry must not be previously added
                    assert(!ignoreSkyboxTLAS);
                    
                    // mark bit if dynamic
                    if (isDynamic)
                    {
                        outPush->skyboxTlasInstanceIsDynamicBits[r.skyboxInstanceCount / MAX_TOP_LEVEL_INSTANCE_COUNT] |= 1 << (r.skyboxInstanceCount % MAX_TOP_LEVEL_INSTANCE_COUNT);
                    }

                    WriteInstanceGeomInfo(instanceGeomInfoOffset, instanceGeomCount, r.skyboxInstanceCount, *blas);
                    r.skyboxInstanceCount++;
                }
            }
        }
    }

    if (r.instanceCount == 0 && r.skyboxInstanceCount == 0)
    {
        return false;
    }

    outPush->tlasInstanceCount = r.instanceCount;
    outPush->skyboxTlasInstanceCount = r.skyboxInstanceCount;

    return true;
}

void ASManager::BuildTLAS(VkCommandBuffer cmd, uint32_t frameIndex, const TLASPrepareResult &r)
{
    // fill buffer
    auto *mapped = (VkAccelerationStructureInstanceKHR*)instanceBuffer->GetMapped(frameIndex);

    memcpy(mapped, r.instances, r.instanceCount * sizeof(VkAccelerationStructureInstanceKHR));
    memcpy(mapped + MAX_TOP_LEVEL_INSTANCE_COUNT, r.skyboxInstances, r.skyboxInstanceCount * sizeof(VkAccelerationStructureInstanceKHR));

    instanceBuffer->CopyFromStaging(cmd, frameIndex);


    TLASComponent *allTLAS[] =
    {
        tlas[frameIndex].get(),
        skyboxTlas[frameIndex].get(),
    };
    constexpr uint32_t allTLASCount = ONLY_MAIN_TLAS ? 1 : sizeof(allTLAS) / sizeof(allTLAS[0]);

    VkAccelerationStructureGeometryKHR instGeoms[allTLASCount] = {};
    VkAccelerationStructureBuildSizesInfoKHR buildSizes[allTLASCount] = {};
    VkAccelerationStructureBuildRangeInfoKHR ranges[allTLASCount] = {};

    uint32_t instanceCounts[allTLASCount] =
    {
        r.instanceCount,
        r.skyboxInstanceCount
    };

    for (uint32_t i = 0; i < allTLASCount; i++)
    {
        VkAccelerationStructureGeometryKHR &instGeom = instGeoms[i];

        instGeom.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
        instGeom.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
        instGeom.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
        auto &instData = instGeom.geometry.instances;
        instData.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
        instData.arrayOfPointers = VK_FALSE;
        instData.data.deviceAddress = 
            instanceBuffer->GetDeviceAddress()
            + sizeof(VkAccelerationStructureInstanceKHR) * MAX_TOP_LEVEL_INSTANCE_COUNT * i;

        // get AS size and create buffer for AS
        buildSizes[i] = asBuilder->GetTopBuildSizes(&instGeom, instanceCounts[i], false);

        // if previous buffer's size is not enough
        allTLAS[i]->RecreateIfNotValid(buildSizes[i], allocator);

        ranges[i].primitiveCount = instanceCounts[i];
    }

    uint32_t tlasToBuild = r.skyboxInstanceCount == 0 ? 1 : allTLASCount;

    for (uint32_t i = 0; i < tlasToBuild; i++)
    {
        assert(asBuilder->IsEmpty());

        assert(allTLAS[i]->GetAS() != VK_NULL_HANDLE);
        asBuilder->AddTLAS(allTLAS[i]->GetAS(), &instGeoms[i], &ranges[i], buildSizes[i], true, false);

        asBuilder->BuildTopLevel(cmd);
    }


    UpdateASDescriptors(frameIndex);
}

void ASManager::CopyDynamicDataToPrevBuffers(VkCommandBuffer cmd, uint32_t frameIndex)
{
    uint32_t vertCount = collectorDynamic[frameIndex]->GetCurrentVertexCount();
    uint32_t indexCount = collectorDynamic[frameIndex]->GetCurrentIndexCount();

    VkBufferCopy vertRegion = {};
    vertRegion.srcOffset = 0;
    vertRegion.dstOffset = 0;
    vertRegion.size = (uint64_t)vertCount * properties.positionStride;

    VkBufferCopy indexRegion = {};
    indexRegion.srcOffset = 0;
    indexRegion.dstOffset = 0;
    indexRegion.size = (uint64_t)indexCount * sizeof(uint32_t);

    vkCmdCopyBuffer(
        cmd, 
        collectorDynamic[frameIndex]->GetVertexBuffer(), 
        previousDynamicPositions.GetBuffer(),
        1, &vertRegion);

    vkCmdCopyBuffer(
        cmd, 
        collectorDynamic[frameIndex]->GetIndexBuffer(), 
        previousDynamicIndices.GetBuffer(),
        1, &indexRegion);
}

void ASManager::OnVertexPreprocessingBegin(VkCommandBuffer cmd, uint32_t frameIndex, bool onlyDynamic)
{
    if (!onlyDynamic)
    {
        collectorStatic->InsertVertexPreprocessBeginBarrier(cmd);
    }

    collectorDynamic[frameIndex]->InsertVertexPreprocessBeginBarrier(cmd);
}

void ASManager::OnVertexPreprocessingFinish(VkCommandBuffer cmd, uint32_t frameIndex, bool onlyDynamic)
{
    if (!onlyDynamic)
    {
        collectorStatic->InsertVertexPreprocessFinishBarrier(cmd);
    }

    collectorDynamic[frameIndex]->InsertVertexPreprocessFinishBarrier(cmd);
}

bool ASManager::IsFastBuild(VertexCollectorFilterTypeFlags filter)
{
    typedef VertexCollectorFilterTypeFlagBits FT;

    // fast trace for static
    // fast build for dynamic
    return filter & FT::CF_DYNAMIC;
}

VkDescriptorSet ASManager::GetBuffersDescSet(uint32_t frameIndex) const
{
    return buffersDescSets[frameIndex];
}

VkDescriptorSet ASManager::GetTLASDescSet(uint32_t frameIndex) const
{
    // if TLAS wasn't built, return null
    if (tlas[frameIndex]->GetAS() == VK_NULL_HANDLE)
    {
        return VK_NULL_HANDLE;
    }

    return asDescSets[frameIndex];
}

VkDescriptorSetLayout ASManager::GetBuffersDescSetLayout() const
{
    return buffersDescSetLayout;
}

VkDescriptorSetLayout ASManager::GetTLASDescSetLayout() const
{
    return asDescSetLayout;
}
