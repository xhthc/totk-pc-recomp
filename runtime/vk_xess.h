// XeSS-SR on Windows; the frame inputs and motion passes are shared with vk_dlaa.c.
#pragma once
#include "third_party/xess/xess_vk.h"

int vk_xess_wanted(void);
int vk_xess_available(void);
void vk_xess_instance_exts(const char **ext, uint32_t *n, uint32_t max, uint32_t *api);
void vk_xess_device_exts(VkInstance instance, VkPhysicalDevice phys, const char **ext, uint32_t *n, uint32_t max);
int vk_xess_device_features(VkInstance instance, VkPhysicalDevice phys, void **features);
void vk_xess_vulkan_failed(const char *what, VkResult result);
void vk_xess_init(void);
int vk_xess_configure(uint32_t w, uint32_t h, xess_2d_t *input);   // before any submissions use XeSS
void vk_xess_prepare(void);   // boot.c: after prewarm joined, before the guest starts
int vk_xess_input_valid(uint32_t w, uint32_t h);
int vk_xess_execute(VkCommandBuffer cb, const xess_vk_execute_params_t *frame);
void vk_xess_shutdown(void);  // after the device is idle, before destroying it
