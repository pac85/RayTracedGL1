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

#ifdef DESC_SET_VERTEX_DATA
#ifdef DESC_SET_GLOBAL_UNIFORM
#ifdef DESC_SET_TEXTURES

#ifdef TEXTURE_GRADIENTS
vec3 processAlbedoGrad(uint materialsBlendFlags, const vec2 texCoords[3], const uvec3 materials[3], const vec4 materialColors[3], const vec2 dPdx[3], const vec2 dPdy[3])
#else
vec3 processAlbedo(uint materialsBlendFlags, const vec2 texCoords[3], const uvec3 materials[3], const vec4 materialColors[3], float lod)
#endif
{
    const uint blendsFlags[] = 
    {
        (materialsBlendFlags & MATERIAL_BLENDING_MASK_FIRST_LAYER)  >> (MATERIAL_BLENDING_FLAG_BIT_COUNT * 0),
        (materialsBlendFlags & MATERIAL_BLENDING_MASK_SECOND_LAYER) >> (MATERIAL_BLENDING_FLAG_BIT_COUNT * 1),
        (materialsBlendFlags & MATERIAL_BLENDING_MASK_THIRD_LAYER)  >> (MATERIAL_BLENDING_FLAG_BIT_COUNT * 2)
    };

    vec3 dst = vec3(1.0);

    for (uint i = 0; i < MATERIAL_MAX_ALBEDO_LAYERS; i++)
    {
        if (materials[i][MATERIAL_ALBEDO_ALPHA_INDEX] != MATERIAL_NO_TEXTURE)
        {
        #ifdef TEXTURE_GRADIENTS
            const vec4 src = materialColors[i] * getTextureSampleGrad(materials[i][MATERIAL_ALBEDO_ALPHA_INDEX], texCoords[i], dPdx[i], dPdy[i]);
        #else
            const vec4 src = materialColors[i] * getTextureSampleLod(materials[i][MATERIAL_ALBEDO_ALPHA_INDEX], texCoords[i], lod);
        #endif

            const float opq = float((blendsFlags[i] & MATERIAL_BLENDING_FLAG_OPAQUE) != 0);
            const float alp = float((blendsFlags[i] & MATERIAL_BLENDING_FLAG_ALPHA)  != 0);
            const float add = float((blendsFlags[i] & MATERIAL_BLENDING_FLAG_ADD)    != 0);
            const float shd = float((blendsFlags[i] & MATERIAL_BLENDING_FLAG_SHADE)  != 0);

            // TODO: test this instead of branching

            dst = opq * (src.rgb) +
                  alp * (src.rgb * src.a + dst * (1 - src.a)) + 
                  add * (src.rgb + dst) +
                  shd * (src.rgb * dst * 2);
        }
    }

    return clamp(dst, vec3(0), vec3(1));
}

#ifndef TEXTURE_GRADIENTS
vec3 getHitInfoAlbedoOnly(ShPayload pl) 
{
    int instanceId, instCustomIndex;
    int geomIndex, primIndex;

    unpackInstanceIdAndCustomIndex(pl.instIdAndIndex, instanceId, instCustomIndex);
    unpackGeometryAndPrimitiveIndex(pl.geomAndPrimIndex, geomIndex, primIndex);

    const ShTriangle tr = getTriangle(instanceId, instCustomIndex, geomIndex, primIndex);

    const vec2 inBaryCoords = pl.baryCoords;
    const vec3 baryCoords = vec3(1.0f - inBaryCoords.x - inBaryCoords.y, inBaryCoords.x, inBaryCoords.y);

    const vec2 texCoords[] = 
    {
        tr.layerTexCoord[0] * baryCoords,
        tr.layerTexCoord[1] * baryCoords,
        tr.layerTexCoord[2] * baryCoords
    };
    
    return processAlbedo(tr.materialsBlendFlags, texCoords, tr.materials, tr.materialColors, 0);
}



#define BOUNCE_MIP_BIAS 0

// "Ray Traced Reflections in 'Wolfenstein: Youngblood'", Jiho Choi, Jim Kjellin, Patrik Willbo, Dmitry Zhdan
float getBounceLOD(float roughness, float viewDist, float hitDist, float screenWidth)
{    
    const float range = 300.0 * pow((1.0 - roughness) * 0.9 + 0.1, 4.0);

    vec2 f = vec2(viewDist, hitDist);
    f = clamp(f / range, vec2(0.0), vec2(1.0));
    f = sqrt(f);

    float mip = max(log2(3840.0 / screenWidth), 0.0);

    mip += f.x * 10.0;
    mip += f.y * 10.0;

    return mip + BOUNCE_MIP_BIAS;
}

#endif

#ifdef TEXTURE_GRADIENTS
// Fast, Minimum Storage Ray-Triangle Intersection, Moller, Trumbore
vec3 intersectRayTriangle(const mat3 positions, const vec3 orig, const vec3 dir)
{
    const vec3 edge1 = positions[1] - positions[0];
    const vec3 edge2 = positions[2] - positions[0];

    const vec3 pvec = cross(dir, edge2);

    const float det = dot(edge1, pvec);
    const float invDet = 1.0 / det;

    const vec3 tvec = orig - positions[0];
    const vec3 qvec = cross(tvec, edge1);

    const float u = dot(tvec, pvec) * invDet;
    const float v = dot(dir, qvec) * invDet;

    return vec3(1 - u - v, u, v);
}

ShHitInfo getHitInfoGrad(
    const ShPayload pl, 
    const vec3 rayOrig, const vec3 rayDirAX, const vec3 rayDirAY, 
    out vec2 motion, out float motionDepthLinear, out vec2 gradDepth, out float depthNDC)
#else
ShHitInfo getHitInfoBounce(
    const ShPayload pl, const vec3 originPosition, const float originRoughness, 
    out float hitDistance)
#endif
{
    ShHitInfo h;

    int instanceId, instCustomIndex;
    int geomIndex, primIndex;

    unpackInstanceIdAndCustomIndex(pl.instIdAndIndex, instanceId, instCustomIndex);
    unpackGeometryAndPrimitiveIndex(pl.geomAndPrimIndex, geomIndex, primIndex);

    const ShTriangle tr = getTriangle(instanceId, instCustomIndex, geomIndex, primIndex);

    const vec2 inBaryCoords = pl.baryCoords;
    const vec3 baryCoords = vec3(1.0f - inBaryCoords.x - inBaryCoords.y, inBaryCoords.x, inBaryCoords.y);
    
    const vec2 texCoords[] = 
    {
        tr.layerTexCoord[0] * baryCoords,
        tr.layerTexCoord[1] * baryCoords,
        tr.layerTexCoord[2] * baryCoords
    };
    
    h.hitPosition = tr.positions * baryCoords;

#ifdef TEXTURE_GRADIENTS
    // Tracing Ray Differentials, Igehy

    // instead of casting new rays, check intersections on the same triangle
    const vec3 baryCoordsAX = intersectRayTriangle(tr.positions, rayOrig, rayDirAX);
    const vec3 baryCoordsAY = intersectRayTriangle(tr.positions, rayOrig, rayDirAY);


    const vec4 viewSpacePosCur  = globalUniform.view     * vec4(h.hitPosition, 1.0);
    const vec4 viewSpacePosPrev = globalUniform.viewPrev * vec4(tr.prevPositions * baryCoords, 1.0);
    const vec4 viewSpacePosAX   = globalUniform.view     * vec4(tr.positions     * baryCoordsAX, 1.0);
    const vec4 viewSpacePosAY   = globalUniform.view     * vec4(tr.positions     * baryCoordsAY, 1.0);

    const vec4 clipSpacePosCur  = globalUniform.projection     * viewSpacePosCur;
    const vec4 clipSpacePosPrev = globalUniform.projectionPrev * viewSpacePosPrev;

    const float clipSpaceDepth   = clipSpacePosCur[2];
    const float clipSpaceDepthAX = dot(globalUniform.projection[2], viewSpacePosAX);
    const float clipSpaceDepthAY = dot(globalUniform.projection[2], viewSpacePosAY);

    const vec3 ndcCur  = clipSpacePosCur.xyz  / clipSpacePosCur.w;
    const vec3 ndcPrev = clipSpacePosPrev.xyz / clipSpacePosPrev.w;

    const vec2 screenSpaceCur  = ndcCur.xy * 0.5 + 0.5;
    const vec2 screenSpacePrev = ndcPrev.xy * 0.5 + 0.5;

    depthNDC = ndcCur.z;
    h.linearDepth = length(viewSpacePosCur.xyz);

    // difference in screen-space
    motion = (screenSpacePrev - screenSpaceCur);
    motionDepthLinear = length(viewSpacePosPrev.xyz) - h.linearDepth;
    // gradient of clip-space depth with respect to clip-space coordinates
    gradDepth = vec2(clipSpaceDepthAX - clipSpaceDepth, clipSpaceDepthAY - clipSpaceDepth);


    // pixel's footprint in texture space
    const vec2 dTdx[] = 
    {
        tr.layerTexCoord[0] * baryCoordsAX - texCoords[0],
        tr.layerTexCoord[1] * baryCoordsAX - texCoords[1],
        tr.layerTexCoord[2] * baryCoordsAX - texCoords[2]
    };

    const vec2 dTdy[] = 
    {
        tr.layerTexCoord[0] * baryCoordsAY - texCoords[0],
        tr.layerTexCoord[1] * baryCoordsAY - texCoords[1],
        tr.layerTexCoord[2] * baryCoordsAY - texCoords[2]
    };

    h.albedo = processAlbedoGrad(tr.materialsBlendFlags, texCoords, tr.materials, tr.materialColors, dTdx, dTdy);
#else

    const float viewDist = length(h.hitPosition - globalUniform.cameraPosition.xyz);
    hitDistance = length(h.hitPosition - originPosition);

    const float lod = getBounceLOD(originRoughness, viewDist, hitDistance, globalUniform.renderWidth);

    h.albedo = processAlbedo(tr.materialsBlendFlags, texCoords, tr.materials, tr.materialColors, lod);
#endif

    h.normalGeom = tr.normals * baryCoords;
    const float nLength = length(h.normalGeom);
    if (nLength > 0.001)
    {
        h.normalGeom /= nLength;
    }
    else
    {
        h.normalGeom = vec3(0, 1, 0);
    }

    if (tr.materials[0][MATERIAL_ROUGHNESS_METALLIC_EMISSION_INDEX] != MATERIAL_NO_TEXTURE)
    {
    #ifdef TEXTURE_GRADIENTS
        const vec3 rme = getTextureSampleGrad(tr.materials[0][MATERIAL_ROUGHNESS_METALLIC_EMISSION_INDEX], texCoords[0], dTdx[0], dTdy[0]).xyz;
    #else
        const vec3 rme = getTextureSampleLod(tr.materials[0][MATERIAL_ROUGHNESS_METALLIC_EMISSION_INDEX], texCoords[0], lod).xyz;
    #endif

        h.roughness = rme[0];
        h.metallic  = rme[1];
        h.emission  = rme[2] * h.albedo + tr.geomEmission;
    }
    else
    {
        h.roughness = tr.geomRoughness;
        h.metallic  = tr.geomMetallicity;
        h.emission  = tr.geomEmission;
    }

    if (tr.materials[0][MATERIAL_NORMAL_INDEX] != MATERIAL_NO_TEXTURE)
    {
    #ifdef TEXTURE_GRADIENTS
        vec3 nrm = getTextureSampleGrad(tr.materials[0][MATERIAL_NORMAL_INDEX], texCoords[0], dTdx[0], dTdy[0]).xyz;
    #else
        vec3 nrm = getTextureSampleLod(tr.materials[0][MATERIAL_NORMAL_INDEX], texCoords[0], lod).xyz;
    #endif
    
        nrm = nrm * 2.0 - vec3(1.0);

        const vec3 bitangent = cross(h.normalGeom, tr.tangent);
        h.normal = mat3(tr.tangent, bitangent, h.normalGeom) * nrm;
    }
    else
    {
        h.normal = h.normalGeom;
    }

    h.instCustomIndex = instCustomIndex;

    return h;
}
#endif // DESC_SET_TEXTURES
#endif // DESC_SET_GLOBAL_UNIFORM
#endif // DESC_SET_VERTEX_DATA