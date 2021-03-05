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

#include "Scene.h"
#include "Generated/ShaderCommonC.h"

using namespace RTGL1;

Scene::Scene(
    VkDevice _device,
    std::shared_ptr<MemoryAllocator> &_allocator,
    std::shared_ptr<CommandBufferManager> &_cmdManager,
    std::shared_ptr<TextureManager> &_textureManager,
    const std::shared_ptr<const GlobalUniform> &_uniform,
    const std::shared_ptr<const ShaderManager> &_shaderManager,
    const VertexBufferProperties &_properties,
    bool _disableGeometrySkybox)
:
    toResubmitMovable(false),
    isRecordingStatic(false),
    submittedStaticInCurrentFrame(false),
    disableGeometrySkybox(_disableGeometrySkybox)
{
    lightManager = std::make_shared<LightManager>(_device, _allocator);
    geomInfoMgr = std::make_shared<GeomInfoManager>(_device, _allocator);

    asManager = std::make_shared<ASManager>(_device, _allocator, _cmdManager, _textureManager, geomInfoMgr, _properties);
  
    vertPreproc = std::make_shared<VertexPreprocessing>(_device, _uniform, asManager, _shaderManager);
}

Scene::~Scene()
{}

void Scene::PrepareForFrame(uint32_t frameIndex)
{
    geomInfoMgr->PrepareForFrame(frameIndex);

    // dynamic geomtry
    asManager->BeginDynamicGeometry(frameIndex);
}

bool Scene::SubmitForFrame(VkCommandBuffer cmd, uint32_t frameIndex, const std::shared_ptr<GlobalUniform> &uniform)
{
    uint32_t preprocMode = submittedStaticInCurrentFrame ? VERT_PREPROC_MODE_ALL : 
                           toResubmitMovable             ? VERT_PREPROC_MODE_DYNAMIC_AND_MOVABLE : 
                                                           VERT_PREPROC_MODE_ONLY_DYNAMIC;
    submittedStaticInCurrentFrame = false;


    lightManager->CopyFromStaging(cmd, frameIndex);
    lightManager->Clear();


    // copy to device-local, if there were any tex coords change for static geometry
    asManager->ResubmitStaticTexCoords(cmd);

    if (toResubmitMovable)
    {
        // at least one transform of static movable geometry was changed
        asManager->ResubmitStaticMovable(cmd);
        toResubmitMovable = false;
    }

    // always submit dynamic geomtetry on the frame ending
    asManager->SubmitDynamicGeometry(cmd, frameIndex);


    // copy geom infos to device-local
    geomInfoMgr->CopyFromStaging(cmd, frameIndex);
    geomInfoMgr->ResetOnlyDynamic(frameIndex);


    ShVertPreprocessing push = {};
    ASManager::TLASPrepareResult prepare = {};

    // prepare for building and fill uniform data
    bool shouldBeBuilt = asManager->PrepareForBuildingTLAS(frameIndex, uniform, disableGeometrySkybox, &push, &prepare);

    // upload uniform data
    uniform->Upload(cmd, frameIndex);
    
    
    vertPreproc->Preprocess(cmd, frameIndex, preprocMode, uniform, asManager, push);


    if (shouldBeBuilt)
    {
        asManager->BuildTLAS(cmd, frameIndex, prepare);

        // store data of current frame to use it in the next one
        asManager->CopyDynamicDataToPrevBuffers(cmd, frameIndex);

        return true;
    }

    return shouldBeBuilt;
}

uint32_t Scene::Upload(uint32_t frameIndex, const RgGeometryUploadInfo &uploadInfo)
{
    if (disableGeometrySkybox && uploadInfo.visibilityType == RG_GEOMETRY_VISIBILITY_TYPE_SKYBOX)
    {
        return UINT32_MAX;
    }

    if (uploadInfo.geomType == RG_GEOMETRY_TYPE_DYNAMIC)
    {
        return asManager->AddDynamicGeometry(frameIndex, uploadInfo);
    }
    else
    {
        if (!isRecordingStatic)
        {
            asManager->BeginStaticGeometry();
            isRecordingStatic = true;
        }

        uint32_t geomId = asManager->AddStaticGeometry(frameIndex, uploadInfo);

        allStaticGeomIds.push_back(geomId);
        if (uploadInfo.geomType == RG_GEOMETRY_TYPE_STATIC_MOVABLE)
        {
            movableGeomIds.push_back(geomId);
        }

        return geomId;
    }
}

bool Scene::UpdateTransform(uint32_t geomId, const RgUpdateTransformInfo &updateInfo)
{
    // check if it's actually movable
    if (std::find(movableGeomIds.begin(), movableGeomIds.end(), geomId) == movableGeomIds.end())
    {
        // do nothing, if it's not
        return false;
    }

    asManager->UpdateStaticMovableTransform(geomId, updateInfo);

    // if not recording, then static geometries were already submitted,
    // as some movable transform was changed AS must be rebuilt
    if (!isRecordingStatic)
    {
        toResubmitMovable = true;
    }

    return true;
}

bool RTGL1::Scene::UpdateTexCoords(uint32_t geomId, const RgUpdateTexCoordsInfo &texCoordsInfo)
{
    // check if it's static
    if (std::find(allStaticGeomIds.begin(), allStaticGeomIds.end(), geomId) == allStaticGeomIds.end())
    {
        // do nothing, if it's not
        return false;
    }

    asManager->UpdateStaticTexCoords(geomId, texCoordsInfo);
    return true;
}

void Scene::SubmitStatic()
{
    // submit even if nothing was recorded
    if (!isRecordingStatic)
    {
        asManager->BeginStaticGeometry();
    }

    asManager->SubmitStaticGeometry();
    isRecordingStatic = false;

    submittedStaticInCurrentFrame = true;
}

void Scene::StartNewStatic()
{
    // if static geometry wasn't submitted yet
    if (isRecordingStatic)
    {
        // then just reset it
        asManager->ResetStaticGeometry();
    }

    allStaticGeomIds.clear();
    movableGeomIds.clear();
}

const std::shared_ptr<ASManager> &Scene::GetASManager()
{
    return asManager;
}

const std::shared_ptr<LightManager> &RTGL1::Scene::GetLightManager()
{
    return lightManager;
}

void Scene::UploadLight(uint32_t frameIndex, const RgDirectionalLightUploadInfo &lightInfo)
{
    lightManager->AddDirectionalLight(frameIndex, lightInfo);
}

void Scene::UploadLight(uint32_t frameIndex, const RgSphericalLightUploadInfo &lightInfo)
{
    lightManager->AddSphericalLight(frameIndex, lightInfo);
}
