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

#include "TextureExporter.h"
#include "Utils.h"

#include "Stb/stb_image_write.h"

#include <span>

namespace
{

bool PrepareTargetFile( const std::filesystem::path& filepath, bool overwriteFiles )
{
    using namespace RTGL1;

    if( std::filesystem::exists( filepath ) )
    {
        if( !overwriteFiles )
        {
            debug::Warning( "{}: Image was not exported, as file already exists",
                            filepath.string() );
            return false;
        }
    }
    else
    {
        std::error_code ec;
        std::filesystem::create_directories( filepath.parent_path(), ec );

        if( ec )
        {
            debug::Warning( "{}: std::filesystem::create_directories error: {}",
                            filepath.string(),
                            ec.message() );
            return false;
        }
    }

    return true;
}

bool WritePNG( const std::filesystem::path& filepath,
               const void*                  pixels,
               const RgExtent2D&            size,
               const size_t                 rowPitch = 0 )
{
    using namespace RTGL1;

    assert( std::filesystem::exists( filepath.parent_path() ) );

    if( !stbi_write_png( filepath.string().c_str(),
                         int( size.width ),
                         int( size.height ),
                         4,
                         pixels,
                         int( rowPitch ) ) )
    {
        debug::Warning( "{}: stbi_write_png fail", filepath.string() );
        return false;
    }

    return true;
}

}

bool RTGL1::TextureExporter::ExportAsPNG( MemoryAllocator&             allocator,
                                          CommandBufferManager&        cmdManager,
                                          VkImage                      srcImage,
                                          RgExtent2D                   srcImageSize,
                                          VkFormat                     srcImageFormat,
                                          const std::filesystem::path& filepath,
                                          bool,
                                          bool overwriteFiles )
{
    VkDevice device = allocator.GetDevice();

    if( !PrepareTargetFile( filepath, overwriteFiles ) )
    {
        return false;
    }

    if( !CheckSupport( allocator.GetPhysicalDevice(), srcImageFormat ) )
    {
        return false;
    }

    vkDeviceWaitIdle( device );
    VkCommandBuffer cmd = cmdManager.StartGraphicsCmd();

    constexpr VkImageLayout srcImageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    const VkImageSubresourceRange subresRange = {
        .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel   = 0,
        .levelCount     = 1,
        .baseArrayLayer = 0,
        .layerCount     = 1,
    };
    const VkImageSubresource subres = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .mipLevel   = 0,
        .arrayLayer = 0,
    };

    VkImage dstImage = VK_NULL_HANDLE;
    {
        VkImageCreateInfo info = {
            .sType                 = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .pNext                 = nullptr,
            .flags                 = 0,
            .imageType             = VK_IMAGE_TYPE_2D,
            .format                = VK_FORMAT_R8G8B8A8_UNORM,
            .extent                = { srcImageSize.width, srcImageSize.height, 1 },
            .mipLevels             = 1,
            .arrayLayers           = 1,
            .samples               = VK_SAMPLE_COUNT_1_BIT,
            .tiling                = VK_IMAGE_TILING_LINEAR,
            .usage                 = VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            .sharingMode           = VK_SHARING_MODE_EXCLUSIVE,
            .queueFamilyIndexCount = 0,
            .pQueueFamilyIndices   = nullptr,
            .initialLayout         = VK_IMAGE_LAYOUT_UNDEFINED,
        };

        VkResult r = vkCreateImage( device, &info, nullptr, &dstImage );
        VK_CHECKERROR( r );
    }

    VkDeviceMemory dstImageMemory = VK_NULL_HANDLE;
    {
        VkMemoryRequirements memReqs = {};
        vkGetImageMemoryRequirements( device, dstImage, &memReqs );

        dstImageMemory = allocator.AllocDedicated( memReqs,
                                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                       VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                                   MemoryAllocator::AllocType::DEFAULT,
                                                   "Export dst image" );

        VkResult r = vkBindImageMemory( device, dstImage, dstImageMemory, 0 );
        VK_CHECKERROR( r );
    }

    {
        VkImageMemoryBarrier2 bs[] = {
            // srcImage to transfer src
            {
                .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2_KHR,
                .srcStageMask        = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,
                .srcAccessMask       = VK_ACCESS_2_SHADER_READ_BIT,
                .dstStageMask        = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
                .dstAccessMask       = VK_ACCESS_2_TRANSFER_READ_BIT,
                .oldLayout           = srcImageLayout,
                .newLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image               = srcImage,
                .subresourceRange    = subresRange,
            },
            // dstImage to transfer dst
            {
                .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2_KHR,
                .srcStageMask        = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,
                .srcAccessMask       = VK_ACCESS_2_NONE,
                .dstStageMask        = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
                .dstAccessMask       = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                .oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image               = dstImage,
                .subresourceRange    = subresRange,
            },
        };

        VkDependencyInfoKHR dependencyInfo = {
            .sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR,
            .imageMemoryBarrierCount = std::size( bs ),
            .pImageMemoryBarriers    = std::data( bs ),
        };

        svkCmdPipelineBarrier2KHR( cmd, &dependencyInfo );
    }
    /*{
        VkImageBlit blit = {
            .srcSubresource = { .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                                .mipLevel       = 0,
                                .baseArrayLayer = 0,
                                .layerCount     = 1 },
            .srcOffsets     = { { 0, 0, 0 },
                                { int32_t( srcImageSize.width ), int32_t( srcImageSize.height ), 0 }
    }, .dstSubresource = { .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel       = 0,
                                .baseArrayLayer = 0,
                                .layerCount     = 1 },
            .dstOffsets     = { { 0, 0, 0 },
                                { int32_t( srcImageSize.width ), int32_t( srcImageSize.height ), 0 }
    },
        };

        vkCmdBlitImage( cmd,
                        srcImage,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        dstImage,
                        VK_IMAGE_LAYOUT_GENERAL,
                        1,
                        &blit,
                        VK_FILTER_NEAREST );
    }*/
    {
        VkImageCopy region = {
            .srcSubresource = { .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                                .mipLevel       = 0,
                                .baseArrayLayer = 0,
                                .layerCount     = 1 },
            .srcOffset      = { 0, 0, 0 },
            .dstSubresource = { .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                                .mipLevel       = 0,
                                .baseArrayLayer = 0,
                                .layerCount     = 1 },
            .dstOffset      = { 0, 0, 0 },
            .extent         = { srcImageSize.width, srcImageSize.height, 1 },
        };

        vkCmdCopyImage( cmd,
                        srcImage,
                        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        dstImage,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        1,
                        &region );
    }
    {
        VkImageMemoryBarrier2 bs[] = {
            {
                // srcImage to original layout
                .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2_KHR,
                .srcStageMask        = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
                .srcAccessMask       = VK_ACCESS_2_TRANSFER_READ_BIT,
                .dstStageMask        = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,
                .dstAccessMask       = VK_ACCESS_2_SHADER_READ_BIT,
                .oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                .newLayout           = srcImageLayout,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image               = srcImage,
                .subresourceRange    = subresRange,
            },
            {
                // dstImage to host read
                .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2_KHR,
                .srcStageMask        = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
                .srcAccessMask       = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                .dstStageMask        = VK_PIPELINE_STAGE_2_HOST_BIT,
                .dstAccessMask       = VK_ACCESS_2_HOST_READ_BIT,
                .oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .newLayout           = VK_IMAGE_LAYOUT_GENERAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image               = dstImage,
                .subresourceRange    = subresRange,
            },
        };

        VkDependencyInfoKHR dependencyInfo = {
            .sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR,
            .imageMemoryBarrierCount = std::size( bs ),
            .pImageMemoryBarriers    = std::data( bs ),
        };

        svkCmdPipelineBarrier2KHR( cmd, &dependencyInfo );
    }
    cmdManager.Submit( cmd );
    cmdManager.WaitGraphicsIdle();

    {
        VkSubresourceLayout subresLayout = {};
        vkGetImageSubresourceLayout( device, dstImage, &subres, &subresLayout );

        assert( subresLayout.size >= 4ull * srcImageSize.width * srcImageSize.height );

        uint8_t* data = nullptr;
        VkResult r    = vkMapMemory(
            device, dstImageMemory, 0, VK_WHOLE_SIZE, 0, reinterpret_cast< void** >( &data ) );
        VK_CHECKERROR( r );

        WritePNG( filepath, &data[ subresLayout.offset ], srcImageSize, subresLayout.rowPitch );
    }
    {
        vkUnmapMemory( device, dstImageMemory );
        vkFreeMemory( device, dstImageMemory, nullptr );
        vkDestroyImage( device, dstImage, nullptr );
    }

    return true;
}

bool RTGL1::TextureExporter::CheckSupport( VkPhysicalDevice physDevice, VkFormat srcImageFormat )
{
    VkFormatProperties formatProps = {};

    // optimal
    {
        vkGetPhysicalDeviceFormatProperties( physDevice, srcImageFormat, &formatProps );
        if( !( formatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT ) )
        {
            debug::Warning( "BLIT_SRC not supported for VkFormat {}", uint32_t( srcImageFormat ) );
            return false;
        }
    }
    {
        vkGetPhysicalDeviceFormatProperties( physDevice, VK_FORMAT_R8G8B8A8_UNORM, &formatProps );
        if( !( formatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT ) )
        {
            debug::Warning( "BLIT_DST not supported for VK_FORMAT_R8G8B8A8_UNORM" );
            return false;
        }
        if( !( formatProps.linearTilingFeatures & VK_FORMAT_FEATURE_TRANSFER_SRC_BIT ) )
        {
            debug::Warning(
                "TRANSFER_SRC not supported for VK_FORMAT_R8G8B8A8_UNORM (linear tiling)" );
            return false;
        }
        if( !( formatProps.linearTilingFeatures & VK_FORMAT_FEATURE_TRANSFER_DST_BIT ) )
        {
            debug::Warning(
                "TRANSFER_DST not supported for VK_FORMAT_R8G8B8A8_UNORM (linear tiling)" );
            return false;
        }
    }

    return true;
}
