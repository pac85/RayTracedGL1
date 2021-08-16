// This file was generated by GenerateShaderCommon.py

#pragma once

#include "../Common.h"

namespace RTGL1
{

#define FB_SAMPLER_INVALID_BINDING 0xFFFFFFFF

enum FramebufferImageIndex
{
    FB_IMAGE_INDEX_ALBEDO = 0,
    FB_IMAGE_INDEX_NORMAL = 1,
    FB_IMAGE_INDEX_NORMAL_PREV = 2,
    FB_IMAGE_INDEX_NORMAL_GEOMETRY = 3,
    FB_IMAGE_INDEX_NORMAL_GEOMETRY_PREV = 4,
    FB_IMAGE_INDEX_METALLIC_ROUGHNESS = 5,
    FB_IMAGE_INDEX_METALLIC_ROUGHNESS_PREV = 6,
    FB_IMAGE_INDEX_DEPTH = 7,
    FB_IMAGE_INDEX_DEPTH_PREV = 8,
    FB_IMAGE_INDEX_RANDOM_SEED = 9,
    FB_IMAGE_INDEX_RANDOM_SEED_PREV = 10,
    FB_IMAGE_INDEX_UNFILTERED_DIRECT = 11,
    FB_IMAGE_INDEX_UNFILTERED_SPECULAR = 12,
    FB_IMAGE_INDEX_UNFILTERED_INDIRECT_S_H_R = 13,
    FB_IMAGE_INDEX_UNFILTERED_INDIRECT_S_H_G = 14,
    FB_IMAGE_INDEX_UNFILTERED_INDIRECT_S_H_B = 15,
    FB_IMAGE_INDEX_SURFACE_POSITION = 16,
    FB_IMAGE_INDEX_VISIBILITY_BUFFER = 17,
    FB_IMAGE_INDEX_VISIBILITY_BUFFER_PREV = 18,
    FB_IMAGE_INDEX_VIEW_DIRECTION = 19,
    FB_IMAGE_INDEX_THROUGHPUT = 20,
    FB_IMAGE_INDEX_PRE_FINAL = 21,
    FB_IMAGE_INDEX_FINAL = 22,
    FB_IMAGE_INDEX_MOTION = 23,
    FB_IMAGE_INDEX_ACCUM_HISTORY_LENGTH = 24,
    FB_IMAGE_INDEX_ACCUM_HISTORY_LENGTH_PREV = 25,
    FB_IMAGE_INDEX_DIFF_ACCUM_COLOR = 26,
    FB_IMAGE_INDEX_DIFF_ACCUM_COLOR_PREV = 27,
    FB_IMAGE_INDEX_DIFF_ACCUM_MOMENTS = 28,
    FB_IMAGE_INDEX_DIFF_ACCUM_MOMENTS_PREV = 29,
    FB_IMAGE_INDEX_DIFF_COLOR_HISTORY = 30,
    FB_IMAGE_INDEX_DIFF_PING_COLOR_AND_VARIANCE = 31,
    FB_IMAGE_INDEX_DIFF_PONG_COLOR_AND_VARIANCE = 32,
    FB_IMAGE_INDEX_SPEC_ACCUM_COLOR = 33,
    FB_IMAGE_INDEX_SPEC_ACCUM_COLOR_PREV = 34,
    FB_IMAGE_INDEX_SPEC_PING_COLOR = 35,
    FB_IMAGE_INDEX_SPEC_PONG_COLOR = 36,
    FB_IMAGE_INDEX_INDIR_ACCUM_S_H_R = 37,
    FB_IMAGE_INDEX_INDIR_ACCUM_S_H_R_PREV = 38,
    FB_IMAGE_INDEX_INDIR_ACCUM_S_H_G = 39,
    FB_IMAGE_INDEX_INDIR_ACCUM_S_H_G_PREV = 40,
    FB_IMAGE_INDEX_INDIR_ACCUM_S_H_B = 41,
    FB_IMAGE_INDEX_INDIR_ACCUM_S_H_B_PREV = 42,
    FB_IMAGE_INDEX_INDIR_PING_S_H_R = 43,
    FB_IMAGE_INDEX_INDIR_PING_S_H_G = 44,
    FB_IMAGE_INDEX_INDIR_PING_S_H_B = 45,
    FB_IMAGE_INDEX_INDIR_PONG_S_H_R = 46,
    FB_IMAGE_INDEX_INDIR_PONG_S_H_G = 47,
    FB_IMAGE_INDEX_INDIR_PONG_S_H_B = 48,
    FB_IMAGE_INDEX_ATROUS_FILTERED_VARIANCE = 49,
    FB_IMAGE_INDEX_GRADIENT_SAMPLES = 50,
    FB_IMAGE_INDEX_GRADIENT_SAMPLES_PREV = 51,
    FB_IMAGE_INDEX_DIFF_AND_SPEC_PING_GRADIENT = 52,
    FB_IMAGE_INDEX_DIFF_AND_SPEC_PONG_GRADIENT = 53,
    FB_IMAGE_INDEX_INDIR_PING_GRADIENT = 54,
    FB_IMAGE_INDEX_INDIR_PONG_GRADIENT = 55,
    FB_IMAGE_INDEX_BLOOM_MIP1 = 56,
    FB_IMAGE_INDEX_BLOOM_MIP2 = 57,
    FB_IMAGE_INDEX_BLOOM_MIP3 = 58,
    FB_IMAGE_INDEX_BLOOM_MIP4 = 59,
    FB_IMAGE_INDEX_BLOOM_MIP5 = 60,
};

enum FramebufferImageFlagBits
{
    FB_IMAGE_FLAGS_FRAMEBUF_FLAGS_IS_ATTACHMENT = 4,
    FB_IMAGE_FLAGS_FRAMEBUF_FLAGS_FORCE_SIZE_1_2 = 8,
    FB_IMAGE_FLAGS_FRAMEBUF_FLAGS_FORCE_SIZE_1_3 = 16,
    FB_IMAGE_FLAGS_FRAMEBUF_FLAGS_FORCE_SIZE_1_4 = 32,
    FB_IMAGE_FLAGS_FRAMEBUF_FLAGS_FORCE_SIZE_1_8 = 64,
    FB_IMAGE_FLAGS_FRAMEBUF_FLAGS_FORCE_SIZE_1_16 = 128,
    FB_IMAGE_FLAGS_FRAMEBUF_FLAGS_FORCE_SIZE_1_32 = 256,
    FB_IMAGE_FLAGS_FRAMEBUF_FLAGS_BILINEAR_SAMPLER = 512,
};
typedef uint32_t FramebufferImageFlags;

extern const uint32_t ShFramebuffers_Count;
extern const VkFormat ShFramebuffers_Formats[];
extern const FramebufferImageFlags ShFramebuffers_Flags[];
extern const uint32_t ShFramebuffers_Bindings[];
extern const uint32_t ShFramebuffers_BindingsSwapped[];
extern const uint32_t ShFramebuffers_Sampler_Bindings[];
extern const uint32_t ShFramebuffers_Sampler_BindingsSwapped[];
extern const char *const ShFramebuffers_DebugNames[];

}