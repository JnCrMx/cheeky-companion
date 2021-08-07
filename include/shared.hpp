#pragma once

#include <vulkan/vulkan.h>
#include <nlohmann/json.hpp>

using nlohmann::json;

inline json gameConfig;

inline VkPipelineLayout pipelineLayout;
inline VkRenderPass renderPass;
inline VkPipeline pipeline;

inline VkBuffer vertexBuffer;
inline VkBuffer indexBuffer;
inline VkDeviceMemory vertexMemory;
inline VkDeviceMemory indexMemory;
inline int indexCount;

VkRect2D rect2D_from_json(json j);
