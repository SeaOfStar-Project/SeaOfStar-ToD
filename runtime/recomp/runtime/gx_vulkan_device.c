// Vulkan's start-up, memory, window images and frames.
#ifndef WII_PLATFORM_SDL
#define GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_VULKAN
#endif
#include "timeline.h"
#include "gx_vulkan.h"
#include "gx_vulkan_ngx.h"

#ifdef WII_PLATFORM_SDL
// SDL3 supplies the window's Vulkan pieces here, as GLFW does on the desktop.
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
static int glfwVulkanSupported(void) {
  if (SDL_Vulkan_LoadLibrary(0)) return 1;
  fprintf(stderr, "vulkan: SDL can't load the Vulkan library: %s\n", SDL_GetError());
  return 0;
}
static const char **glfwGetRequiredInstanceExtensions(uint32_t *count) { return (const char **)SDL_Vulkan_GetInstanceExtensions(count); }
static VkResult glfwCreateWindowSurface(VkInstance instance, struct GLFWwindow *window, const void *allocator, VkSurfaceKHR *surface) {
  (void)allocator;
  if (SDL_Vulkan_CreateSurface((SDL_Window *)window, instance, 0, surface)) return VK_SUCCESS;
  fprintf(stderr, "vulkan: SDL can't make the window's surface: %s\n", SDL_GetError());
  return VK_ERROR_INITIALIZATION_FAILED;
}
#else
#include <GLFW/glfw3.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef WII_PLATFORM_SDL
#include "ios.h"
void window_letterbox(int width, int height, int *x, int *y, int *w, int *h);  // window_sdl.c
#endif
struct GLFWwindow *window_handle(void);

// The memory the processor writes and the card reads, one share per frame in flight (three), all mapped: 116 MiB a share on a
// computer, so 348 MiB. On a phone that is the same memory as the game's and it is taken whole, so staging (texture uploads,
// the biggest of them being a 1024 by 1024 picture with its levels, under 6 MiB) is half as large there; a frame that
// needs more finishes its commands and goes on, as it does on a computer when its share runs out.
// WII_RECOMP_RING_MB=<vertices>,<constants>,<indices>,<staging> sets the sizes, and the largest each ring reached is said as it grows.
#define VERTEX_RING_BYTES (32u * 1024u * 1024u)
#define CONSTANT_RING_BYTES (16u * 1024u * 1024u)
#define INDEX_RING_BYTES (4u * 1024u * 1024u)
#ifdef __ANDROID__
#define STAGING_RING_BYTES (32u * 1024u * 1024u)
#else
#define STAGING_RING_BYTES (64u * 1024u * 1024u)
#endif
static VkDeviceSize ring_peaks[4];
static VkDeviceSize ring_sizes[4] = {VERTEX_RING_BYTES, CONSTANT_RING_BYTES, INDEX_RING_BYTES, STAGING_RING_BYTES};
static void ring_sizes_choose(void) {
  static int done;
  if (done) return;
  done = 1;
  const char *asked = getenv("WII_RECOMP_RING_MB");
  if (!asked || !*asked) return;
  unsigned mb[4];
  if (sscanf(asked, "%u,%u,%u,%u", &mb[0], &mb[1], &mb[2], &mb[3]) != 4 || !mb[0] || !mb[1] || !mb[2] || mb[3] < 8) {
    fprintf(stderr, "vulkan: WII_RECOMP_RING_MB=%s isn't <vertices>,<constants>,<indices>,<staging> in MiB (staging at least 8): the usual sizes stay\n", asked);
    return;
  }
  for (int i = 0; i < 4; i++) ring_sizes[i] = (VkDeviceSize)mb[i] << 20;
  fprintf(stderr, "vulkan: rings of %u, %u, %u and %u MiB a frame, as WII_RECOMP_RING_MB asked\n", mb[0], mb[1], mb[2], mb[3]);
}
// Said when a ring's use in a frame passes its earlier largest by 2 MiB (1 for the small ones), so the log shows how much of each is needed.
static void ring_peaks_note(const Frame *frame) {
  const VkDeviceSize used[4] = {frame->vertices.used, frame->constants.used, frame->indices.used, frame->staging.used};
  static const char *const names[4] = {"vertices", "constants", "indices", "staging"};
  for (int i = 0; i < 4; i++) {
    if (used[i] > ring_peaks[i] + (2u << 20)) {
      ring_peaks[i] = used[i];
      fprintf(stderr, "vulkan: ring use: %s reached %.1f of %.0f MiB in a frame\n", names[i], (double)used[i] / (1 << 20), (double)ring_sizes[i] / (1 << 20));
    }
  }
}
#define VALIDATION_LAYER "VK_LAYER_KHRONOS_validation"
#define PORTABILITY_ENUMERATION "VK_KHR_portability_enumeration"
#define PORTABILITY_SUBSET "VK_KHR_portability_subset"

enum { MAX_EXTENSIONS = 16, WANTED_SWAP_IMAGES = 3 };

Vulkan vk;

void vk_check(VkResult result, const char *what) {
  if (result == VK_SUCCESS) return;
  fprintf(stderr, "stop: vulkan: %s failed with %d\n", what, result);
  exit(10);
}

static int list_has(const char *const *names, uint32_t count, const char *name);

static int instance_has_extension(const char *name) {
  uint32_t count = 0;
  vkEnumerateInstanceExtensionProperties(0, &count, 0);
  VkExtensionProperties *found = calloc(count, sizeof *found);
  vkEnumerateInstanceExtensionProperties(0, &count, found);
  int has = 0;
  for (uint32_t i = 0; i < count; i++) has |= !strcmp(found[i].extensionName, name);
  free(found);
  return has;
}

static int instance_has_layer(const char *name) {
  uint32_t count = 0;
  vkEnumerateInstanceLayerProperties(&count, 0);
  VkLayerProperties *found = calloc(count, sizeof *found);
  vkEnumerateInstanceLayerProperties(&count, found);
  int has = 0;
  for (uint32_t i = 0; i < count; i++) has |= !strcmp(found[i].layerName, name);
  free(found);
  return has;
}

// What the validation layer finds goes to stderr (so, on Android, into the run log), the first
// few hundred of them: a layer that finds one thing every frame would drown the log.
static VkDebugUtilsMessengerEXT messenger;
static volatile int validation_count;

static VKAPI_ATTR VkBool32 VKAPI_CALL validation_message(VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT types,
                                                         const VkDebugUtilsMessengerCallbackDataEXT *data, void *unused) {
  (void)types, (void)unused;
  int number = __atomic_add_fetch(&validation_count, 1, __ATOMIC_RELAXED);
  if (number > 400) {
    if (number == 401) fprintf(stderr, "validation: more than 400 messages, the rest are not printed\n");
    return VK_FALSE;
  }
  const char *kind = severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT ? "error" : severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT ? "warning" : "info";
  fprintf(stderr, "validation: %s: [%s] %s\n", kind, data->pMessageIdName ? data->pMessageIdName : "-", data->pMessage ? data->pMessage : "");
  return VK_FALSE;
}

static int layer_has_extension(const char *layer, const char *name) {
  uint32_t count = 0;
  vkEnumerateInstanceExtensionProperties(layer, &count, 0);
  VkExtensionProperties *found = calloc(count ? count : 1, sizeof *found);
  vkEnumerateInstanceExtensionProperties(layer, &count, found);
  int has = 0;
  for (uint32_t i = 0; i < count; i++) has |= !strcmp(found[i].extensionName, name);
  free(found);
  return has;
}

// The window's surface must be made on the window's thread on the Mac. With no window,
// there is no surface, and pictures only reach vk_picture_sink.
void vk_create_instance_and_surface(struct GLFWwindow *window) {
  fprintf(stderr, "vulkan: starting the instance%s\n", window ? " and the window's surface" : " (no window)");
  if (window && !glfwVulkanSupported()) { fprintf(stderr, "stop: no Vulkan here\n"); exit(10); }
  // The DLSS experiment, when asked for: load NGX before the instance exists, because
  // it names the instance extensions it needs. A missing core only logs; the game runs.
  vk_ngx_begin();
  const char *extensions[MAX_EXTENSIONS];
  uint32_t window_extensions = 0, extension_count = 0;
  const char **needed = window ? glfwGetRequiredInstanceExtensions(&window_extensions) : 0;
  for (uint32_t i = 0; i < window_extensions; i++) extensions[extension_count++] = needed[i];
  VkApplicationInfo application = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .pApplicationName = "Wii Fit Plus", .apiVersion = VK_API_VERSION_1_1};
  VkInstanceCreateInfo info = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &application};
  if (instance_has_extension(PORTABILITY_ENUMERATION)) {
    extensions[extension_count++] = PORTABILITY_ENUMERATION;
    info.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
  }
  if (window && instance_has_extension(VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME)) extensions[extension_count++] = VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME;
  const char *layers[] = {VALIDATION_LAYER};
  // WII_RECOMP_VULKAN_VALIDATION=1 turns the Khronos layer on where it can be found (on Android, as
  // libVkLayer_khronos_validation.so in the app's library folder of a debuggable build:
  // gradlew -PvalidationLayer=<path>). What it finds is printed through VK_EXT_debug_utils.
  int validating = 0;
  if (getenv("WII_RECOMP_VULKAN_VALIDATION")) {
    if (instance_has_layer(VALIDATION_LAYER)) {
      info.enabledLayerCount = 1;
      info.ppEnabledLayerNames = layers;
      validating = 1;
      if (layer_has_extension(VALIDATION_LAYER, VK_EXT_DEBUG_UTILS_EXTENSION_NAME)) extensions[extension_count++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
      else fprintf(stderr, "vulkan: the validation layer has no debug utils, so what it finds is not printed\n");
      fprintf(stderr, "vulkan: validation layer on\n");
    } else {
      fprintf(stderr, "vulkan: validation asked for, but the layer isn't installed\n");
    }
  }
  vk_ngx_add_instance_extensions(extensions, &extension_count, MAX_EXTENSIONS);
  info.enabledExtensionCount = extension_count;
  info.ppEnabledExtensionNames = extensions;
  fprintf(stderr, "vulkan: creating the instance with %u extensions, %u layers\n", (unsigned)extension_count, (unsigned)info.enabledLayerCount);
  vk_check(vkCreateInstance(&info, 0, &vk.instance), "creating an instance");
  fprintf(stderr, "vulkan: the instance exists\n");
  if (validating && list_has(extensions, extension_count, VK_EXT_DEBUG_UTILS_EXTENSION_NAME)) {
    PFN_vkCreateDebugUtilsMessengerEXT create = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(vk.instance, "vkCreateDebugUtilsMessengerEXT");
    VkDebugUtilsMessengerCreateInfoEXT listen = {.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
                                                 .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT,
                                                 .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                                                VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
                                                 .pfnUserCallback = validation_message};
    if (!create || create(vk.instance, &listen, 0, &messenger) != VK_SUCCESS) fprintf(stderr, "vulkan: can't listen to the validation layer\n");
  }
  if (window) {
    fprintf(stderr, "vulkan: creating the window's surface\n");
    vk_check(glfwCreateWindowSurface(vk.instance, window, 0, &vk.surface), "creating the window's surface");
    fprintf(stderr, "vulkan: the window's surface exists\n");
  }
}

static int device_has_extension(VkPhysicalDevice device, const char *name) {
  uint32_t count = 0;
  vkEnumerateDeviceExtensionProperties(device, 0, &count, 0);
  VkExtensionProperties *found = calloc(count, sizeof *found);
  vkEnumerateDeviceExtensionProperties(device, 0, &count, found);
  int has = 0;
  for (uint32_t i = 0; i < count; i++) has |= !strcmp(found[i].extensionName, name);
  free(found);
  return has;
}

static int list_has(const char *const *names, uint32_t count, const char *name) {
  int has = 0;
  for (uint32_t i = 0; i < count; i++) has |= names[i] && !strcmp(names[i], name);
  return has;
}

// A queue that both draws and shows pictures, or -1.
static int find_queue_family(VkPhysicalDevice device) {
  uint32_t count = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(device, &count, 0);
  VkQueueFamilyProperties *families = calloc(count, sizeof *families);
  vkGetPhysicalDeviceQueueFamilyProperties(device, &count, families);
  int found = -1;
  for (uint32_t i = 0; i < count && found < 0; i++) {
    VkBool32 presents = vk.surface ? VK_FALSE : VK_TRUE;
    if (vk.surface) vkGetPhysicalDeviceSurfaceSupportKHR(device, i, vk.surface, &presents);
    if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && presents) found = (int)i;
  }
  free(families);
  return found;
}

static void choose_physical_device(void) {
  uint32_t count = 0;
  vkEnumeratePhysicalDevices(vk.instance, &count, 0);
  VkPhysicalDevice *devices = calloc(count, sizeof *devices);
  vkEnumeratePhysicalDevices(vk.instance, &count, devices);
  VkPhysicalDeviceProperties chosen_properties;
  for (uint32_t i = 0; i < count; i++) {
    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(devices[i], &properties);
    if (find_queue_family(devices[i]) < 0 || (vk.surface && !device_has_extension(devices[i], VK_KHR_SWAPCHAIN_EXTENSION_NAME))) continue;
    // Prefer a real graphics card to one built into the processor, and either to software.
    if (!vk.physical || properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ||
        chosen_properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU) {
      vk.physical = devices[i];
      chosen_properties = properties;
    }
  }
  free(devices);
  if (!vk.physical) { fprintf(stderr, "stop: no suitable graphics card for Vulkan\n"); exit(10); }
  vk.limits = chosen_properties.limits;
  fprintf(stderr, "vulkan: %s\n", chosen_properties.deviceName);
  vk_ngx_check_support(vk.instance, vk.physical);
}

// 32-bit float depth, which the picture only ever compares, where the device can attach it, sample it and
// move it around; else 24 bits. WII_RECOMP_GX_DEBUG=depth=x8d24 asks for the 24 bits.
enum { DEPTH_NEEDS = VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_BLIT_SRC_BIT |
                     VK_FORMAT_FEATURE_BLIT_DST_BIT | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT };

static int depth_format_works(VkFormat format) {
  VkFormatProperties properties;
  vkGetPhysicalDeviceFormatProperties(vk.physical, format, &properties);
  return (properties.optimalTilingFeatures & DEPTH_NEEDS) == DEPTH_NEEDS;
}

static void choose_depth_format(void) {
  const char *asked = gx_debug_value("depth");
  vk.depth_format = VK_FORMAT_D32_SFLOAT;
  if (asked && !strcmp(asked, "x8d24")) vk.depth_format = VK_FORMAT_X8_D24_UNORM_PACK32;
  else if (asked && strcmp(asked, "d32")) fprintf(stderr, "gx debug: depth=%s isn't known (d32, x8d24), so d32\n", asked);
  if (!depth_format_works(vk.depth_format)) {
    VkFormat other = vk.depth_format == VK_FORMAT_D32_SFLOAT ? VK_FORMAT_X8_D24_UNORM_PACK32 : VK_FORMAT_D32_SFLOAT;
    fprintf(stderr, "vulkan: depth format %d lacks features the picture needs, trying %d\n", (int)vk.depth_format, (int)other);
    if (depth_format_works(other)) vk.depth_format = other;
    else fprintf(stderr, "vulkan: neither depth format has everything, so %d is used and depth copies may fail\n", (int)vk.depth_format);
  }
}

void vk_create_device(void) {
  choose_physical_device();
  vk.queue_family = (uint32_t)find_queue_family(vk.physical);
  // The window's queue: a second one in the same family, or else another family that
  // draws and shows. On the Mac, main.c asks for the extra families, each its own Metal queue.
  uint32_t family_count = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(vk.physical, &family_count, 0);
  VkQueueFamilyProperties *families = calloc(family_count, sizeof *families);
  vkGetPhysicalDeviceQueueFamilyProperties(vk.physical, &family_count, families);
  int own_show_queue = vk.surface && vk_window_wants_own_queue();
  uint32_t show_family = vk.queue_family;
  if (own_show_queue && families[vk.queue_family].queueCount < 2) {
    own_show_queue = 0;
    for (uint32_t i = 0; i < family_count && !own_show_queue; i++) {
      VkBool32 presents = VK_FALSE;
      vkGetPhysicalDeviceSurfaceSupportKHR(vk.physical, i, vk.surface, &presents);
      if (i != vk.queue_family && (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && presents) {
        show_family = i;
        own_show_queue = 1;
      }
    }
  }
  free(families);
  float priorities[2] = {1, 1};
  int same_family = own_show_queue && show_family == vk.queue_family;
  VkDeviceQueueCreateInfo queues[2] = {
    {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueFamilyIndex = vk.queue_family, .queueCount = same_family ? 2 : 1, .pQueuePriorities = priorities},
    {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueFamilyIndex = show_family, .queueCount = 1, .pQueuePriorities = priorities},
  };
  const char *extensions[MAX_EXTENSIONS];
  uint32_t extension_count = 0;
  if (vk.surface) extensions[extension_count++] = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
  int pushes_textures = device_has_extension(vk.physical, VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME) && !gx_debug("nopush");
  if (pushes_textures) extensions[extension_count++] = VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME;
  // DLSS needs a few more, which the NGX core names after it has seen the card.
  vk_ngx_add_device_extensions(extensions, &extension_count, MAX_EXTENSIONS);
  VkPhysicalDeviceFeatures available, wanted = {0};
  vkGetPhysicalDeviceFeatures(vk.physical, &available);
  // Blending reads a second shader output. Without it, a constant output alpha also
  // changes the blend, which only a few effects use.
  wanted.dualSrcBlend = available.dualSrcBlend;
  vk.dual_source = available.dualSrcBlend == VK_TRUE;
  if (gx_debug("nodual")) {
    wanted.dualSrcBlend = VK_FALSE;
    vk.dual_source = 0;
  }
  vk.alt_no_dual = !gx_debug("oldnodual");
  vk.alpha_simple = !gx_debug("fullalphapass");
  gx_alpha_zero_variants = !vk.dual_source && vk.alt_no_dual && vk.alpha_simple;
  if (!vk.dual_source) {
    if (vk.alt_no_dual) {
      fprintf(stderr, "vulkan: no dual-source blending: blends read the stages' alpha from the output's alpha, and an alpha to store (a constant alpha) "
                      "is drawn by a second, alpha-only pass where a draw blends with the stages' alpha\n");
    } else {
      fprintf(stderr, "vulkan: no dual-source blending (oldnodual): blends read the alpha to store, so constant alpha will blend slightly wrong\n");
    }
  }
  // The bounding box: each pixel drawn widens it, from the shader.
  wanted.fragmentStoresAndAtomics = available.fragmentStoresAndAtomics;
  vk.bounding_box = available.fragmentStoresAndAtomics == VK_TRUE;
  if (gx_debug("nobbox")) {
    wanted.fragmentStoresAndAtomics = VK_FALSE;
    vk.bounding_box = 0;
  }
  if (!vk.bounding_box) fprintf(stderr, "vulkan: shaders can't write to buffers, so the bounding box stays as the game set it\n");
  wanted.samplerAnisotropy = available.samplerAnisotropy;
  vk.max_anisotropy = available.samplerAnisotropy ? vk.limits.maxSamplerAnisotropy : 0;
  if (gx_debug("noaniso")) {
    wanted.samplerAnisotropy = VK_FALSE;
    vk.max_anisotropy = 0;
  }
  if (!vk.max_anisotropy) fprintf(stderr, "vulkan: no anisotropic filtering, so textures seen at a slope will blur more\n");
  VkDeviceCreateInfo info = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .queueCreateInfoCount = own_show_queue && !same_family ? 2 : 1, .pQueueCreateInfos = queues,
                             .enabledExtensionCount = extension_count, .ppEnabledExtensionNames = extensions, .pEnabledFeatures = &wanted};
  // Only for measuring: the card counts its own work, which needs a lock no other program holds.
  VkPhysicalDevicePerformanceQueryFeaturesKHR counting = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PERFORMANCE_QUERY_FEATURES_KHR,
                                                          .performanceCounterQueryPools = VK_TRUE};
  if (getenv("WII_RECOMP_GPU_COUNTERS")) {
    if (device_has_extension(vk.physical, VK_KHR_PERFORMANCE_QUERY_EXTENSION_NAME)) {
      extensions[info.enabledExtensionCount++] = VK_KHR_PERFORMANCE_QUERY_EXTENSION_NAME;
      info.pNext = &counting;
      vk.counting = 1;
    } else {
      fprintf(stderr, "vulkan: this device can't count its own work, so WII_RECOMP_GPU_COUNTERS does nothing\n");
    }
  }
  // Vulkan on Metal leaves some things out, and using what's left must be declared.
  VkPhysicalDevicePortabilitySubsetFeaturesKHR portability = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PORTABILITY_SUBSET_FEATURES_KHR, .pNext = (void *)info.pNext};
  if (device_has_extension(vk.physical, PORTABILITY_SUBSET)) {
    VkPhysicalDeviceFeatures2 features = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &portability};
    vkGetPhysicalDeviceFeatures2(vk.physical, &features);
    if (!portability.imageViewFormatSwizzle) fprintf(stderr, "vulkan: this device can't swizzle, so one-channel copies will look wrong\n");
    extensions[info.enabledExtensionCount++] = PORTABILITY_SUBSET;
    info.pNext = &portability;
  }
  // DLSS's device extensions come with feature bits: buffer device addresses and
  // synchronization 2. NGX needs them declared on the device, so swap the plain
  // features for a chain that carries the same ones plus these, keeping whatever the
  // measuring or Metal path already put there.
  VkPhysicalDeviceFeatures2 dlss_features = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .features = wanted};
  VkPhysicalDeviceBufferDeviceAddressFeatures dlss_address = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES};
  VkPhysicalDeviceSynchronization2Features dlss_sync = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES};
  if (vk_ngx_wants_feature_chain()) {
    VkPhysicalDeviceBufferDeviceAddressFeatures address_available = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES};
    VkPhysicalDeviceSynchronization2Features sync_available = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES,
                                                              .pNext = &address_available};
    VkPhysicalDeviceFeatures2 can = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &sync_available};
    vkGetPhysicalDeviceFeatures2(vk.physical, &can);
    int has_address = list_has(extensions, extension_count, VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME) && address_available.bufferDeviceAddress;
    int has_sync = list_has(extensions, extension_count, VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME) && sync_available.synchronization2;
    if (!has_address) fprintf(stderr, "ngx: the device can't give buffer device addresses, which DLSS needs\n");
    if (has_address || has_sync) {
      void *tail = info.pNext;
      if (has_sync) {
        dlss_sync.synchronization2 = VK_TRUE;
        dlss_sync.pNext = tail;
        tail = &dlss_sync;
      }
      if (has_address) {
        dlss_address.bufferDeviceAddress = VK_TRUE;
        dlss_address.pNext = tail;
        tail = &dlss_address;
      }
      dlss_features.pNext = tail;
      // The chain replaces pEnabledFeatures; passing both would be wrong.
      info.pNext = &dlss_features;
      info.pEnabledFeatures = 0;
    }
  }
  vk_check(vkCreateDevice(vk.physical, &info, 0, &vk.device), "creating the device");
  if (vk.counting) {
    PFN_vkAcquireProfilingLockKHR lock = (PFN_vkAcquireProfilingLockKHR)vkGetDeviceProcAddr(vk.device, "vkAcquireProfilingLockKHR");
    VkAcquireProfilingLockInfoKHR wait = {.sType = VK_STRUCTURE_TYPE_ACQUIRE_PROFILING_LOCK_INFO_KHR, .timeout = UINT64_MAX};
    if (!lock || lock(vk.device, &wait) != VK_SUCCESS) {
      fprintf(stderr, "vulkan: the card's counters are in use elsewhere, so WII_RECOMP_GPU_COUNTERS does nothing\n");
      vk.counting = 0;
    }
  }
  vkGetDeviceQueue(vk.device, vk.queue_family, 0, &vk.queue);
  if (own_show_queue) vkGetDeviceQueue(vk.device, show_family, same_family ? 1 : 0, &vk.show_queue);
  // The device exists; let NGX start on it if everything it asked for was granted.
  vk_ngx_init_device(vk.instance, vk.physical, vk.device, vk.queue, vk.queue_family);
  vk.show_family = show_family;
  if (own_show_queue) fprintf(stderr, "vulkan: pictures shown on a queue of their own\n");
  if (pushes_textures) vk.push_descriptors = (PFN_vkCmdPushDescriptorSetKHR)vkGetDeviceProcAddr(vk.device, "vkCmdPushDescriptorSetKHR");
  choose_depth_format();
  VkFormatProperties depth;
  vkGetPhysicalDeviceFormatProperties(vk.physical, DEPTH_FORMAT, &depth);
  vk.depth_filters_linear = (depth.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) != 0 && !gx_debug("nodepthlinear");
  VkFormatFeatureFlags blits = VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT;
  if ((depth.optimalTilingFeatures & blits) != blits) fprintf(stderr, "vulkan: this device can't blit depth, so depth copies will fail\n");
  vk_report_device();
  if (gx_stats_wanted()) {
    // Which vertex formats the device can read from a buffer as they are (for decoding less on the processor): one line.
    static const struct { VkFormat format; const char *name; } formats[] = {
      {VK_FORMAT_R8G8B8A8_UNORM, "rgba8 unorm"}, {VK_FORMAT_R8G8B8_UNORM, "rgb8 unorm"}, {VK_FORMAT_R8G8B8_SNORM, "rgb8 snorm"}, {VK_FORMAT_R8G8B8_SSCALED, "rgb8 sscaled"},
      {VK_FORMAT_R16G16_UNORM, "rg16 unorm"}, {VK_FORMAT_R16G16_SSCALED, "rg16 sscaled"}, {VK_FORMAT_R16G16B16_SSCALED, "rgb16 sscaled"},
      {VK_FORMAT_R16G16B16_SNORM, "rgb16 snorm"}, {VK_FORMAT_R16G16B16A16_SSCALED, "rgba16 sscaled"}};
    char line[512];
    int at = snprintf(line, sizeof line, "vulkan: vertex input formats:");
    for (size_t i = 0; i < sizeof formats / sizeof *formats && at < (int)sizeof line - 40; i++) {
      VkFormatProperties properties;
      vkGetPhysicalDeviceFormatProperties(vk.physical, formats[i].format, &properties);
      at += snprintf(line + at, sizeof line - (size_t)at, " %s=%s", formats[i].name, properties.bufferFeatures & VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT ? "yes" : "no");
    }
    fprintf(stderr, "%s (guest data is big-endian: only the 8-bit ones could be fed as they are)\n", line);
  }
}

// ---- What the device offers, printed once after the device is made (and by WII_RECOMP_GX_DEBUG=report
// later). On a phone this is what tells a black picture's cause from a guess.

typedef struct { uint32_t bit; const char *name; } FeatureName;

static const FeatureName format_bits[] = {
  {VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT, "sampled"}, {VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT, "linear"},
  {VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT, "color"}, {VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT, "blend"},
  {VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT, "depth"}, {VK_FORMAT_FEATURE_BLIT_SRC_BIT, "blit_src"},
  {VK_FORMAT_FEATURE_BLIT_DST_BIT, "blit_dst"}, {VK_FORMAT_FEATURE_TRANSFER_SRC_BIT, "xfer_src"},
  {VK_FORMAT_FEATURE_TRANSFER_DST_BIT, "xfer_dst"}, {VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT, "vertex"},
  {VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT, "storage"},
};

static void print_bits(const char *label, uint32_t have, uint32_t needed) {
  fprintf(stderr, " %s[", label);
  for (size_t i = 0; i < sizeof format_bits / sizeof format_bits[0]; i++) {
    if (have & format_bits[i].bit) fprintf(stderr, "%s%s", format_bits[i].name, " ");
  }
  fprintf(stderr, "]");
  for (size_t i = 0; i < sizeof format_bits / sizeof format_bits[0]; i++) {
    if ((needed & format_bits[i].bit) && !(have & format_bits[i].bit)) fprintf(stderr, " MISSING:%s", format_bits[i].name);
  }
}

static void report_format(const char *name, VkFormat format, uint32_t optimal_needed, uint32_t buffer_needed) {
  VkFormatProperties p;
  vkGetPhysicalDeviceFormatProperties(vk.physical, format, &p);
  fprintf(stderr, "vulkan: format %s (%d):", name, (int)format);
  print_bits("optimal", p.optimalTilingFeatures, optimal_needed);
  if (buffer_needed) print_bits("buffer", p.bufferFeatures, buffer_needed);
  fprintf(stderr, "\n");
}

void vk_report_device(void) {
  static int printed;
  if (printed++ && !gx_debug("report")) return;
  VkPhysicalDeviceProperties properties;
  vkGetPhysicalDeviceProperties(vk.physical, &properties);
  VkPhysicalDeviceDriverProperties driver = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES};
  VkPhysicalDeviceProperties2 both = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, .pNext = &driver};
  int has_driver = device_has_extension(vk.physical, "VK_KHR_driver_properties");
  if (has_driver) vkGetPhysicalDeviceProperties2(vk.physical, &both);
  fprintf(stderr, "vulkan: report: %s, type %d, api %u.%u.%u, driver version 0x%x, vendor 0x%x, device 0x%x\n", properties.deviceName, (int)properties.deviceType,
          VK_VERSION_MAJOR(properties.apiVersion), VK_VERSION_MINOR(properties.apiVersion), VK_VERSION_PATCH(properties.apiVersion),
          properties.driverVersion, properties.vendorID, properties.deviceID);
  if (has_driver) fprintf(stderr, "vulkan: report: driver %s (%s), id %d, conformance %u.%u.%u.%u\n", driver.driverName, driver.driverInfo, (int)driver.driverID,
                          driver.conformanceVersion.major, driver.conformanceVersion.minor, driver.conformanceVersion.subminor, driver.conformanceVersion.patch);
  VkPhysicalDeviceFeatures f;
  vkGetPhysicalDeviceFeatures(vk.physical, &f);
  fprintf(stderr, "vulkan: report: features: dualSrcBlend=%d fragmentStoresAndAtomics=%d samplerAnisotropy=%d depthClamp=%d depthBiasClamp=%d independentBlend=%d logicOp=%d "
          "fillModeNonSolid=%d wideLines=%d shaderInt16=%d shaderFloat64=%d textureCompressionBC=%d ETC2=%d ASTC_LDR=%d\n",
          f.dualSrcBlend, f.fragmentStoresAndAtomics, f.samplerAnisotropy, f.depthClamp, f.depthBiasClamp, f.independentBlend, f.logicOp, f.fillModeNonSolid,
          f.wideLines, f.shaderInt16, f.shaderFloat64, f.textureCompressionBC, f.textureCompressionETC2, f.textureCompressionASTC_LDR);
  const VkPhysicalDeviceLimits *l = &vk.limits;
  // The runtime's own needs beside each limit that matters to them.
  fprintf(stderr, "vulkan: report: limits: pushConstants %u (the runtime pushes %u) | dynamic uniform buffers per set %u, per stage %u, bound sets %u (the runtime uses %d blocks, %d sets) | "
          "uniform range %u (a block is at most %d bytes) offset alignment %llu | storage buffers per stage %u | samplers per stage %u, sampled images per stage %u (the runtime binds %d) | "
          "vertex: attributes %u, bindings %u, stride %u, attribute offset %u | fragment outputs %u, dual-source %u | anisotropy %.1f | image 2D %u, framebuffer %ux%u, viewport %ux%u, bounds %.0f..%.0f | "
          "nonCoherentAtom %llu, mapAlignment %zu, allocations %u, samplerAllocations %u, timestampPeriod %.1f\n",
          l->maxPushConstantsSize, 32u, l->maxDescriptorSetUniformBuffersDynamic, l->maxPerStageDescriptorUniformBuffers, l->maxBoundDescriptorSets, (int)GX_CONSTANT_BLOCKS, (int)SET_COUNT,
          l->maxUniformBufferRange, (int)GX_CONSTANT_BYTES, (unsigned long long)l->minUniformBufferOffsetAlignment, l->maxPerStageDescriptorStorageBuffers,
          l->maxPerStageDescriptorSamplers, l->maxPerStageDescriptorSampledImages, (int)MAX_TEXTURE_MAPS, l->maxVertexInputAttributes, l->maxVertexInputBindings,
          l->maxVertexInputBindingStride, l->maxVertexInputAttributeOffset, l->maxFragmentOutputAttachments, l->maxFragmentDualSrcAttachments, (double)l->maxSamplerAnisotropy,
          l->maxImageDimension2D, l->maxFramebufferWidth, l->maxFramebufferHeight, l->maxViewportDimensions[0], l->maxViewportDimensions[1],
          (double)l->viewportBoundsRange[0], (double)l->viewportBoundsRange[1], (unsigned long long)l->nonCoherentAtomSize, l->minMemoryMapAlignment, l->maxMemoryAllocationCount,
          l->maxSamplerAllocationCount, (double)l->timestampPeriod);
  fprintf(stderr, "vulkan: report: sample counts color 0x%x depth 0x%x\n", (unsigned)l->framebufferColorSampleCounts, (unsigned)l->framebufferDepthSampleCounts);
  // Every format the runtime creates or reads, with what it needs of each.
  const uint32_t color_needs = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT | VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
                               VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT | VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT |
                               VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
  report_format("picture and texture color R8G8B8A8_UNORM", COLOR_FORMAT, color_needs, 0);
  report_format("depth (chosen)", DEPTH_FORMAT, DEPTH_NEEDS | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT, 0);
  report_format("depth D32_SFLOAT", VK_FORMAT_D32_SFLOAT, DEPTH_NEEDS, 0);
  report_format("depth X8_D24_UNORM", VK_FORMAT_X8_D24_UNORM_PACK32, DEPTH_NEEDS, 0);
  report_format("depth D24_UNORM_S8_UINT (not used)", VK_FORMAT_D24_UNORM_S8_UINT, DEPTH_NEEDS, 0);
  if (vk.swapchain) report_format("window picture", vk.swap_format, VK_FORMAT_FEATURE_TRANSFER_DST_BIT | VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT, 0);
  else fprintf(stderr, "vulkan: report: no window picture yet (the swapchain's format is reported with WII_RECOMP_GX_DEBUG=report, which prints this block again after each swapchain)\n");
  static const struct { const char *name; VkFormat format; } vertex[] = {
    {"vertex R32G32_SFLOAT", VK_FORMAT_R32G32_SFLOAT}, {"vertex R32G32B32_SFLOAT", VK_FORMAT_R32G32B32_SFLOAT}, {"vertex R32G32B32A32_SFLOAT", VK_FORMAT_R32G32B32A32_SFLOAT},
    {"vertex R8_UINT", VK_FORMAT_R8_UINT}, {"vertex R8G8_UINT", VK_FORMAT_R8G8_UINT}, {"vertex R8G8B8A8_UINT", VK_FORMAT_R8G8B8A8_UINT},
  };
  for (size_t i = 0; i < sizeof vertex / sizeof vertex[0]; i++) report_format(vertex[i].name, vertex[i].format, 0, VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT);
  fprintf(stderr, "vulkan: report: the runtime's vertex formats are FLOAT and UINT only, never SCALED/NORMALIZED, so none of them can be missing for that reason\n");
  VkPhysicalDeviceMemoryProperties memory;
  vkGetPhysicalDeviceMemoryProperties(vk.physical, &memory);
  for (uint32_t i = 0; i < memory.memoryHeapCount; i++) fprintf(stderr, "vulkan: report: heap %u: %llu MiB, flags 0x%x\n", i, (unsigned long long)(memory.memoryHeaps[i].size >> 20), (unsigned)memory.memoryHeaps[i].flags);
  for (uint32_t i = 0; i < memory.memoryTypeCount; i++) fprintf(stderr, "vulkan: report: memory type %u: heap %u, properties 0x%x\n", i, memory.memoryTypes[i].heapIndex, (unsigned)memory.memoryTypes[i].propertyFlags);
  uint32_t count = 0;
  vkEnumerateDeviceExtensionProperties(vk.physical, 0, &count, 0);
  VkExtensionProperties *found = calloc(count ? count : 1, sizeof *found);
  vkEnumerateDeviceExtensionProperties(vk.physical, 0, &count, found);
  fprintf(stderr, "vulkan: report: %u device extensions:", count);
  for (uint32_t i = 0; i < count; i++) fprintf(stderr, " %s", found[i].extensionName);
  fprintf(stderr, "\n");
  free(found);
  fprintf(stderr, "vulkan: report: decisions: dual_source=%d (alt_no_dual=%d) bounding_box=%d push_descriptors=%d anisotropy=%.1f depth_filters_linear=%d depth_format=%d show_queue=%d\n",
          vk.dual_source, vk.alt_no_dual, vk.bounding_box, vk.push_descriptors != 0, (double)vk.max_anisotropy, vk.depth_filters_linear, (int)vk.depth_format, vk.show_queue != 0);
}

// Memory with the properties `needed`, and `wanted` too where there is some. A type that
// has them can still be out of room: a discrete card's memory the processor can also
// reach is a small heap of its own (256 MiB on NVIDIA's), and the rings don't all fit. So
// each suitable type is tried in turn, the wanted ones first.
static VkDeviceMemory allocate(VkMemoryRequirements needs, VkMemoryPropertyFlags wanted, VkMemoryPropertyFlags needed) {
  VkPhysicalDeviceMemoryProperties memory;
  vkGetPhysicalDeviceMemoryProperties(vk.physical, &memory);
  VkMemoryPropertyFlags tries[2] = {wanted | needed, needed};
  VkResult result = VK_ERROR_FEATURE_NOT_PRESENT;
  uint32_t tried = 0, tried_wanted = 0;
  for (int attempt = 0; attempt < 2; attempt++) {
    for (uint32_t i = 0; i < memory.memoryTypeCount; i++) {
      if (!(needs.memoryTypeBits & (1u << i)) || (tried & (1u << i))) continue;
      if ((memory.memoryTypes[i].propertyFlags & tries[attempt]) != tries[attempt]) continue;
      tried |= 1u << i;
      VkMemoryAllocateInfo info = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = needs.size, .memoryTypeIndex = i};
      VkDeviceMemory got;
      result = vkAllocateMemory(vk.device, &info, 0, &got);
      if (result != VK_SUCCESS) continue;
      static int told;  // Once: what's put elsewhere may be slower to reach.
      if (attempt && tried_wanted && !told++) fprintf(stderr, "vulkan: the memory wanted is full, so some goes in another kind\n");
      return got;
    }
    if (!attempt) tried_wanted = tried;
  }
  if (!tried) {
    fprintf(stderr, "stop: vulkan: no suitable memory\n");
    exit(10);
  }
  vk_check(result, "allocating memory");
  return 0;
}

void vk_create_image(VkFormat format, VkImageUsageFlags usage, uint32_t width, uint32_t height, uint32_t levels,
                     VkImage *image, VkDeviceMemory *memory) {
  vk_create_shared_image(format, usage, width, height, levels, 0, image, memory);
}

void vk_create_shared_image(VkFormat format, VkImageUsageFlags usage, uint32_t width, uint32_t height, uint32_t levels, int shared,
                            VkImage *image, VkDeviceMemory *memory) {
  uint32_t both[2] = {vk.queue_family, vk.show_family};
  int concurrent = shared && vk.show_family != vk.queue_family;
  VkImageCreateInfo info = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D, .format = format,
                            .extent = {width, height, 1}, .mipLevels = levels, .arrayLayers = 1, .samples = VK_SAMPLE_COUNT_1_BIT,
                            .tiling = VK_IMAGE_TILING_OPTIMAL, .usage = usage, .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                            .sharingMode = concurrent ? VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE,
                            .queueFamilyIndexCount = concurrent ? 2 : 0, .pQueueFamilyIndices = both};
  vk_check(vkCreateImage(vk.device, &info, 0, image), "creating an image");
  VkMemoryRequirements needs;
  vkGetImageMemoryRequirements(vk.device, *image, &needs);
  *memory = allocate(needs, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);
  vk_check(vkBindImageMemory(vk.device, *image, *memory, 0), "binding image memory");
}

VkImageView vk_create_view(VkImage image, VkFormat format, VkImageAspectFlags aspect, uint32_t levels, VkComponentMapping channels) {
  VkImageViewCreateInfo info = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = image, .viewType = VK_IMAGE_VIEW_TYPE_2D,
                                .format = format, .components = channels, .subresourceRange = {aspect, 0, levels, 0, 1}};
  VkImageView view;
  vk_check(vkCreateImageView(vk.device, &info, 0, &view), "creating an image view");
  return view;
}

void vk_retire(VkImage image, VkImageView view, VkDeviceMemory memory) {
  Frame *frame = vk.frame;
  if (frame->retired_count == frame->retired_capacity) {
    frame->retired_capacity = frame->retired_capacity ? frame->retired_capacity * 2 : 64;
    frame->retired = realloc(frame->retired, (size_t)frame->retired_capacity * sizeof *frame->retired);
  }
  frame->retired[frame->retired_count++] = (Retired){image, view, memory};
}

static Ring create_ring(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags wanted);

Ring vk_create_ring(VkDeviceSize size, VkBufferUsageFlags usage) { return create_ring(size, usage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT); }

// The processor reads these, which is many times slower from memory it doesn't cache.
Ring vk_create_read_ring(VkDeviceSize size) { return create_ring(size, VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_CACHED_BIT); }

static Ring create_ring(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags wanted) {
  Ring ring = {.size = size};
  VkBufferCreateInfo info = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = size, .usage = usage};
  vk_check(vkCreateBuffer(vk.device, &info, 0, &ring.buffer), "creating a buffer");
  VkMemoryRequirements needs;
  vkGetBufferMemoryRequirements(vk.device, ring.buffer, &needs);
  ring.memory = allocate(needs, wanted, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  vk_check(vkBindBufferMemory(vk.device, ring.buffer, ring.memory, 0), "binding buffer memory");
  vk_check(vkMapMemory(vk.device, ring.memory, 0, VK_WHOLE_SIZE, 0, (void **)&ring.mapped), "mapping a buffer");
  return ring;
}

VkDeviceSize vk_ring_take(Ring *ring, VkDeviceSize bytes, VkDeviceSize alignment) {
  VkDeviceSize at = (ring->used + alignment - 1) / alignment * alignment;
  if (at + bytes > ring->size) return (VkDeviceSize)-1;
  ring->used = at + bytes;
  return at;
}

void vk_create_frames(void) {
  ring_sizes_choose();
  for (int i = 0; i < FRAMES_IN_FLIGHT; i++) {
    Frame *frame = &vk.frames[i];
    VkCommandPoolCreateInfo pool = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, .queueFamilyIndex = vk.queue_family};
    vk_check(vkCreateCommandPool(vk.device, &pool, 0, &frame->pool), "creating a command pool");
    VkCommandBufferAllocateInfo buffers = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = frame->pool,
                                           .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 2};
    VkCommandBuffer both[2];
    vk_check(vkAllocateCommandBuffers(vk.device, &buffers, both), "creating command buffers");
    frame->draw = both[0];
    frame->upload = both[1];
    VkFenceCreateInfo fence = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    vk_check(vkCreateFence(vk.device, &fence, 0, &frame->fence), "creating a fence");
    VkSemaphoreCreateInfo semaphore = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    vk_check(vkCreateSemaphore(vk.device, &semaphore, 0, &frame->image_ready), "creating a semaphore");
    frame->vertices = vk_create_ring(ring_sizes[0], VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    frame->constants = vk_create_ring(ring_sizes[1], VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    frame->staging = vk_create_ring(ring_sizes[3], VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    frame->indices = vk_create_ring(ring_sizes[2], VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    VkDescriptorSetAllocateInfo set = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, .descriptorPool = vk.descriptor_pool,
                                       .descriptorSetCount = 1, .pSetLayouts = &vk.set_layouts[SET_CONSTANTS]};
    vk_check(vkAllocateDescriptorSets(vk.device, &set, &frame->constant_set), "allocating a descriptor set");
    VkDescriptorBufferInfo blocks[GX_CONSTANT_BLOCKS];
    VkWriteDescriptorSet writes[GX_CONSTANT_BLOCKS];
    for (uint32_t block = 0; block < GX_CONSTANT_BLOCKS; block++) {
      blocks[block] = (VkDescriptorBufferInfo){frame->constants.buffer, 0, GX_CONSTANT_BYTES};
      writes[block] = (VkWriteDescriptorSet){.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = frame->constant_set, .dstBinding = block,
                                             .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, .pBufferInfo = &blocks[block]};
    }
    vkUpdateDescriptorSets(vk.device, GX_CONSTANT_BLOCKS, writes, 0, 0);
  }
}

// How pictures meet the display. Waiting for the display's refresh (FIFO) shows each one
// whole, with no tearing; MAILBOX shows the newest at each refresh; IMMEDIATE shows each
// at once and tears. WII_RECOMP_PRESENT=fifo|mailbox|immediate chooses; otherwise the
// first of FIFO, MAILBOX, IMMEDIATE that the display offers. FIFO is always offered.
static VkPresentModeKHR choose_present_mode(void) {
  static const struct { const char *name; VkPresentModeKHR mode; } named[] = {
    {"fifo", VK_PRESENT_MODE_FIFO_KHR}, {"mailbox", VK_PRESENT_MODE_MAILBOX_KHR}, {"immediate", VK_PRESENT_MODE_IMMEDIATE_KHR}};
  uint32_t count = 0;
  vkGetPhysicalDeviceSurfacePresentModesKHR(vk.physical, vk.surface, &count, 0);
  VkPresentModeKHR *modes = calloc(count, sizeof *modes);
  vkGetPhysicalDeviceSurfacePresentModesKHR(vk.physical, vk.surface, &count, modes);
  const char *asked = getenv("WII_RECOMP_PRESENT");
  int chosen = -1;
  for (size_t n = 0; n < sizeof named / sizeof named[0] && chosen < 0; n++) {
    if (asked && strcmp(asked, named[n].name)) continue;
    for (uint32_t m = 0; m < count; m++) {
      if (modes[m] == named[n].mode) chosen = (int)n;
    }
  }
  if (chosen < 0) {
    if (asked) fprintf(stderr, "vulkan: the display doesn't offer %s, so pictures wait for its refresh\n", asked);
    chosen = 0;
  }
  fprintf(stderr, "vulkan: pictures shown with %s (the display offers", named[chosen].name);
  for (uint32_t m = 0; m < count; m++) {
    for (size_t n = 0; n < sizeof named / sizeof named[0]; n++) {
      if (modes[m] == named[n].mode) fprintf(stderr, " %s", named[n].name);
    }
  }
  fprintf(stderr, ")\n");
  free(modes);
  return named[chosen].mode;
}

// Plain 8-bit output, as the picture's values are already what the screen should show.
// Where the system would convert colors for the display, as the Mac does for anything
// labelled sRGB, passing them through unchanged matches what OpenGL shows.
static VkSurfaceFormatKHR choose_surface_format(void) {
  uint32_t count = 0;
  vkGetPhysicalDeviceSurfaceFormatsKHR(vk.physical, vk.surface, &count, 0);
  VkSurfaceFormatKHR *formats = calloc(count, sizeof *formats);
  vkGetPhysicalDeviceSurfaceFormatsKHR(vk.physical, vk.surface, &count, formats);
  VkSurfaceFormatKHR chosen = formats[0];
  int score = 0;
  for (uint32_t i = 0; i < count; i++) {
    int plain = formats[i].format == VK_FORMAT_B8G8R8A8_UNORM || formats[i].format == VK_FORMAT_R8G8B8A8_UNORM;
    int found = plain ? (formats[i].colorSpace == VK_COLOR_SPACE_PASS_THROUGH_EXT ? 2 : 1) : 0;
    if (found > score) {
      chosen = formats[i];
      score = found;
    }
  }
  fprintf(stderr, "vulkan: window pictures as format %d, color space %s (of %u offered:", (int)chosen.format,
          chosen.colorSpace == VK_COLOR_SPACE_PASS_THROUGH_EXT ? "passed through" : chosen.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR ? "sRGB" : "other", count);
  for (uint32_t i = 0; i < count; i++) fprintf(stderr, " %d/%d", (int)formats[i].format, (int)formats[i].colorSpace);
  fprintf(stderr, ")\n");
  free(formats);
  return chosen;
}

int vk_swapchain_matches_window(void) {
  int width, height;
  window_size(&width, &height);
  return !width || !height || (vk.swap_window_width == width && vk.swap_window_height == height);
}

static const char *transform_name(VkSurfaceTransformFlagBitsKHR transform) {
  switch (transform) {
    case VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR: return "identity";
    case VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR: return "rotate 90";
    case VK_SURFACE_TRANSFORM_ROTATE_180_BIT_KHR: return "rotate 180";
    case VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR: return "rotate 270";
    case VK_SURFACE_TRANSFORM_HORIZONTAL_MIRROR_BIT_KHR: return "mirror";
    default: return "other";
  }
}

static int swapchain_depth;  // vk_create_swapchain through vk_recreate_surface and back: once.

// Whether a present or an acquire says the swapchain must be made again. A swapchain made with a
// transform of its own choosing (see vk_create_swapchain) is always "suboptimal" by the system's
// reckoning, which is no reason to rebuild it every picture. A lost surface is rebuilt too.
int vk_swapchain_stale(VkResult result) {
  if (result == VK_ERROR_SURFACE_LOST_KHR) {
    vk.surface_lost = 1;
    return 1;
  }
  return result == VK_ERROR_OUT_OF_DATE_KHR || (result == VK_SUBOPTIMAL_KHR && !vk.accept_suboptimal);
}

// The swapchain again, from a new surface where the old one was lost.
void vk_swapchain_renew(void) {
  if (vk.surface_lost) vk_recreate_surface();
  else vk_create_swapchain();
}

// Where in the swapchain's pictures the game's picture goes: the whole of it, or on Android the
// largest 4:3 (or 16:9, by the console's own setting) rectangle in the middle. WII_RECOMP_STRETCH=1 fills the window.
void vk_window_rect(int *x, int *y, int *width, int *height) {
  *x = *y = 0;
  *width = (int)vk.swap_extent.width;
  *height = (int)vk.swap_extent.height;
#ifdef WII_PLATFORM_SDL
  static int stretch = -1;
  if (stretch < 0) stretch = getenv("WII_RECOMP_STRETCH") != 0;
  if (!stretch && *width > 0 && *height > 0) window_letterbox(*width, *height, x, y, width, height);
#endif
}

// The window's surface is gone (an Android app that went to the background has none) or a new one is
// wanted: wait for the card, drop the swapchain and the surface, make both again from the window.
// Returns 0 while the window can't give a surface; pictures are then simply not shown, and every so
// often the next present asks again. [H] Unproven on a device: nothing has been through a real surface loss yet.
int vk_recreate_surface(void) {
  if (!window_handle()) return 0;
  vkDeviceWaitIdle(vk.device);
  vk_fsr_reset();
  vk_overlay_reset();
  for (uint32_t i = 0; i < vk.swap_count; i++) vkDestroySemaphore(vk.device, vk.swap_done[i], 0);
  vk.swap_count = 0;
  if (vk.swapchain) vkDestroySwapchainKHR(vk.device, vk.swapchain, 0);
  vk.swapchain = VK_NULL_HANDLE;
  if (vk.surface) vkDestroySurfaceKHR(vk.instance, vk.surface, 0);
  vk.surface = VK_NULL_HANDLE;
  VkSurfaceKHR fresh = VK_NULL_HANDLE;
  VkResult made = glfwCreateWindowSurface(vk.instance, window_handle(), 0, &fresh);
  if (made != VK_SUCCESS || !fresh) {
    vk.surface_lost = 1;
    static int told;
    if (!told++) fprintf(stderr, "vulkan: the window has no surface now (%d), will try again\n", (int)made);
    return 0;
  }
  VkBool32 presents = VK_FALSE;
  vkGetPhysicalDeviceSurfaceSupportKHR(vk.physical, vk.queue_family, fresh, &presents);
  if (!presents) {
    vkDestroySurfaceKHR(vk.instance, fresh, 0);
    fprintf(stderr, "vulkan: the new surface can't be shown on this queue\n");
    vk.surface_lost = 1;
    return 0;
  }
  vk.surface = fresh;
  vk.surface_lost = 0;
  fprintf(stderr, "vulkan: a new surface for the window\n");
  vk_create_swapchain();
  return vk.swapchain != VK_NULL_HANDLE;
}

void vk_create_swapchain(void) {
  VkSurfaceCapabilitiesKHR capabilities;
  VkResult read = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vk.physical, vk.surface, &capabilities);
  if (read == VK_ERROR_SURFACE_LOST_KHR) {
    vk.surface_lost = 1;
    if (!swapchain_depth) {
      swapchain_depth++;
      vk_recreate_surface();
      swapchain_depth--;
    }
    return;
  }
  vk_check(read, "reading the surface");
  int width, height;
  window_size(&width, &height);
  VkExtent2D extent = capabilities.currentExtent;
  VkSurfaceTransformFlagBitsKHR transform = capabilities.currentTransform;
  vk.accept_suboptimal = 0;
#ifdef __ANDROID__
  // A phone's panel is portrait, and the system's own transform is then a quarter turn, with the extent
  // the panel's (portrait). Drawing the game's picture into that unrotated shows it on its side and
  // stretched. Pre-rotating each picture would save the compositor work; instead the swapchain says
  // "identity" and has the window's own (landscape) extent, so the system turns the finished picture,
  // in the display hardware where it can or with one more pass of its own where not. The cost is that
  // path's power and latency, and a system that calls every picture "suboptimal" for it (vk_swapchain_stale).
  if (transform != VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR && (capabilities.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)) {
    // The window's own pixel size is the picture's orientation whichever way currentExtent is reported (the
    // docs and drivers differ), so it is the extent here, within what the surface allows.
    if (width > 0 && height > 0) extent = (VkExtent2D){(uint32_t)width, (uint32_t)height};
    fprintf(stderr, "vulkan: the surface's transform is %s (its extent %ux%u); making the swapchain %s and the window's %ux%u, so the system turns it\n",
            transform_name(transform), capabilities.currentExtent.width, capabilities.currentExtent.height, transform_name(VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR),
            extent.width, extent.height);
    transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    vk.accept_suboptimal = 1;
  }
#endif
  if (extent.width == UINT32_MAX) extent = (VkExtent2D){(uint32_t)width, (uint32_t)height};
  if (!extent.width || !extent.height) return;  // Minimised or hidden: nothing to draw into. The next present asks again.
  if (extent.width < capabilities.minImageExtent.width) extent.width = capabilities.minImageExtent.width;
  if (extent.height < capabilities.minImageExtent.height) extent.height = capabilities.minImageExtent.height;
  if (extent.width > capabilities.maxImageExtent.width) extent.width = capabilities.maxImageExtent.width;
  if (extent.height > capabilities.maxImageExtent.height) extent.height = capabilities.maxImageExtent.height;
  vkDeviceWaitIdle(vk.device);
  vk_fsr_reset();  // Its framebuffers name the pictures of the swapchain being replaced.
  vk_overlay_reset();
  vk.swap_window_width = width;
  vk.swap_window_height = height;
  uint32_t images = capabilities.minImageCount > WANTED_SWAP_IMAGES ? capabilities.minImageCount : WANTED_SWAP_IMAGES;
  if (capabilities.maxImageCount && images > capabilities.maxImageCount) images = capabilities.maxImageCount;
  VkSurfaceFormatKHR format = choose_surface_format();
  VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  if (capabilities.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  vk.swap_drawable = (usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) != 0;
  VkSwapchainKHR old = vk.swapchain;
  VkSwapchainCreateInfoKHR info = {
    .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR, .surface = vk.surface, .minImageCount = images,
    .imageFormat = format.format, .imageColorSpace = format.colorSpace, .imageExtent = extent, .imageArrayLayers = 1,
    // Drawn into by the FSR upscale, or copied into when it is off.
    .imageUsage = usage,
    .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE, .preTransform = transform,
    .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, .presentMode = choose_present_mode(), .clipped = VK_TRUE, .oldSwapchain = old,
  };
  static int described;
  if (!described++ || old == VK_NULL_HANDLE) {
    fprintf(stderr, "vulkan: surface: current %ux%u, transform %s, min/max extent %ux%u..%ux%u, %u..%u images, usage 0x%x, supported transforms 0x%x, window %dx%d; swapchain %ux%u, %u images, transform %s\n",
            capabilities.currentExtent.width, capabilities.currentExtent.height, transform_name(capabilities.currentTransform),
            capabilities.minImageExtent.width, capabilities.minImageExtent.height, capabilities.maxImageExtent.width, capabilities.maxImageExtent.height,
            capabilities.minImageCount, capabilities.maxImageCount, (unsigned)capabilities.supportedUsageFlags, (unsigned)capabilities.supportedTransforms,
            width, height, extent.width, extent.height, images, transform_name(transform));
  }
  VkSwapchainKHR fresh = VK_NULL_HANDLE;
  VkResult created = vkCreateSwapchainKHR(vk.device, &info, 0, &fresh);
  if (created == VK_ERROR_SURFACE_LOST_KHR) {
    // The old swapchain belonged to the lost surface: a new surface starts without it.
    vk.surface_lost = 1;
    if (!swapchain_depth) {
      swapchain_depth++;
      vk_recreate_surface();
      swapchain_depth--;
    }
    return;
  }
  vk_check(created, "creating the swapchain");
  vk.swapchain = fresh;
  for (uint32_t i = 0; i < vk.swap_count; i++) vkDestroySemaphore(vk.device, vk.swap_done[i], 0);
  if (old) vkDestroySwapchainKHR(vk.device, old, 0);
  vk.swap_extent = extent;
  vk.swap_format = format.format;
  {
    int x, y, w, h;
    vk_window_rect(&x, &y, &w, &h);
    fprintf(stderr, "picture: window %ux%u, the game's picture goes in %dx%d at %d,%d (the game draws at %dx%d)\n", extent.width, extent.height, w, h, x, y,
            EFB_WIDTH * EFB_SCALE, EFB_HEIGHT * EFB_SCALE);
  }
  vkGetSwapchainImagesKHR(vk.device, vk.swapchain, &vk.swap_count, 0);
  vk.swap_images = realloc(vk.swap_images, vk.swap_count * sizeof *vk.swap_images);
  vk.swap_done = realloc(vk.swap_done, vk.swap_count * sizeof *vk.swap_done);
  vkGetSwapchainImagesKHR(vk.device, vk.swapchain, &vk.swap_count, vk.swap_images);
  VkSemaphoreCreateInfo semaphore = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
  for (uint32_t i = 0; i < vk.swap_count; i++) vk_check(vkCreateSemaphore(vk.device, &semaphore, 0, &vk.swap_done[i]), "creating a semaphore");
  if (gx_debug("report")) vk_report_device();
}

VkCommandBuffer vk_upload_commands(void) {
  Frame *frame = vk.frame;
  if (!frame->upload_started) {
    VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    vk_check(vkBeginCommandBuffer(frame->upload, &begin), "starting commands");
    frame->upload_started = 1;
  }
  return frame->upload;
}

// Where the driver can't push textures, each change takes a set from this frame's pools.
static void add_texture_pool(Frame *frame) {
  if (frame->texture_pool_count == MAX_TEXTURE_POOLS) { fprintf(stderr, "stop: vulkan: too many texture changes in one frame\n"); exit(10); }
  VkDescriptorPoolSize size = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, TEXTURE_SETS_PER_POOL * MAX_TEXTURE_MAPS};
  VkDescriptorPoolCreateInfo pool = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, .maxSets = TEXTURE_SETS_PER_POOL, .poolSizeCount = 1, .pPoolSizes = &size};
  vk_check(vkCreateDescriptorPool(vk.device, &pool, 0, &frame->texture_pools[frame->texture_pool_count++]), "creating a descriptor pool");
}

VkDescriptorSet vk_texture_set(Frame *frame) {
  if (frame->texture_sets_used % TEXTURE_SETS_PER_POOL == 0 && frame->texture_sets_used / TEXTURE_SETS_PER_POOL == frame->texture_pool_count) {
    add_texture_pool(frame);
  }
  VkDescriptorSetAllocateInfo info = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                                      .descriptorPool = frame->texture_pools[frame->texture_sets_used / TEXTURE_SETS_PER_POOL],
                                      .descriptorSetCount = 1, .pSetLayouts = &vk.set_layouts[SET_TEXTURES]};
  VkDescriptorSet set;
  vk_check(vkAllocateDescriptorSets(vk.device, &info, &set), "allocating a descriptor set");
  frame->texture_sets_used++;
  return set;
}

// Waits for this frame's last turn on the card, then readies it for another.
void vk_frame_begin(Frame *frame) {
  if (frame->submitted) {
    TIMELINE_BEGIN("waiting for the chip");
    vk_check(vkWaitForFences(vk.device, 1, &frame->fence, VK_TRUE, UINT64_MAX), "waiting for the card");
    TIMELINE_END("waiting for the chip");
    vk_check(vkResetFences(vk.device, 1, &frame->fence), "resetting a fence");
    frame->submitted = 0;
  }
  for (int i = 0; i < frame->retired_count; i++) {
    Retired *old = &frame->retired[i];
    vkDestroyImageView(vk.device, old->view, 0);
    vkDestroyImage(vk.device, old->image, 0);
    vkFreeMemory(vk.device, old->memory, 0);
  }
  frame->retired_count = 0;
  for (int i = 0; i < frame->texture_pool_count; i++) vk_check(vkResetDescriptorPool(vk.device, frame->texture_pools[i], 0), "resetting a descriptor pool");
  frame->texture_sets_used = 0;
  ring_peaks_note(frame);
  frame->vertices.used = frame->constants.used = frame->staging.used = frame->indices.used = 0;
  frame->upload_started = 0;
  vk_check(vkResetCommandPool(vk.device, frame->pool, 0), "resetting commands");
  VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
  vk_check(vkBeginCommandBuffer(frame->draw, &begin), "starting commands");
  vk.frame = frame;
}

void vk_frame_submit(Frame *frame, VkSemaphore wait, VkSemaphore signal) {
  VkCommandBuffer buffers[2];
  uint32_t count = 0;
  if (frame->upload_started) {
    vk_check(vkEndCommandBuffer(frame->upload), "finishing commands");
    buffers[count++] = frame->upload;
  }
  vk_check(vkEndCommandBuffer(frame->draw), "finishing commands");
  buffers[count++] = frame->draw;
  // The window picture is copied into when FSR is off, and drawn into when it is on.
  VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  VkSubmitInfo info = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .waitSemaphoreCount = wait ? 1 : 0, .pWaitSemaphores = &wait,
                       .pWaitDstStageMask = &wait_stage, .commandBufferCount = count, .pCommandBuffers = buffers,
                       .signalSemaphoreCount = signal ? 1 : 0, .pSignalSemaphores = &signal};
  vk_check(vkQueueSubmit(vk.queue, 1, &info, frame->fence), "submitting commands");
  frame->submitted = 1;
}
