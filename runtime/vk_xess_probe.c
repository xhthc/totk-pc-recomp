// --probe-xess: resolve a known HDR color through XeSS with the game's scene formats, without game data.
#include <stdio.h>

#include "host.h"
#include "vk.h"
#include "vk_xess.h"

typedef struct {
    xess_vk_image_view_info info;
    VmaAllocation mem;
} Image;

static int make_image(const VkCtx *c, Image *out, xess_2d_t size, VkFormat format,
                      VkImageAspectFlags aspect, VkImageUsageFlags usage) {
    VkImageCreateInfo image = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D,
        .format = format, .extent = {size.x, size.y, 1}, .mipLevels = 1, .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL, .usage = usage};
    VmaAllocationCreateInfo alloc = {.usage = VMA_MEMORY_USAGE_AUTO};
    if (vmaCreateImage(c->vma, &image, &alloc, &out->info.image, &out->mem, NULL) != VK_SUCCESS) return 0;
    out->info.subresourceRange = (VkImageSubresourceRange){aspect, 0, 1, 0, 1};
    out->info.format = format;
    out->info.width = size.x;
    out->info.height = size.y;
    VkImageViewCreateInfo view = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = out->info.image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = format, .subresourceRange = out->info.subresourceRange};
    return vkCreateImageView(c->device, &view, NULL, &out->info.imageView) == VK_SUCCESS;
}

static void init_image(VkCommandBuffer cb, const xess_vk_image_view_info *image) {
    VkImageMemoryBarrier2 b = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, .dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED, .newLayout = VK_IMAGE_LAYOUT_GENERAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image->image, .subresourceRange = image->subresourceRange};
    VkDependencyInfo d = {.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &b};
    vkCmdPipelineBarrier2(cb, &d);
}

int probe_xess(void) {
    if (!vk_xess_wanted()) {
        puts("probe-xess: set TOTK_XESS=native or a XeSS quality mode");
        return 1;
    }
    if (!vk_init()) return 1;
    const VkCtx *c = vk_ctx();
    const xess_2d_t out = {1920, 1080};
    xess_2d_t in;
    Image img[4] = {0};
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer cb;
    VkBuffer read = VK_NULL_HANDLE;
    VmaAllocation read_mem;
    VmaAllocationInfo mapped;
    const char *step = "SDK initialization";
    int passed = 0;
    if (!vk_xess_configure(out.x, out.y, &in)) goto end;

    step = "allocating scene textures";
    VkImageUsageFlags input = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (!make_image(c, &img[0], in, VK_FORMAT_B10G11R11_UFLOAT_PACK32, VK_IMAGE_ASPECT_COLOR_BIT, input) ||
        !make_image(c, &img[1], in, VK_FORMAT_R16G16_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT, input) ||
        !make_image(c, &img[2], in, VK_FORMAT_D32_SFLOAT, VK_IMAGE_ASPECT_DEPTH_BIT, input) ||
        !make_image(c, &img[3], out, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT,
                    VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT)) goto end;
    VkBufferCreateInfo buffer = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = 8,
        .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT};
    VmaAllocationCreateInfo host = {.usage = VMA_MEMORY_USAGE_AUTO,
        .flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT};
    if (vmaCreateBuffer(c->vma, &buffer, &host, &read, &read_mem, &mapped) != VK_SUCCESS) goto end;

    step = "recording the test frame";
    VkCommandPoolCreateInfo pi = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .queueFamilyIndex = c->gfx_family};
    if (vkCreateCommandPool(c->device, &pi, NULL, &pool) != VK_SUCCESS) goto end;
    VkCommandBufferAllocateInfo ai = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
    VkCommandBufferBeginInfo bi = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    if (vkAllocateCommandBuffers(c->device, &ai, &cb) != VK_SUCCESS || vkBeginCommandBuffer(cb, &bi) != VK_SUCCESS) goto end;
    for (int i = 0; i < 4; i++) init_image(cb, &img[i].info);
    VkClearColorValue color = {.float32 = {0.125f, 0.5f, 0.25f, 1.0f}}, motion = {0};
    VkClearDepthStencilValue depth = {0.75f, 0};
    vkCmdClearColorImage(cb, img[0].info.image, VK_IMAGE_LAYOUT_GENERAL, &color, 1, &img[0].info.subresourceRange);
    vkCmdClearColorImage(cb, img[1].info.image, VK_IMAGE_LAYOUT_GENERAL, &motion, 1, &img[1].info.subresourceRange);
    vkCmdClearDepthStencilImage(cb, img[2].info.image, VK_IMAGE_LAYOUT_GENERAL, &depth, 1, &img[2].info.subresourceRange);
    xess_vk_execute_params_t frame = {.colorTexture = img[0].info, .velocityTexture = img[1].info,
        .depthTexture = img[2].info, .outputTexture = img[3].info, .exposureScale = 1.0f,
        .resetHistory = 1, .inputWidth = in.x, .inputHeight = in.y};
    if (!vk_xess_execute(cb, &frame)) goto end;
    VkMemoryBarrier2 sync = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, .srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT, .dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT};
    VkDependencyInfo di = {.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .memoryBarrierCount = 1, .pMemoryBarriers = &sync};
    vkCmdPipelineBarrier2(cb, &di);
    VkBufferImageCopy pixel = {.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
        .imageOffset = {(int32_t)(out.x / 2), (int32_t)(out.y / 2), 0}, .imageExtent = {1, 1, 1}};
    vkCmdCopyImageToBuffer(cb, img[3].info.image, VK_IMAGE_LAYOUT_GENERAL, read, 1, &pixel);
    if (vkEndCommandBuffer(cb) != VK_SUCCESS) goto end;

    step = "submitting the test frame";
    VkSubmitInfo submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cb};
    if (vkQueueSubmit(c->gfx_queue, 1, &submit, VK_NULL_HANDLE) != VK_SUCCESS ||
        vkQueueWaitIdle(c->gfx_queue) != VK_SUCCESS ||
        vmaInvalidateAllocation(c->vma, read_mem, 0, VK_WHOLE_SIZE) != VK_SUCCESS) goto end;
    const uint16_t *rgba = mapped.pMappedData;
    printf("probe-xess: output pixel RGB (FP16 bits) %04x %04x %04x\n", rgba[0], rgba[1], rgba[2]);
    step = "checking the reconstructed pixel";
    passed = rgba[0] > 0 && rgba[0] < rgba[2] && rgba[2] < rgba[1] && rgba[1] < 0x3c00;
end:
    if (read) vmaDestroyBuffer(c->vma, read, read_mem);
    if (pool) vkDestroyCommandPool(c->device, pool, NULL);
    for (int i = 0; i < 4; i++) {
        if (img[i].info.imageView) vkDestroyImageView(c->device, img[i].info.imageView, NULL);
        if (img[i].info.image) vmaDestroyImage(c->vma, img[i].info.image, img[i].mem);
    }
    vk_shutdown();
    long errors = vk_validation_errors_total();
    if (!passed || errors) {
        printf("probe-xess: FAILED at %s (Vulkan validation errors: %ld)\n", step, errors);
        return 1;
    }
    puts("probe-xess: passed");
    return 0;
}
