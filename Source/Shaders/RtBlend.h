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

#extension GL_EXT_ray_tracing : require

#define DESC_SET_GLOBAL_UNIFORM 2
#define DESC_SET_VERTEX_DATA 3
#define DESC_SET_TEXTURES 4
#include "ShaderCommonGLSLFunc.h"

layout(location = PAYLOAD_INDEX_DEFAULT) rayPayloadInEXT ShPayload payload;
hitAttributeEXT vec2 inBaryCoords;

#ifdef ADDITIVE_BLENDING 
	#define BLEND_FUNC blendAdditive
#else
	#define BLEND_FUNC blendUnder
#endif

void main()
{
	ShTriangle tr = getTriangle(gl_InstanceID, gl_InstanceCustomIndexEXT, gl_GeometryIndexEXT, gl_PrimitiveID);

	vec3 baryCoords = vec3(1.0f - inBaryCoords.x - inBaryCoords.y, inBaryCoords.x, inBaryCoords.y);
    vec2 texCoord = tr.texCoords[0] * baryCoords.x + tr.texCoords[1] * baryCoords.y + tr.texCoords[2] * baryCoords.z;
 
  	vec4 color = getTextureSample(tr.materials[0][0], texCoord) * tr.geomColor;

	float curDistance = gl_HitTEXT;

	if (curDistance > payload.maxTransparDistance)
	{
		// previous is under current
		payload.color = BLEND_FUNC(color, payload.color);
		payload.maxTransparDistance = curDistance;
	}
	else
	{
		// current is under previous
		payload.color = BLEND_FUNC(payload.color, color);
	}

	// blended geometry can't be a closest hit, so ignore this intersection
	ignoreIntersectionEXT;
}