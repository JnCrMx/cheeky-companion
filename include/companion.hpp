#pragma once

#include "logger.hpp"
#include <vulkan/vulkan.h>
#include <nlohmann/json.hpp>
#include <vector>
#include <string>
#include <variant>
#include <vulkan/vulkan_core.h>
#include <glm/glm.hpp>

using json = nlohmann::json;

struct RenderMesh
{
	uint32_t indexCount;
	VkBuffer indexBuffer;
	std::vector<VkBuffer> vertexBuffers;

	VkDeviceMemory memory;
};

struct RenderTexture
{
	VkImage image;
	VkImageView imageView;
	VkSampler sampler;

	VkDeviceMemory memory;
};

enum ModelType
{
	Obj
};

enum TextureType
{
	None,
	Png,
	Color
};

class companion
{
	public:
		companion(json& json, std::string filebase);

		void uploadMesh(VkDevice device, CheekyLayer::active_logger& logger);
		void uploadTexture(VkDevice device, CheekyLayer::active_logger& logger);

		bool hasTexture();
		VkDescriptorImageInfo getTextureDescriptorInfo();

		void draw(VkDevice device, VkCommandBuffer commandBuffer);

		std::string id() {return m_id;}
		RenderMesh& mesh() {return m_renderMesh;}
		RenderTexture& texture() {return m_renderTexture;}
	private:
		std::string m_id;
		ModelType m_modelType;
		std::string m_modelFile;

		TextureType m_textureType;
		std::variant<std::string, glm::vec4> m_textureArgument;

		RenderMesh m_renderMesh;
		RenderTexture m_renderTexture;
};
