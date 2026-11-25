// The DLSS experiment. WII_RECOMP_NGX=1 makes the console ask NVIDIA's NGX core
// whether DLSS Super Resolution can run on the Vulkan device it just made. The core
// comes from the installed driver and the feature library from
// WII_RECOMP_NGX_FEATURE_PATH (default: beside the console), so nothing NVIDIA ships
// lives in this tree. The experiment proves the whole feature lifecycle on a synthetic
// frame: create a 1280x720 to 1920x1080 feature, evaluate it, check that it wrote the
// output, and release it. The game itself still has no motion vectors or jitter, so it
// keeps the linear blit or FSR.
#pragma once
#include "gx_vulkan.h"

// True when WII_RECOMP_NGX asked for the experiment, whether or not the core loaded.
int vk_ngx_enabled(void);

// Loads the NGX core. Call once, before the instance is created, because NGX says
// which instance extensions it needs. Returns 1 when every piece was found.
int vk_ngx_begin(void);

// Adds the instance extensions NGX asks for, skipping names already present.
void vk_ngx_add_instance_extensions(const char **names, uint32_t *count, uint32_t most);

// Asks the core whether the chosen card can run DLSS, and remembers the device
// extensions NGX needs. Call after the physical device is chosen.
void vk_ngx_check_support(VkInstance instance, VkPhysicalDevice physical);

// Adds the device extensions remembered by vk_ngx_check_support. A device that
// lacks one is reported and turns the experiment off rather than failing to start.
void vk_ngx_add_device_extensions(const char **names, uint32_t *count, uint32_t most);

// True when the device should declare NGX's feature bits as well as extensions.
int vk_ngx_wants_feature_chain(void);

// Starts NGX on the finished device, logs what it reports, and creates and releases
// one DLSS feature to prove the parameter path. Call after vkCreateDevice, with the
// window's queue already taken.
void vk_ngx_init_device(VkInstance instance, VkPhysicalDevice physical, VkDevice device, VkQueue queue,
                        uint32_t queue_family);

// Ends NGX. Today nothing calls this: the console's device lives until the process
// exits, and the driver frees it then.
void vk_ngx_shutdown(VkDevice device);
