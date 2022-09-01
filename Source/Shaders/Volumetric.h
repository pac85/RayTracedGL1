// Copyright (c) 2022 Sultim Tsyrendashiev
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

#ifndef VOLUMETRIC_H_
#define VOLUMETRIC_H_

#if !defined( DESC_SET_GLOBAL_UNIFORM ) || !defined( DESC_SET_VOLUMETRIC )
    #error
#endif


vec3 volume_getCenter(const ivec3 cell)
{
    vec3 local = 
        ( vec3( cell ) + 0.5 ) 
        / vec3( VOLUMETRIC_SIZE_X, VOLUMETRIC_SIZE_Y, VOLUMETRIC_SIZE_Z );

    vec4 ndc = {
        local.x * 2.0 - 1.0,
        local.y * 2.0 - 1.0,
        local.z,
        1.0,
    };

    vec4 world = globalUniform.invView * globalUniform.invProjection * ndc;
    return world.xyz / world.w;
}

vec3 volume_toSamplePosition( const vec3 world )
{
    vec4 ndc = globalUniform.projection * globalUniform.view * vec4( world, 1.0 );
    ndc.xyz /= ndc.w;

    vec3 local = {
        ( ndc.x + 1.0 ) * 0.5,
        ( ndc.y + 1.0 ) * 0.5,
        ndc.z,
    };

    return local;
}

vec4 volume_sample( const vec3 world ) 
{
    return textureLod( g_volumetric_Sampler, volume_toSamplePosition( world ), 0.0 );
}

ivec3 volume_toCellIndex( const vec3 world )
{
    return ivec3( volume_toSamplePosition( world ) );
}


#endif // VOLUMETRIC_H_