// The DLSS experiment: WII_RECOMP_NGX=1 asks NVIDIA's NGX core whether DLSS Super
// Resolution can run on the Vulkan device the console made. The core is loaded from
// the installed driver at run time, and the DLSS feature library is looked for in
// WII_RECOMP_NGX_FEATURE_PATH (default: beside the console), so nothing NVIDIA ships
// is part of this project. Success means the device, driver and feature library accept
// NGX and a 1280x720 to 1920x1080 feature writes a synthetic frame. It does not mean a
// game picture is upscaled: the runtime still has no motion vectors or jitter, which
// DLSS needs, so the game keeps its present path.
//
// The declarations below mirror the public NGX SDK's ABI. The SDK's own headers are
// proprietary, so they stay out of this tree and only the few shapes used here are
// repeated.

#include "gx_vulkan_ngx.h"

#ifndef _WIN32

int vk_ngx_enabled(void) { return 0; }
int vk_ngx_begin(void) { return 0; }
void vk_ngx_add_instance_extensions(const char **names, uint32_t *count, uint32_t most) { (void)names, (void)count, (void)most; }
void vk_ngx_check_support(VkInstance instance, VkPhysicalDevice physical) { (void)instance, (void)physical; }
void vk_ngx_add_device_extensions(const char **names, uint32_t *count, uint32_t most) { (void)names, (void)count, (void)most; }
int vk_ngx_wants_feature_chain(void) { return 0; }
void vk_ngx_init_device(VkInstance instance, VkPhysicalDevice physical, VkDevice device, VkQueue queue,
                        uint32_t queue_family) {
  (void)instance, (void)physical, (void)device, (void)queue, (void)queue_family;
}
void vk_ngx_shutdown(VkDevice device) { (void)device; }

#else

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

// NGX's enums are int-sized in the SDK's C++ declarations.
typedef int NgxResult;
typedef int NgxEngineType;
typedef int NgxVersion;
typedef int NgxFeature;
typedef int NgxIdentifierType;

enum {
  NGX_SUCCESS = 0x1,
  NGX_FEATURE_SUPER_SAMPLING = 1,
  NGX_IDENTIFIER_PROJECT_ID = 1,
  NGX_ENGINE_CUSTOM = 0,
  NGX_VERSION_API = 0x0000015,  // SDK 1.5.0, the value the public header uses.
  NGX_LOGGING_VERBOSE = 2,
  MOST_NGX_EXTENSIONS = 8,
};

#define NGX_PROJECT_ID "7c1e4b2a-9d3f-4a86-b5e2-0c8f6d1a3b47"
#define NGX_ENGINE_VERSION "1.0"
#define NGX_PARAMETER_AVAILABLE "SuperSampling.Available"
#define NGX_PARAMETER_NEEDS_DRIVER "SuperSampling.NeedsUpdatedDriver"
#define NGX_PARAMETER_WIDTH "Width"
#define NGX_PARAMETER_HEIGHT "Height"
#define NGX_PARAMETER_OUT_WIDTH "OutWidth"
#define NGX_PARAMETER_OUT_HEIGHT "OutHeight"
#define NGX_PARAMETER_PERF_QUALITY "PerfQualityValue"
#define NGX_PARAMETER_CREATE_FLAGS "DLSS.Feature.Create.Flags"
#define NGX_PARAMETER_CREATION_MASK "CreationNodeMask"
#define NGX_PARAMETER_VISIBILITY_MASK "VisibilityNodeMask"
#define NGX_PARAMETER_COLOR "Color"
#define NGX_PARAMETER_DEPTH "Depth"
#define NGX_PARAMETER_MOTION_VECTORS "MotionVectors"
#define NGX_PARAMETER_OUTPUT "Output"
#define NGX_PARAMETER_RESET "Reset"
#define NGX_PARAMETER_SHARPNESS "Sharpness"
#define NGX_PARAMETER_JITTER_X "Jitter.Offset.X"
#define NGX_PARAMETER_JITTER_Y "Jitter.Offset.Y"
#define NGX_PARAMETER_MV_SCALE_X "MV.Scale.X"
#define NGX_PARAMETER_MV_SCALE_Y "MV.Scale.Y"

// MaxQuality is the 1.5x mode, so a 1280x720 picture grows into 1920x1080.
enum { NGX_PERF_QUALITY_MAX_QUALITY = 2 };

typedef struct {
  const char *ProjectId;
  NgxEngineType EngineType;
  const char *EngineVersion;
} NgxProjectDescription;

// IdentifierType then a union: 32 bytes on x64, as the SDK's C++ struct is.
typedef struct {
  NgxIdentifierType IdentifierType;
  union {
    NgxProjectDescription ProjectDesc;
    unsigned long long ApplicationId;
  } v;
} NgxApplicationIdentifier;

typedef struct {
  const wchar_t *const *Path;
  unsigned int Length;
} NgxPathListInfo;

typedef void (*NgxLogCallback)(const char *, int, NgxFeature);

// NVSDK_NGX_LoggingInfo, 16 bytes on x64 after the trailing bool's padding.
typedef struct {
  NgxLogCallback LoggingCallback;
  int MinimumLoggingLevel;
  unsigned char DisableOtherLoggingSinks;
  unsigned char pad[3];
} NgxLoggingInfo;

typedef struct {
  NgxPathListInfo PathListInfo;
  void *InternalData;
  NgxLoggingInfo LoggingInfo;
} NgxFeatureCommonInfo;

_Static_assert(offsetof(NgxFeatureCommonInfo, LoggingInfo) == 24, "logging block must follow the path list");
_Static_assert(sizeof(NgxFeatureCommonInfo) == 40, "NVSDK_NGX_FeatureCommonInfo is 40 bytes on x64");

typedef struct {
  NgxVersion SDKVersion;
  NgxFeature FeatureID;
  NgxApplicationIdentifier Identifier;
  const wchar_t *ApplicationDataPath;
  const NgxFeatureCommonInfo *FeatureInfo;
} NgxFeatureDiscoveryInfo;

typedef struct {
  unsigned int FeatureSupported;
  unsigned int MinHWArchitecture;
  char MinOSVersion[255];
} NgxFeatureRequirement;

typedef struct NgxParameter NgxParameter;

typedef NgxResult (*NgxInitProjectId)(const char *, NgxEngineType, const char *, const wchar_t *, VkInstance, VkPhysicalDevice,
                                      VkDevice, NgxVersion, const NgxFeatureCommonInfo *);
typedef NgxResult (*NgxGetFeatureRequirements)(VkInstance, VkPhysicalDevice, const NgxFeatureDiscoveryInfo *, NgxFeatureRequirement *);
typedef NgxResult (*NgxGetInstanceExtensions)(const NgxFeatureDiscoveryInfo *, uint32_t *, VkExtensionProperties **);
typedef NgxResult (*NgxGetDeviceExtensions)(VkInstance, VkPhysicalDevice, const NgxFeatureDiscoveryInfo *, uint32_t *,
                                            VkExtensionProperties **);
typedef NgxResult (*NgxGetCapabilityParameters)(NgxParameter **);
typedef NgxResult (*NgxShutdown)(VkDevice);
typedef NgxResult (*NgxAllocateParameters)(NgxParameter **);
typedef NgxResult (*NgxCreateFeature1)(VkDevice, VkCommandBuffer, NgxFeature, NgxParameter *, void **);
typedef NgxResult (*NgxEvaluateFeature)(VkCommandBuffer, const void *, const NgxParameter *, void *);
typedef NgxResult (*NgxReleaseFeature)(void *);
typedef NgxResult (*NgxDestroyParameters)(NgxParameter *);

// NVSDK_NGX_Parameter is an abstract C++ class. Its eight Set overloads come first:
// unsigned long long, float, double, unsigned int, int, two graphics-resource
// pointers and void*. Then the Gets: unsigned long long, float, double, unsigned
// int, int, two graphics-resource pointers and void*. So index 3 sets an unsigned
// int, index 4 sets an int, and index 12 reads an int.
typedef void (*NgxParameterSetFloat)(NgxParameter *, const char *, float);
typedef void (*NgxParameterSetUInt)(NgxParameter *, const char *, unsigned int);
typedef void (*NgxParameterSetInt)(NgxParameter *, const char *, int);
typedef void (*NgxParameterSetVoid)(NgxParameter *, const char *, void *);
typedef NgxResult (*NgxParameterGetInt)(const NgxParameter *, const char *, int *);
typedef NgxResult (*NgxParameterGetVoid)(const NgxParameter *, const char *, void **);

// NVSDK_NGX_Resource_VK from nvsdk_ngx_defs_vk.h. The unions and trailing bool are
// part of the ABI the core reads, so the shapes are repeated with their C types.
typedef struct {
  VkImageView ImageView;
  VkImage Image;
  VkImageSubresourceRange SubresourceRange;
  VkFormat Format;
  unsigned int Width;
  unsigned int Height;
} NgxImageViewInfo;

typedef struct {
  union {
    NgxImageViewInfo ImageViewInfo;
  } Resource;
  int Type;  // NVSDK_NGX_RESOURCE_VK_TYPE_VK_IMAGEVIEW
  unsigned char ReadWrite;
} NgxResource;

_Static_assert(offsetof(NgxResource, Resource) == 0, "resource union must come first");
_Static_assert(offsetof(NgxResource, Type) == sizeof(NgxImageViewInfo), "resource type must follow the union");
_Static_assert(sizeof(NgxResource) == 56, "NVSDK_NGX_Resource_VK is 56 bytes on x64");

static struct {
  int asked, loaded, supported, init_possible;
  HMODULE core;
  wchar_t core_path[MAX_PATH];
  wchar_t feature_dir[MAX_PATH];
  wchar_t app_data[MAX_PATH];
  const wchar_t *feature_paths[1];
  NgxFeatureCommonInfo feature_info;
  NgxInitProjectId init_project_id;
  NgxGetFeatureRequirements get_requirements;
  NgxGetInstanceExtensions get_instance_extensions;
  NgxGetDeviceExtensions get_device_extensions;
  NgxGetCapabilityParameters get_capability_parameters;
  NgxShutdown shutdown;
  NgxAllocateParameters allocate_parameters;
  NgxCreateFeature1 create_feature;
  NgxEvaluateFeature evaluate_feature;
  NgxReleaseFeature release_feature;
  NgxDestroyParameters destroy_parameters;
  int desired, reported;  // DLSS's answer, so it is printed once.
  char required[MOST_NGX_EXTENSIONS][VK_MAX_EXTENSION_NAME_SIZE];
  uint32_t required_count;
  char instance_names[MOST_NGX_EXTENSIONS][VK_MAX_EXTENSION_NAME_SIZE];
  uint32_t instance_name_count;
} ngx;

static void to_utf8(const wchar_t *from, char *to, int bytes) {
  if (!WideCharToMultiByte(CP_UTF8, 0, from, -1, to, bytes, 0, 0)) {
    to[0] = 0;
  }
}

static void ngx_log(const char *message, int level, int source) {
  (void)level, (void)source;
  if (message) fprintf(stderr, "ngx core: %s\n", message);
}

static int module_directory(wchar_t *out, DWORD chars) {
  DWORD length = GetModuleFileNameW(0, out, chars);
  if (!length || length >= chars) return 0;
  wchar_t *slash = wcsrchr(out, L'\\');
  if (!slash) return 0;
  *slash = 0;
  return 1;
}

static HMODULE load_core_from(const wchar_t *path) {
  HMODULE module = LoadLibraryW(path);
  if (module) {
    wcscpy(ngx.core_path, path);
    return module;
  }
  return 0;
}

// The driver keeps the core in its DriverStore folder, whose exact name changes with
// every driver. Look for `_nvngx.dll` in each nv*.inf folder, newest names first is
// not needed: the one the loader can open is the one the driver registered.
static HMODULE load_core_from_driver_store(void) {
  static const wchar_t *const store = L"C:\\Windows\\System32\\DriverStore\\FileRepository";
  wchar_t pattern[MAX_PATH];
  swprintf(pattern, MAX_PATH, L"%s\\nv*", store);
  WIN32_FIND_DATAW found;
  HANDLE search = FindFirstFileW(pattern, &found);
  if (search == INVALID_HANDLE_VALUE) return 0;
  HMODULE module = 0;
  do {
    if (!(found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
    wchar_t path[MAX_PATH];
    swprintf(path, MAX_PATH, L"%s\\%s\\_nvngx.dll", store, found.cFileName);
    module = load_core_from(path);
  } while (!module && FindNextFileW(search, &found));
  FindClose(search);
  return module;
}

static HMODULE load_core(void) {
  wchar_t dir[MAX_PATH];
  if (module_directory(dir, MAX_PATH)) {
    wchar_t path[MAX_PATH];
    swprintf(path, MAX_PATH, L"%s\\_nvngx.dll", dir);
    if (load_core_from(path)) return ngx.core;
    swprintf(path, MAX_PATH, L"%s\\nvngx.dll", dir);
    if (load_core_from(path)) return ngx.core;
  }
  wchar_t system[MAX_PATH];
  if (GetSystemDirectoryW(system, MAX_PATH)) {
    wchar_t path[MAX_PATH];
    swprintf(path, MAX_PATH, L"%s\\_nvngx.dll", system);
    if (load_core_from(path)) return ngx.core;
  }
  return load_core_from_driver_store();
}

static int resolve(void) {
  struct { void **slot; const char *name; } wanted[] = {
    {(void **)&ngx.init_project_id, "NVSDK_NGX_VULKAN_Init_ProjectID"},
    {(void **)&ngx.get_requirements, "NVSDK_NGX_VULKAN_GetFeatureRequirements"},
    {(void **)&ngx.get_instance_extensions, "NVSDK_NGX_VULKAN_GetFeatureInstanceExtensionRequirements"},
    {(void **)&ngx.get_device_extensions, "NVSDK_NGX_VULKAN_GetFeatureDeviceExtensionRequirements"},
    {(void **)&ngx.get_capability_parameters, "NVSDK_NGX_VULKAN_GetCapabilityParameters"},
    {(void **)&ngx.shutdown, "NVSDK_NGX_VULKAN_Shutdown1"},
    {(void **)&ngx.allocate_parameters, "NVSDK_NGX_VULKAN_AllocateParameters"},
    {(void **)&ngx.create_feature, "NVSDK_NGX_VULKAN_CreateFeature1"},
    {(void **)&ngx.evaluate_feature, "NVSDK_NGX_VULKAN_EvaluateFeature"},
    {(void **)&ngx.release_feature, "NVSDK_NGX_VULKAN_ReleaseFeature"},
    {(void **)&ngx.destroy_parameters, "NVSDK_NGX_VULKAN_DestroyParameters"},
  };
  for (size_t i = 0; i < sizeof wanted / sizeof wanted[0]; i++) {
    *wanted[i].slot = GetProcAddress(ngx.core, wanted[i].name);
    if (!*wanted[i].slot) {
      fprintf(stderr, "ngx: the core has no %s, so DLSS stays off\n", wanted[i].name);
      return 0;
    }
  }
  return 1;
}

static NgxFeatureDiscoveryInfo discovery(void) {
  NgxFeatureDiscoveryInfo info;
  memset(&info, 0, sizeof info);
  info.SDKVersion = NGX_VERSION_API;
  info.FeatureID = NGX_FEATURE_SUPER_SAMPLING;
  info.Identifier.IdentifierType = NGX_IDENTIFIER_PROJECT_ID;
  info.Identifier.v.ProjectDesc.ProjectId = NGX_PROJECT_ID;
  info.Identifier.v.ProjectDesc.EngineType = NGX_ENGINE_CUSTOM;
  info.Identifier.v.ProjectDesc.EngineVersion = NGX_ENGINE_VERSION;
  info.ApplicationDataPath = ngx.app_data;
  info.FeatureInfo = &ngx.feature_info;
  return info;
}

static int makes_extension_list(VkPhysicalDevice device, const char *name) {
  uint32_t count = 0;
  vkEnumerateDeviceExtensionProperties(device, 0, &count, 0);
  VkExtensionProperties *found = calloc(count, sizeof *found);
  if (!found) return 0;
  vkEnumerateDeviceExtensionProperties(device, 0, &count, found);
  int has = 0;
  for (uint32_t i = 0; i < count; i++) has |= strcmp(found[i].extensionName, name) == 0;
  free(found);
  return has;
}

int vk_ngx_enabled(void) { return ngx.asked; }

int vk_ngx_begin(void) {
  const char *asked = getenv("WII_RECOMP_NGX");
  ngx.asked = asked && *asked && strcmp(asked, "0");
  if (!ngx.asked) return 0;

  wchar_t dir[MAX_PATH];
  if (!module_directory(dir, MAX_PATH)) {
    fprintf(stderr, "ngx: the console can't find its own folder, so DLSS stays off\n");
    return 0;
  }
  DWORD feature_length = GetEnvironmentVariableW(L"WII_RECOMP_NGX_FEATURE_PATH", ngx.feature_dir, MAX_PATH);
  if (!feature_length || feature_length >= MAX_PATH) {
    wcscpy(ngx.feature_dir, dir);
  }
  swprintf(ngx.app_data, MAX_PATH, L"%s\\ngx-data", dir);

  wchar_t feature_dll[MAX_PATH];
  swprintf(feature_dll, MAX_PATH, L"%s\\nvngx_dlss.dll", ngx.feature_dir);
  if (GetFileAttributesW(feature_dll) == INVALID_FILE_ATTRIBUTES) {
    char shown[MAX_PATH * 3];
    to_utf8(ngx.feature_dir, shown, sizeof shown);
    fprintf(stderr, "ngx: no nvngx_dlss.dll in %s; set WII_RECOMP_NGX_FEATURE_PATH to its folder\n", shown);
  }

  ngx.core = load_core();
  if (!ngx.core) {
    fprintf(stderr, "ngx: no NGX core (_nvngx.dll) from the driver, so DLSS stays off\n");
    return 0;
  }
  char shown[MAX_PATH * 3];
  to_utf8(ngx.core_path, shown, sizeof shown);
  fprintf(stderr, "ngx: core %s\n", shown);
  if (!resolve()) {
    FreeLibrary(ngx.core);
    ngx.core = 0;
    return 0;
  }
  CreateDirectoryW(ngx.app_data, 0);
  memset(&ngx.feature_info, 0, sizeof ngx.feature_info);
  ngx.feature_paths[0] = ngx.feature_dir;
  ngx.feature_info.PathListInfo.Path = ngx.feature_paths;
  ngx.feature_info.PathListInfo.Length = 1;
  const char *verbose = getenv("WII_RECOMP_NGX_LOG");
  if (verbose && *verbose && strcmp(verbose, "0")) {
    ngx.feature_info.LoggingInfo.LoggingCallback = ngx_log;
    ngx.feature_info.LoggingInfo.MinimumLoggingLevel = NGX_LOGGING_VERBOSE;
  }
  ngx.loaded = 1;
  ngx.init_possible = 1;
  return 1;
}

void vk_ngx_add_instance_extensions(const char **names, uint32_t *count, uint32_t most) {
  if (!ngx.loaded) return;
  NgxFeatureDiscoveryInfo info = discovery();
  uint32_t found = 0;
  VkExtensionProperties *properties = 0;
  NgxResult result = ngx.get_instance_extensions(&info, &found, &properties);
  if (result != NGX_SUCCESS) {
    // This core answers NotImplemented here, but still needs the three extensions
    // NGX has always asked for. They are all Vulkan 1.1 promotions, so a driver that
    // advertises them accepts them by name as well.
    fprintf(stderr, "ngx: the instance query returned 0x%08X; using the three known extensions\n", result);
    static const char *const known[] = {
      "VK_KHR_get_physical_device_properties2",
      "VK_KHR_external_memory_capabilities",
      "VK_KHR_external_semaphore_capabilities",
    };
    uint32_t available = 0;
    vkEnumerateInstanceExtensionProperties(0, &available, 0);
    VkExtensionProperties *all = calloc(available ? available : 1, sizeof *all);
    if (all) vkEnumerateInstanceExtensionProperties(0, &available, all);
    for (uint32_t i = 0; all && i < sizeof known / sizeof known[0]; i++) {
      int advertised = 0;
      for (uint32_t j = 0; j < available; j++) advertised |= strcmp(all[j].extensionName, known[i]) == 0;
      if (!advertised) {
        fprintf(stderr, "ngx: the loader doesn't offer %s, so DLSS stays off\n", known[i]);
        ngx.init_possible = 0;
        continue;
      }
      int present = 0;
      for (uint32_t j = 0; j < *count; j++) present |= strcmp(names[j], known[i]) == 0;
      if (!present && *count < most) names[(*count)++] = known[i];
      fprintf(stderr, "ngx: instance extension %s\n", known[i]);
    }
    free(all);
    return;
  }
  for (uint32_t i = 0; i < found && i < MOST_NGX_EXTENSIONS; i++) {
    const char *name = properties[i].extensionName;
    strncpy(ngx.instance_names[ngx.instance_name_count], name, VK_MAX_EXTENSION_NAME_SIZE - 1);
    ngx.instance_names[ngx.instance_name_count][VK_MAX_EXTENSION_NAME_SIZE - 1] = 0;
    name = ngx.instance_names[ngx.instance_name_count++];
    int present = 0;
    for (uint32_t j = 0; j < *count; j++) present |= strcmp(names[j], name) == 0;
    if (!present && *count < most) names[(*count)++] = name;
    fprintf(stderr, "ngx: instance extension %s\n", name);
  }
}

void vk_ngx_check_support(VkInstance instance, VkPhysicalDevice physical) {
  if (!ngx.loaded) return;
  NgxFeatureDiscoveryInfo info = discovery();
  NgxFeatureRequirement requirement;
  memset(&requirement, 0, sizeof requirement);
  NgxResult result = ngx.get_requirements(instance, physical, &info, &requirement);
  if (result != NGX_SUCCESS) {
    fprintf(stderr, "ngx: the support check failed with 0x%08X, so DLSS stays off\n", result);
    ngx.init_possible = 0;
    return;
  }
  if (requirement.FeatureSupported == 0) {
    ngx.supported = 1;
    fprintf(stderr, "ngx: DLSS Super Resolution is supported on this card\n");
  } else {
    fprintf(stderr, "ngx: DLSS Super Resolution is not supported here (reason 0x%X, needs hardware 0x%X, OS %s)\n",
            requirement.FeatureSupported, requirement.MinHWArchitecture, requirement.MinOSVersion);
    ngx.init_possible = 0;
    return;
  }

  uint32_t count = 0;
  VkExtensionProperties *properties = 0;
  result = ngx.get_device_extensions(instance, physical, &info, &count, &properties);
  if (result != NGX_SUCCESS) {
    fprintf(stderr, "ngx: asking for device extensions failed with 0x%08X, so DLSS stays off\n", result);
    ngx.init_possible = 0;
    return;
  }
  for (uint32_t i = 0; i < count && ngx.required_count < MOST_NGX_EXTENSIONS; i++) {
    const char *name = properties[i].extensionName;
    strncpy(ngx.required[ngx.required_count], name, VK_MAX_EXTENSION_NAME_SIZE - 1);
    ngx.required[ngx.required_count][VK_MAX_EXTENSION_NAME_SIZE - 1] = 0;
    if (!makes_extension_list(physical, name)) {
      fprintf(stderr, "ngx: this card lacks %s, so DLSS stays off\n", name);
      ngx.init_possible = 0;
    }
    ngx.required_count++;
    fprintf(stderr, "ngx: device extension %s\n", name);
  }
}

void vk_ngx_add_device_extensions(const char **names, uint32_t *count, uint32_t most) {
  if (!ngx.loaded || !ngx.init_possible) return;
  for (uint32_t i = 0; i < ngx.required_count; i++) {
    const char *name = ngx.required[i];
    int present = 0;
    for (uint32_t j = 0; j < *count; j++) present |= strcmp(names[j], name) == 0;
    if (present) continue;
    if (*count >= most) {
      fprintf(stderr, "ngx: no room for %s in the device's extensions, so DLSS stays off\n", name);
      ngx.init_possible = 0;
      return;
    }
    names[(*count)++] = name;
  }
}

int vk_ngx_wants_feature_chain(void) { return ngx.loaded && ngx.init_possible; }

// One probe image, its view and the memory behind it.
typedef struct {
  VkImage image;
  VkImageView view;
  VkDeviceMemory memory;
} NgxProbeImage;

static uint32_t ngx_memory_type(VkPhysicalDevice physical, uint32_t bits, VkMemoryPropertyFlags want) {
  VkPhysicalDeviceMemoryProperties properties;
  vkGetPhysicalDeviceMemoryProperties(physical, &properties);
  for (uint32_t i = 0; i < properties.memoryTypeCount; i++) {
    if ((bits & (1u << i)) && (properties.memoryTypes[i].propertyFlags & want) == want) return i;
  }
  return UINT32_MAX;
}

static int ngx_make_image(VkPhysicalDevice physical, VkDevice device, VkFormat format, VkImageUsageFlags usage,
                          VkImageAspectFlags aspect, uint32_t width, uint32_t height, NgxProbeImage *out) {
  memset(out, 0, sizeof *out);
  VkImageCreateInfo info = {
    .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
    .imageType = VK_IMAGE_TYPE_2D,
    .format = format,
    .extent = {width, height, 1},
    .mipLevels = 1,
    .arrayLayers = 1,
    .samples = VK_SAMPLE_COUNT_1_BIT,
    .tiling = VK_IMAGE_TILING_OPTIMAL,
    .usage = usage,
    .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
  };
  if (vkCreateImage(device, &info, 0, &out->image) != VK_SUCCESS) return 0;
  VkMemoryRequirements requirements;
  vkGetImageMemoryRequirements(device, out->image, &requirements);
  uint32_t type = ngx_memory_type(physical, requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  if (type == UINT32_MAX) return 0;
  VkMemoryAllocateInfo allocate = {
    .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
    .allocationSize = requirements.size,
    .memoryTypeIndex = type,
  };
  if (vkAllocateMemory(device, &allocate, 0, &out->memory) != VK_SUCCESS) return 0;
  if (vkBindImageMemory(device, out->image, out->memory, 0) != VK_SUCCESS) return 0;
  VkImageViewCreateInfo view = {
    .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
    .image = out->image,
    .viewType = VK_IMAGE_VIEW_TYPE_2D,
    .format = format,
    .subresourceRange = {aspect, 0, 1, 0, 1},
  };
  if (vkCreateImageView(device, &view, 0, &out->view) != VK_SUCCESS) return 0;
  return 1;
}

static void ngx_drop_image(VkDevice device, NgxProbeImage *image) {
  if (image->view) vkDestroyImageView(device, image->view, 0);
  if (image->image) vkDestroyImage(device, image->image, 0);
  if (image->memory) vkFreeMemory(device, image->memory, 0);
  memset(image, 0, sizeof *image);
}

static NgxResource ngx_view_resource(const NgxProbeImage *image, VkFormat format, VkImageAspectFlags aspect,
                                     uint32_t width, uint32_t height, int read_write) {
  NgxResource resource;
  memset(&resource, 0, sizeof resource);
  resource.Resource.ImageViewInfo.ImageView = image->view;
  resource.Resource.ImageViewInfo.Image = image->image;
  resource.Resource.ImageViewInfo.SubresourceRange = (VkImageSubresourceRange){aspect, 0, 1, 0, 1};
  resource.Resource.ImageViewInfo.Format = format;
  resource.Resource.ImageViewInfo.Width = width;
  resource.Resource.ImageViewInfo.Height = height;
  resource.Type = 0;
  resource.ReadWrite = (unsigned char)read_write;
  return resource;
}

static void ngx_barrier(VkCommandBuffer commands, VkImage image, VkImageAspectFlags aspect, VkImageLayout from,
                        VkImageLayout to, VkAccessFlags source, VkAccessFlags target, VkPipelineStageFlags from_stage,
                        VkPipelineStageFlags to_stage) {
  VkImageMemoryBarrier barrier = {
    .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
    .srcAccessMask = source,
    .dstAccessMask = target,
    .oldLayout = from,
    .newLayout = to,
    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
    .image = image,
    .subresourceRange = {aspect, 0, 1, 0, 1},
  };
  vkCmdPipelineBarrier(commands, from_stage, to_stage, 0, 0, 0, 0, 0, 1, &barrier);
}

// The core's own parameter map accepts the friendly resource names but does not hand
// them back through EvaluateFeature (it reports "could not find Color parameter"), so
// the probe supplies its own map for evaluation. Every getter the core calls lands
// here; names the probe did not set are forwarded to the core's map, which still
// holds the feature's creation state.
enum { NGX_SHIM_SLOTS = 24 };
enum { NGX_SHIM_ULL, NGX_SHIM_FLOAT, NGX_SHIM_DOUBLE, NGX_SHIM_UINT, NGX_SHIM_INT, NGX_SHIM_POINTER };

typedef struct {
  const char *name;
  int type;
  union {
    unsigned long long ull;
    float f;
    double d;
    unsigned int ui;
    int i;
    void *ptr;
  } value;
} NgxShimEntry;

typedef struct {
  void **vtable;
  NgxParameter *fallback;
  NgxShimEntry entries[NGX_SHIM_SLOTS];
  int count;
} NgxParameterShim;

typedef NgxResult (*NgxParameterGetULL)(const NgxParameter *, const char *, unsigned long long *);
typedef NgxResult (*NgxParameterGetFloat)(const NgxParameter *, const char *, float *);
typedef NgxResult (*NgxParameterGetDouble)(const NgxParameter *, const char *, double *);
typedef NgxResult (*NgxParameterGetUInt)(const NgxParameter *, const char *, unsigned int *);

static NgxShimEntry *shim_find(NgxParameterShim *self, const char *name) {
  for (int i = 0; i < self->count; i++) {
    if (strcmp(self->entries[i].name, name) == 0) return &self->entries[i];
  }
  return 0;
}

static NgxShimEntry *shim_store(NgxParameterShim *self, const char *name, int type) {
  NgxShimEntry *entry = shim_find(self, name);
  if (!entry) {
    if (self->count >= NGX_SHIM_SLOTS) return 0;
    entry = &self->entries[self->count++];
    entry->name = name;
  }
  entry->type = type;
  memset(&entry->value, 0, sizeof entry->value);
  return entry;
}

static void shim_set_ull(NgxParameterShim *self, const char *name, unsigned long long value) {
  NgxShimEntry *entry = shim_store(self, name, NGX_SHIM_ULL);
  if (entry) entry->value.ull = value;
}

static void shim_set_float(NgxParameterShim *self, const char *name, float value) {
  NgxShimEntry *entry = shim_store(self, name, NGX_SHIM_FLOAT);
  if (entry) entry->value.f = value;
}

static void shim_set_double(NgxParameterShim *self, const char *name, double value) {
  NgxShimEntry *entry = shim_store(self, name, NGX_SHIM_DOUBLE);
  if (entry) entry->value.d = value;
}

static void shim_set_uint(NgxParameterShim *self, const char *name, unsigned int value) {
  NgxShimEntry *entry = shim_store(self, name, NGX_SHIM_UINT);
  if (entry) entry->value.ui = value;
}

static void shim_set_int(NgxParameterShim *self, const char *name, int value) {
  NgxShimEntry *entry = shim_store(self, name, NGX_SHIM_INT);
  if (entry) entry->value.i = value;
}

static void shim_set_void(NgxParameterShim *self, const char *name, void *value) {
  NgxShimEntry *entry = shim_store(self, name, NGX_SHIM_POINTER);
  if (entry) entry->value.ptr = value;
}

static NgxResult shim_get_ull(NgxParameterShim *self, const char *name, unsigned long long *out) {
  NgxShimEntry *entry = shim_find(self, name);
  if (!entry) {
    if (!self->fallback) return 0xBAD00005;
    return ((NgxParameterGetULL)(*(void ***)self->fallback)[8])(self->fallback, name, out);
  }
  if (out) {
    switch (entry->type) {
      case NGX_SHIM_POINTER: *out = (unsigned long long)(uintptr_t)entry->value.ptr; break;
      case NGX_SHIM_UINT: *out = entry->value.ui; break;
      case NGX_SHIM_INT: *out = (unsigned long long)(long long)entry->value.i; break;
      case NGX_SHIM_FLOAT: *out = (unsigned long long)entry->value.f; break;
      default: *out = entry->value.ull; break;
    }
  }
  return NGX_SUCCESS;
}

static NgxResult shim_get_float(NgxParameterShim *self, const char *name, float *out) {
  NgxShimEntry *entry = shim_find(self, name);
  if (!entry) {
    if (!self->fallback) return 0xBAD00005;
    return ((NgxParameterGetFloat)(*(void ***)self->fallback)[9])(self->fallback, name, out);
  }
  if (out) {
    switch (entry->type) {
      case NGX_SHIM_POINTER: *out = (float)(uintptr_t)entry->value.ptr; break;
      case NGX_SHIM_UINT: *out = (float)entry->value.ui; break;
      case NGX_SHIM_INT: *out = (float)entry->value.i; break;
      case NGX_SHIM_ULL: *out = (float)entry->value.ull; break;
      default: *out = entry->value.f; break;
    }
  }
  return NGX_SUCCESS;
}

static NgxResult shim_get_double(NgxParameterShim *self, const char *name, double *out) {
  NgxShimEntry *entry = shim_find(self, name);
  if (!entry) {
    if (!self->fallback) return 0xBAD00005;
    return ((NgxParameterGetDouble)(*(void ***)self->fallback)[10])(self->fallback, name, out);
  }
  if (out) {
    switch (entry->type) {
      case NGX_SHIM_POINTER: *out = (double)(uintptr_t)entry->value.ptr; break;
      case NGX_SHIM_UINT: *out = (double)entry->value.ui; break;
      case NGX_SHIM_INT: *out = (double)entry->value.i; break;
      case NGX_SHIM_ULL: *out = (double)entry->value.ull; break;
      case NGX_SHIM_FLOAT: *out = (double)entry->value.f; break;
      default: *out = entry->value.d; break;
    }
  }
  return NGX_SUCCESS;
}

static NgxResult shim_get_uint(NgxParameterShim *self, const char *name, unsigned int *out) {
  NgxShimEntry *entry = shim_find(self, name);
  if (!entry) {
    if (!self->fallback) return 0xBAD00005;
    return ((NgxParameterGetUInt)(*(void ***)self->fallback)[11])(self->fallback, name, out);
  }
  if (out) {
    switch (entry->type) {
      case NGX_SHIM_POINTER: *out = (unsigned int)(uintptr_t)entry->value.ptr; break;
      case NGX_SHIM_INT: *out = (unsigned int)entry->value.i; break;
      case NGX_SHIM_ULL: *out = (unsigned int)entry->value.ull; break;
      case NGX_SHIM_FLOAT: *out = (unsigned int)entry->value.f; break;
      default: *out = entry->value.ui; break;
    }
  }
  return NGX_SUCCESS;
}

static NgxResult shim_get_int(NgxParameterShim *self, const char *name, int *out) {
  NgxShimEntry *entry = shim_find(self, name);
  if (!entry) {
    if (!self->fallback) return 0xBAD00005;
    return ((NgxParameterGetInt)(*(void ***)self->fallback)[12])(self->fallback, name, out);
  }
  if (out) {
    switch (entry->type) {
      case NGX_SHIM_POINTER: *out = (int)(intptr_t)entry->value.ptr; break;
      case NGX_SHIM_UINT: *out = (int)entry->value.ui; break;
      case NGX_SHIM_ULL: *out = (int)entry->value.ull; break;
      case NGX_SHIM_FLOAT: *out = (int)entry->value.f; break;
      default: *out = entry->value.i; break;
    }
  }
  return NGX_SUCCESS;
}

static NgxResult shim_get_ptr(NgxParameterShim *self, const char *name, void **out) {
  NgxShimEntry *entry = shim_find(self, name);
  if (!entry) {
    if (!self->fallback) return 0xBAD00005;
    return ((NgxParameterGetVoid)(*(void ***)self->fallback)[15])(self->fallback, name, out);
  }
  if (out) {
    switch (entry->type) {
      case NGX_SHIM_POINTER: *out = entry->value.ptr; break;
      case NGX_SHIM_ULL: *out = (void *)(uintptr_t)entry->value.ull; break;
      case NGX_SHIM_UINT: *out = (void *)(uintptr_t)entry->value.ui; break;
      case NGX_SHIM_INT: *out = (void *)(intptr_t)entry->value.i; break;
      default: *out = 0; break;
    }
  }
  return NGX_SUCCESS;
}

static void shim_reset(NgxParameterShim *self) { self->count = 0; }

static void *ngx_parameter_vtable[17] = {
  (void *)shim_set_ull,
  (void *)shim_set_float,
  (void *)shim_set_double,
  (void *)shim_set_uint,
  (void *)shim_set_int,
  (void *)shim_set_void,  // the two graphics-resource slots share the pointer form
  (void *)shim_set_void,
  (void *)shim_set_void,
  (void *)shim_get_ull,
  (void *)shim_get_float,
  (void *)shim_get_double,
  (void *)shim_get_uint,
  (void *)shim_get_int,
  (void *)shim_get_ptr,
  (void *)shim_get_ptr,
  (void *)shim_get_ptr,
  (void *)shim_reset,
};

// The second half of the probe: feed DLSS a synthetic cleared frame and see whether it
// writes the upscaled output. Color and motion vectors are RGBA/RG16, depth is D32.
// A hash of the output is not enough here: the output is cleared to magenta first, so
// any pixel that is no longer magenta proves the feature ran and produced a picture.
static void evaluate_dlss_feature(VkPhysicalDevice physical, VkDevice device, VkQueue queue, VkCommandPool pool,
                                  void *handle, NgxParameter *parameters) {
  const uint32_t in_width = 1280, in_height = 720, out_width = 1920, out_height = 1080;
  const VkImageUsageFlags input_usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  const VkImageUsageFlags output_usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT |
                                         VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  NgxProbeImage color, depth, motion, output;
  VkBuffer readback = VK_NULL_HANDLE;
  VkDeviceMemory readback_memory = VK_NULL_HANDLE;
  void *mapped = 0;
  NgxResult result = 0;

  memset(&color, 0, sizeof color);
  memset(&depth, 0, sizeof depth);
  memset(&motion, 0, sizeof motion);
  memset(&output, 0, sizeof output);

  if (!ngx_make_image(physical, device, VK_FORMAT_R8G8B8A8_UNORM, input_usage, VK_IMAGE_ASPECT_COLOR_BIT,
                      in_width, in_height, &color)) {
    fprintf(stderr, "ngx: the probe could not make the color image\n");
    goto done;
  }
  if (!ngx_make_image(physical, device, VK_FORMAT_D32_SFLOAT, input_usage, VK_IMAGE_ASPECT_DEPTH_BIT,
                      in_width, in_height, &depth)) {
    fprintf(stderr, "ngx: the probe could not make the depth image\n");
    goto done;
  }
  if (!ngx_make_image(physical, device, VK_FORMAT_R16G16_SFLOAT, input_usage, VK_IMAGE_ASPECT_COLOR_BIT,
                      in_width, in_height, &motion)) {
    fprintf(stderr, "ngx: the probe could not make the motion-vector image\n");
    goto done;
  }
  if (!ngx_make_image(physical, device, VK_FORMAT_R8G8B8A8_UNORM, output_usage, VK_IMAGE_ASPECT_COLOR_BIT,
                      out_width, out_height, &output)) {
    fprintf(stderr, "ngx: the probe could not make the output image\n");
    goto done;
  }

  VkDeviceSize bytes = (VkDeviceSize)out_width * out_height * 4;
  VkBufferCreateInfo buffer_info = {
    .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
    .size = bytes,
    .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
    .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
  };
  if (vkCreateBuffer(device, &buffer_info, 0, &readback) != VK_SUCCESS) {
    fprintf(stderr, "ngx: the probe could not make the readback buffer\n");
    goto done;
  }
  VkMemoryRequirements requirements;
  vkGetBufferMemoryRequirements(device, readback, &requirements);
  uint32_t type = ngx_memory_type(physical, requirements.memoryTypeBits,
                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  if (type == UINT32_MAX) {
    fprintf(stderr, "ngx: the probe found no host-visible memory for the readback\n");
    goto done;
  }
  VkMemoryAllocateInfo allocate = {
    .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
    .allocationSize = requirements.size,
    .memoryTypeIndex = type,
  };
  if (vkAllocateMemory(device, &allocate, 0, &readback_memory) != VK_SUCCESS ||
      vkBindBufferMemory(device, readback, readback_memory, 0) != VK_SUCCESS) {
    fprintf(stderr, "ngx: the probe could not attach the readback memory\n");
    goto done;
  }

  VkCommandBuffer commands = VK_NULL_HANDLE;
  VkCommandBufferAllocateInfo allocate_commands = {
    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
    .commandPool = pool,
    .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
    .commandBufferCount = 1,
  };
  VkCommandBufferBeginInfo begin = {
    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
  };
  if (vkAllocateCommandBuffers(device, &allocate_commands, &commands) != VK_SUCCESS ||
      vkBeginCommandBuffer(commands, &begin) != VK_SUCCESS) {
    fprintf(stderr, "ngx: the probe could not record the evaluation\n");
    goto done;
  }

  ngx_barrier(commands, color.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
              0, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
  ngx_barrier(commands, depth.image, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
              0, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
  ngx_barrier(commands, motion.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
              0, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
  ngx_barrier(commands, output.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
              0, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

  VkImageSubresourceRange color_range = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  VkImageSubresourceRange depth_range = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
  VkClearColorValue gray = {.float32 = {0.5f, 0.5f, 0.5f, 1.0f}};
  VkClearColorValue zero = {.float32 = {0.0f, 0.0f, 0.0f, 1.0f}};
  VkClearColorValue magenta = {.float32 = {1.0f, 0.0f, 1.0f, 1.0f}};
  VkClearDepthStencilValue far_plane = {1.0f, 0};
  vkCmdClearColorImage(commands, color.image, VK_IMAGE_LAYOUT_GENERAL, &gray, 1, &color_range);
  vkCmdClearDepthStencilImage(commands, depth.image, VK_IMAGE_LAYOUT_GENERAL, &far_plane, 1, &depth_range);
  vkCmdClearColorImage(commands, motion.image, VK_IMAGE_LAYOUT_GENERAL, &zero, 1, &color_range);
  vkCmdClearColorImage(commands, output.image, VK_IMAGE_LAYOUT_GENERAL, &magenta, 1, &color_range);

  ngx_barrier(commands, color.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
              VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
              VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
  ngx_barrier(commands, depth.image, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
              VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
              VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
  ngx_barrier(commands, motion.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
              VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
              VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
  ngx_barrier(commands, output.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
              VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
              VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);

  NgxResource color_resource = ngx_view_resource(&color, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT,
                                                 in_width, in_height, 0);
  NgxResource depth_resource = ngx_view_resource(&depth, VK_FORMAT_D32_SFLOAT, VK_IMAGE_ASPECT_DEPTH_BIT,
                                                 in_width, in_height, 0);
  NgxResource motion_resource = ngx_view_resource(&motion, VK_FORMAT_R16G16_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT,
                                                  in_width, in_height, 0);
  NgxResource output_resource = ngx_view_resource(&output, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT,
                                                  out_width, out_height, 1);
  // The core accepts these pointers from its own map when asked through the same
  // getter, yet EvaluateFeature still reports "could not find Color parameter", so
  // its lookup does not go through the public Get path. A private map answers every
  // getter the feature uses, and names the probe does not set fall through to the
  // core's map, which still holds the creation state.
  NgxParameterShim shim;
  memset(&shim, 0, sizeof shim);
  shim.vtable = ngx_parameter_vtable;
  shim.fallback = parameters;
  shim_set_void(&shim, NGX_PARAMETER_COLOR, &color_resource);
  shim_set_void(&shim, NGX_PARAMETER_DEPTH, &depth_resource);
  shim_set_void(&shim, NGX_PARAMETER_MOTION_VECTORS, &motion_resource);
  shim_set_void(&shim, NGX_PARAMETER_OUTPUT, &output_resource);
  shim_set_int(&shim, NGX_PARAMETER_RESET, 1);
  shim_set_float(&shim, NGX_PARAMETER_JITTER_X, 0.0f);
  shim_set_float(&shim, NGX_PARAMETER_JITTER_Y, 0.0f);
  shim_set_float(&shim, NGX_PARAMETER_MV_SCALE_X, 1.0f);
  shim_set_float(&shim, NGX_PARAMETER_MV_SCALE_Y, 1.0f);
  shim_set_float(&shim, NGX_PARAMETER_SHARPNESS, 0.0f);

  fprintf(stderr, "ngx: evaluating the DLSS feature (%ux%u into %ux%u)\n", in_width, in_height, out_width, out_height);
  result = ngx.evaluate_feature(commands, handle, (NgxParameter *)&shim, 0);

  ngx_barrier(commands, output.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_GENERAL,
              VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
              VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
  VkBufferImageCopy copy = {
    .bufferOffset = 0,
    .bufferRowLength = 0,
    .bufferImageHeight = 0,
    .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
    .imageOffset = {0, 0, 0},
    .imageExtent = {out_width, out_height, 1},
  };
  vkCmdCopyImageToBuffer(commands, output.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback, 1, &copy);
  VkBufferMemoryBarrier buffer_barrier = {
    .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
    .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
    .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
    .buffer = readback,
    .offset = 0,
    .size = VK_WHOLE_SIZE,
  };
  vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, 0, 1,
                       &buffer_barrier, 0, 0);
  vkEndCommandBuffer(commands);

  VkSubmitInfo submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &commands};
  if (vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE) != VK_SUCCESS || vkQueueWaitIdle(queue) != VK_SUCCESS) {
    fprintf(stderr, "ngx: the probe could not submit the evaluation\n");
    goto done;
  }
  if (result != NGX_SUCCESS) {
    fprintf(stderr, "ngx: evaluating the DLSS feature failed with 0x%08X\n", result);
    goto done;
  }
  if (vkMapMemory(device, readback_memory, 0, VK_WHOLE_SIZE, 0, &mapped) != VK_SUCCESS) {
    fprintf(stderr, "ngx: the probe could not read the output back\n");
    goto done;
  }

  const unsigned char *pixels = mapped;
  size_t total = (size_t)out_width * out_height, changed = 0;
  unsigned first[4] = {0, 0, 0, 0};
  for (size_t i = 0; i < total; i++) {
    const unsigned char *pixel = pixels + i * 4;
    if (pixel[0] == 255 && pixel[1] == 0 && pixel[2] == 255 && pixel[3] == 255) continue;
    if (!changed) {
      first[0] = pixel[0];
      first[1] = pixel[1];
      first[2] = pixel[2];
      first[3] = pixel[3];
    }
    changed++;
  }
  if (changed) {
    fprintf(stderr, "ngx: DLSS wrote the output (%zu of %zu pixels differ from the cleared frame; first R%u G%u B%u A%u)\n",
            changed, total, first[0], first[1], first[2], first[3]);
  } else {
    fprintf(stderr, "ngx: the evaluation returned success but the output stayed cleared\n");
  }

done:
  if (mapped) vkUnmapMemory(device, readback_memory);
  if (readback) vkDestroyBuffer(device, readback, 0);
  if (readback_memory) vkFreeMemory(device, readback_memory, 0);
  ngx_drop_image(device, &color);
  ngx_drop_image(device, &depth);
  ngx_drop_image(device, &motion);
  ngx_drop_image(device, &output);
}

// After NGX says the feature is available, ask it to build one. The command buffer
// must be recording, so the probe makes a transient pool, records the creation,
// submits it, waits, then evaluates one synthetic frame and releases the feature.
// The frame proves the feature writes an upscaled picture; it still does not prove
// the game can feed it, because the runtime has no motion vectors or jitter yet.
static void create_dlss_feature(VkPhysicalDevice physical, VkDevice device, VkQueue queue, uint32_t queue_family) {
  if (!ngx.desired) return;
  if (!ngx.allocate_parameters || !ngx.create_feature || !ngx.release_feature || !ngx.destroy_parameters) {
    fprintf(stderr, "ngx: the core has no feature entry points, so no feature is created\n");
    return;
  }
  NgxParameter *parameters = 0;
  if (ngx.allocate_parameters(&parameters) != NGX_SUCCESS || !parameters) {
    fprintf(stderr, "ngx: allocating the feature parameters failed\n");
    return;
  }
  void **vtable = *(void ***)parameters;
  NgxParameterSetFloat set_float = (NgxParameterSetFloat)vtable[1];
  NgxParameterSetUInt set_uint = (NgxParameterSetUInt)vtable[3];
  NgxParameterSetInt set_int = (NgxParameterSetInt)vtable[4];
  const unsigned int in_width = 1280, in_height = 720;
  const unsigned int out_width = 1920, out_height = 1080;
  set_uint(parameters, NGX_PARAMETER_WIDTH, in_width);
  set_uint(parameters, NGX_PARAMETER_HEIGHT, in_height);
  set_uint(parameters, NGX_PARAMETER_OUT_WIDTH, out_width);
  set_uint(parameters, NGX_PARAMETER_OUT_HEIGHT, out_height);
  set_uint(parameters, NGX_PARAMETER_CREATION_MASK, 1);
  set_uint(parameters, NGX_PARAMETER_VISIBILITY_MASK, 1);
  set_int(parameters, NGX_PARAMETER_PERF_QUALITY, NGX_PERF_QUALITY_MAX_QUALITY);
  set_int(parameters, NGX_PARAMETER_CREATE_FLAGS, 0);
  // The core logs MV.Scale as 0 when the values only reach the evaluation map, so it
  // appears to read them from the map the feature was created with. Set both places.
  set_float(parameters, NGX_PARAMETER_MV_SCALE_X, 1.0f);
  set_float(parameters, NGX_PARAMETER_MV_SCALE_Y, 1.0f);

  VkCommandPool pool = VK_NULL_HANDLE;
  VkCommandPoolCreateInfo pool_info = {
    .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
    .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
    .queueFamilyIndex = queue_family,
  };
  if (vkCreateCommandPool(device, &pool_info, 0, &pool) != VK_SUCCESS) {
    fprintf(stderr, "ngx: the feature needs a command pool, which the device refused\n");
    ngx.destroy_parameters(parameters);
    return;
  }
  VkCommandBuffer commands = VK_NULL_HANDLE;
  VkCommandBufferAllocateInfo allocate_info = {
    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
    .commandPool = pool,
    .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
    .commandBufferCount = 1,
  };
  VkCommandBufferBeginInfo begin_info = {
    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
  };
  void *handle = 0;
  NgxResult created = 0;
  int attempted = 0;
  if (vkAllocateCommandBuffers(device, &allocate_info, &commands) == VK_SUCCESS &&
      vkBeginCommandBuffer(commands, &begin_info) == VK_SUCCESS) {
    attempted = 1;
    created = ngx.create_feature(device, commands, NGX_FEATURE_SUPER_SAMPLING, parameters, &handle);
    vkEndCommandBuffer(commands);
  }
  if (attempted && created == NGX_SUCCESS && handle) {
    VkSubmitInfo submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &commands};
    if (vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE) == VK_SUCCESS) vkQueueWaitIdle(queue);
    fprintf(stderr, "ngx: the DLSS feature was created (%ux%u into %ux%u)\n", in_width, in_height, out_width, out_height);
    evaluate_dlss_feature(physical, device, queue, pool, handle, parameters);
    NgxResult released = ngx.release_feature(handle);
    if (released == NGX_SUCCESS) {
      fprintf(stderr, "ngx: the DLSS feature was released\n");
    } else {
      fprintf(stderr, "ngx: releasing the DLSS feature failed with 0x%08X\n", released);
    }
  } else if (attempted) {
    fprintf(stderr, "ngx: creating the DLSS feature failed with 0x%08X\n", created);
  } else {
    fprintf(stderr, "ngx: the device refused a command buffer for the DLSS feature\n");
  }
  vkDestroyCommandPool(device, pool, 0);
  ngx.destroy_parameters(parameters);
}

void vk_ngx_init_device(VkInstance instance, VkPhysicalDevice physical, VkDevice device, VkQueue queue, uint32_t queue_family) {
  if (!ngx.loaded || !ngx.init_possible) return;
  char app_data[MAX_PATH * 3];
  to_utf8(ngx.app_data, app_data, sizeof app_data);
  fprintf(stderr, "ngx: starting on the device (data in %s)\n", app_data);
  // The core's legacy entry takes the feature info and no Vulkan function pointers: it
  // resolves those through the loader that created the instance. Passing pointers, as
  // the newer Init_ProjectID_Ext does, made it read a function address as a version and
  // crash inside its own struct copy.
  NgxResult result = ngx.init_project_id(NGX_PROJECT_ID, NGX_ENGINE_CUSTOM, NGX_ENGINE_VERSION, ngx.app_data, instance,
                                         physical, device, NGX_VERSION_API, &ngx.feature_info);
  if (result != NGX_SUCCESS) {
    fprintf(stderr, "ngx: starting on this device failed with 0x%08X; logs are in %s\n", result, app_data);
    ngx.init_possible = 0;
    return;
  }
  fprintf(stderr, "ngx: NGX started on this device (project %s)\n", NGX_PROJECT_ID);
  // The core may open the feature library only when a feature is created, so say which
  // is true: a loaded library means discovery found it, not that DLSS is producing.
  fprintf(stderr, GetModuleHandleW(L"nvngx_dlss.dll")
                      ? "ngx: the DLSS library is loaded\n"
                      : "ngx: the DLSS library is not loaded yet; it opens when a feature is created\n");

  NgxParameter *parameters = 0;
  if (ngx.get_capability_parameters(&parameters) != NGX_SUCCESS || !parameters) {
    fprintf(stderr, "ngx: the capability parameters are missing, so support is unconfirmed\n");
    return;
  }
  // The class's vtable puts Get(const char *, int *) after the eight Set overloads
  // and four other Gets: index 12.
  void **vtable = *(void ***)parameters;
  NgxParameterGetInt get_int = (NgxParameterGetInt)vtable[12];
  int available = 0, needs_driver = 0;
  NgxResult asked = get_int(parameters, NGX_PARAMETER_AVAILABLE, &available);
  get_int(parameters, NGX_PARAMETER_NEEDS_DRIVER, &needs_driver);
  ngx.desired = asked == NGX_SUCCESS && available;
  if (!ngx.reported) {
    ngx.reported = 1;
    if (ngx.desired) {
      fprintf(stderr, "ngx: DLSS Super Resolution is available to this runtime\n");
    } else {
      fprintf(stderr, "ngx: DLSS is not available (query 0x%08X, needs driver: %d)\n", asked, needs_driver);
    }
  }
  create_dlss_feature(physical, device, queue, queue_family);
}

void vk_ngx_shutdown(VkDevice device) {
  if (ngx.core && ngx.shutdown) ngx.shutdown(device);
}

#endif
