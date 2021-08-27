#pragma once

#include "companion.hpp"
#include "client.hpp"
#include "net/server.hpp"

#include <vulkan/vulkan.h>
#include <nlohmann/json.hpp>

#include <map>
#include <memory>

using nlohmann::json;

inline json mainConfig;
inline json gameConfig;
inline bool ready;

inline network::server* server;

inline VkDevice globalDevice;

inline VkPipelineLayout pipelineLayout;
inline VkRenderPass renderPass;
inline VkPipeline pipeline;

inline VkBuffer vertexBuffer;
inline VkBuffer indexBuffer;
inline VkDeviceMemory vertexMemory;
inline VkDeviceMemory indexMemory;
inline int indexCount;

inline VkDescriptorSetLayout descriptorSetLayout;
inline VkDescriptorPool descriptorPool;
inline std::vector<VkDescriptorSetLayoutBinding> descriptorBindings;

struct GeneralVariables
{
	uint32_t seconds;
	float partial_seconds;
};
inline VkBuffer generalVariablesBuffer;
inline VkDeviceMemory generalVariablesMemory;
inline GeneralVariables* generalVariables;

VkRect2D rect2D_from_json(json& j);
void updateGeneralVariables();

inline std::map<std::string, std::unique_ptr<companion>> companions;
inline std::vector<render_client*> clients;

