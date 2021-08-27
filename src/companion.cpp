#include "companion.hpp"
#include "dispatch.hpp"
#include "layer.hpp"
#include "logger.hpp"
#include "utils.hpp"
#include "image.hpp"

#include <cstdint>
#include <filesystem>
#include <glm/glm.hpp>
#include <fstream>
#include <glm/gtx/string_cast.hpp>
#include <iomanip>
#include <ios>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vulkan/vulkan_core.h>
#include <stb_image.h>

companion::companion(json& json, std::string filebase)
{
	m_id = json["id"];

	if(json["modelType"] == "obj") 		m_modelType = Obj;
	m_modelFile = filebase + "/" + (std::string)json["modelFile"];

	if(json["textureType"] == "none") 	m_textureType = None;
	if(json["textureType"] == "png") 	m_textureType = Png;
	if(json["textureType"] == "color") 	m_textureType = Color;

	if(m_textureType == Png)
		m_textureArgument = filebase + "/" + (std::string)json["textureFile"];
	if(m_textureType == Color)
		m_textureArgument = glm::vec4(json["textureColor"]["r"].get<float>(), 
			json["textureColor"]["g"].get<float>(), json["textureColor"]["b"].get<float>(), json["textureColor"]["a"].get<float>());
}

struct Vertex
{
	glm::vec3 position;
	glm::vec3 normal;
	glm::vec2 texCoord;
};

std::tuple<std::vector<Vertex>,std::vector<uint16_t>> load_obj(std::string file)
{
	std::vector<glm::vec3> positions;
	std::vector<glm::vec3> normals;
	std::vector<glm::vec2> texCoords;
	std::vector<std::tuple<int, int, int>> indices;

	std::ifstream obj(file);
	if(!obj)
		throw std::runtime_error("file not found: "+file);

	std::string line;
	while(std::getline(obj, line))
	{
		std::istringstream is(line);
		std::string type;
		is >> type;
		if(type=="v")
		{
			float x, y, z;
			is >> x >> y >> z;
			positions.push_back({x, y, z});
		}
		if(type=="vt")
		{
			float u, v;
			is >> u >> v;
			texCoords.push_back({u, -v});
		}
		if(type=="vn")
		{
			float x, y, z;
			is >> x >> y >> z;
			normals.push_back({x, y, z});
		}
		if(type=="f")
		{
			std::array<std::string, 3> args;
			is >> args[0] >> args[1] >> args[2];
			for(int i=0; i<3; i++)
			{
				std::stringstream s(args[i]);
				int vertex, uv, normal;

				s >> vertex;
				s.ignore(1);
				s >> uv;
				s.ignore(1);
				s >> normal;

				indices.push_back({vertex-1, uv-1, normal-1});
			}
		}
	}

	std::vector<Vertex> vertices;
	std::vector<std::tuple<int, int, int>> indexCombos;
	std::vector<uint16_t> newIndices;
	for(int i=0; i<indices.size(); i++)
	{
		auto index = indices[i];
		auto p = std::find(indexCombos.begin(), indexCombos.end(), index);
		if(p == indexCombos.end())
		{
			newIndices.push_back(vertices.size());
			auto [pos, tex, nor] = index;
			vertices.push_back({positions[pos], normals[nor], texCoords[tex]});
			indexCombos.push_back(index);
		}
		else
		{
			newIndices.push_back(std::distance(indexCombos.begin(), p));
		}
	}

	return {vertices, newIndices};
}

void companion::uploadMesh(VkDevice device, CheekyLayer::active_logger& logger)
{
	std::vector<Vertex> vertices;
	std::vector<uint16_t> indices;

	switch(m_modelType)
	{
		case Obj:
			std::tie(vertices, indices) = load_obj(m_modelFile);
			break;
	}
	logger << "[" << m_id << "] loaded mesh with " << vertices.size() << " vertices and " << indices.size() << " indices!\n";

	VkBuffer vertexBuffer;
	VkBuffer indexBuffer;
	VkDeviceMemory memory;

	VkBuffer stagingBuffer;
	VkDeviceMemory stagingMemory;

	VkBufferCreateInfo vertexBufferCreateInfo{};
	vertexBufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	vertexBufferCreateInfo.size = vertices.size() * sizeof(Vertex);
	vertexBufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
	vertexBufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	if(device_dispatch[GetKey(device)].CreateBuffer(device, &vertexBufferCreateInfo, nullptr, &vertexBuffer) != VK_SUCCESS)
		throw std::runtime_error("failed to create vertex buffer");
	
	VkBufferCreateInfo indexBufferCreateInfo{};
	indexBufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	indexBufferCreateInfo.size = indices.size() * sizeof(uint16_t);
	indexBufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
	indexBufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	if(device_dispatch[GetKey(device)].CreateBuffer(device, &indexBufferCreateInfo, nullptr, &indexBuffer) != VK_SUCCESS)
		throw std::runtime_error("failed to create index buffer");

	VkBufferCreateInfo stagingBufferCreateInfo{};
	stagingBufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	stagingBufferCreateInfo.size = vertices.size() * sizeof(Vertex) + indices.size() * sizeof(uint16_t);
	stagingBufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	stagingBufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	if(device_dispatch[GetKey(device)].CreateBuffer(device, &stagingBufferCreateInfo, nullptr, &stagingBuffer) != VK_SUCCESS)
		throw std::runtime_error("failed to create staging buffer");
	
	VkMemoryRequirements vertexBufferMemoryRequirements;
	VkMemoryRequirements indexBufferMemoryRequirements;
	device_dispatch[GetKey(device)].GetBufferMemoryRequirements(device, vertexBuffer, &vertexBufferMemoryRequirements);
	device_dispatch[GetKey(device)].GetBufferMemoryRequirements(device, indexBuffer, &indexBufferMemoryRequirements);

	VkMemoryRequirements stagingBufferMemoryRequirements;
	device_dispatch[GetKey(device)].GetBufferMemoryRequirements(device, stagingBuffer, &stagingBufferMemoryRequirements);

	VkMemoryAllocateInfo memoryallocateInfo{};
	memoryallocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	memoryallocateInfo.allocationSize = vertexBufferMemoryRequirements.size + indexBufferMemoryRequirements.size;
	memoryallocateInfo.memoryTypeIndex = findMemoryType(deviceInfos[device].memory, 
		vertexBufferMemoryRequirements.memoryTypeBits | indexBufferMemoryRequirements.memoryTypeBits,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	if(device_dispatch[GetKey(device)].AllocateMemory(device, &memoryallocateInfo, nullptr, &memory) != VK_SUCCESS)
		throw std::runtime_error("failed to allocate memory");
	
	VkMemoryAllocateInfo stagingMemoryallocateInfo{};
	stagingMemoryallocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	stagingMemoryallocateInfo.allocationSize = stagingBufferMemoryRequirements.size;
	stagingMemoryallocateInfo.memoryTypeIndex = findMemoryType(deviceInfos[device].memory, 
		stagingBufferMemoryRequirements.memoryTypeBits,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	if(device_dispatch[GetKey(device)].AllocateMemory(device, &stagingMemoryallocateInfo, nullptr, &stagingMemory) != VK_SUCCESS)
		throw std::runtime_error("failed to allocate staging memory");

	if(device_dispatch[GetKey(device)].BindBufferMemory(device, vertexBuffer, memory, 0) != VK_SUCCESS)
		throw std::runtime_error("failed to bind memory to vertex buffer");
	if(device_dispatch[GetKey(device)].BindBufferMemory(device, indexBuffer, memory, vertexBufferMemoryRequirements.size) != VK_SUCCESS)
		throw std::runtime_error("failed to bind memory to vertex buffer");
	if(device_dispatch[GetKey(device)].BindBufferMemory(device, stagingBuffer, stagingMemory, 0) != VK_SUCCESS)
		throw std::runtime_error("failed to bind memory to staging buffer");
	
	{
		void* stagingPointer;
		if(device_dispatch[GetKey(device)].MapMemory(device, stagingMemory, 0, VK_WHOLE_SIZE, 0, &stagingPointer) != VK_SUCCESS)
			throw std::runtime_error("failed to map staging memory");
		std::copy(vertices.begin(), vertices.end(), (Vertex*)((uint8_t*)stagingPointer + 0));
		std::copy(indices.begin(), indices.end(), (uint16_t*)((uint8_t*)stagingPointer + vertexBufferCreateInfo.size));
		device_dispatch[GetKey(device)].UnmapMemory(device, stagingMemory);
	}

	VkCommandBuffer commandBuffer = transferCommandBuffers[device];
	if(device_dispatch[GetKey(device)].ResetCommandBuffer(commandBuffer, 0) != VK_SUCCESS)
		throw std::runtime_error("failed to reset command buffer");

	VkCommandBufferBeginInfo beginInfo{};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	if(device_dispatch[GetKey(device)].BeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS)
		throw std::runtime_error("failed to begin the command buffer");
	
	VkBufferCopy copyVertexRegion{};
	copyVertexRegion.srcOffset = 0;
	copyVertexRegion.dstOffset = 0;
	copyVertexRegion.size = vertexBufferCreateInfo.size;
	device_dispatch[GetKey(device)].CmdCopyBuffer(commandBuffer, stagingBuffer, vertexBuffer, 1, &copyVertexRegion);

	VkBufferCopy copyIndexRegion{};
	copyIndexRegion.srcOffset = vertexBufferCreateInfo.size;
	copyIndexRegion.dstOffset = 0;
	copyIndexRegion.size = indexBufferCreateInfo.size;
	device_dispatch[GetKey(device)].CmdCopyBuffer(commandBuffer, stagingBuffer, indexBuffer, 1, &copyIndexRegion);

	if(device_dispatch[GetKey(device)].EndCommandBuffer(commandBuffer) != VK_SUCCESS)
		throw std::runtime_error("failed to end command buffer");
	
	VkQueue queue = transferQueues[device];

	if(device_dispatch[GetKey(device)].QueueWaitIdle(queue) != VK_SUCCESS)
		throw std::runtime_error("cannot wait for queue to be idle before copying");
	
	VkSubmitInfo submitInfo{};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &commandBuffer;
	if(device_dispatch[GetKey(device)].QueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS)
		throw std::runtime_error("failed to submit command buffer");

	if(device_dispatch[GetKey(device)].QueueWaitIdle(queue) != VK_SUCCESS)
		throw std::runtime_error("cannot wait for queue to be idle after copying");

	device_dispatch[GetKey(device)].DestroyBuffer(device, stagingBuffer, nullptr);
	device_dispatch[GetKey(device)].FreeMemory(device, stagingMemory, nullptr);

	m_renderMesh = {.indexCount = static_cast<uint32_t>(indices.size()), .indexBuffer = indexBuffer, .vertexBuffers={vertexBuffer}, .memory = memory};
}

void companion::uploadTexture(VkDevice device, CheekyLayer::active_logger &logger)
{
	if(m_textureType == None)
		return;
	
	int w, h, comp;
	uint8_t* buf;

	if(m_textureType == Png)
	{
		std::string path = std::get<std::string>(m_textureArgument);
		if(!std::filesystem::exists(path))
			throw std::runtime_error("file not foind: "+path);

		buf = stbi_load(path.c_str(), &w, &h, &comp, STBI_rgb_alpha);
		logger << "[" << m_id << "] Loaded image " << path << " with size of " << w << "x" << h << ".\n";
	}
	else if(m_textureType == Color)
	{
		glm::vec4 cv = std::get<glm::vec4>(m_textureArgument);
		image_tools::image::color color = image_tools::color(cv);
		w = 4;
		h = 4;
		buf = new uint8_t[w*h*4];
		std::fill(reinterpret_cast<uint32_t*>(buf), reinterpret_cast<uint32_t*>(buf)+(w*h), color);
		logger << "[" << m_id << "] Created image of color " << glm::to_string(cv) << " (converted to " 
			<< std::hex << std::showbase << std::internal << std::setw(8) << color << std::dec << ") with size of " << w << "x" << h << ".\n";
	}

	VkImage image;
	VkImageView imageView;
	VkSampler sampler;
	VkDeviceMemory memory;

	VkBuffer stagingBuffer;
	VkDeviceMemory stagingMemory;

	VkImageCreateInfo imageCreateInfo{};
	imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
	imageCreateInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
	imageCreateInfo.extent = {static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1};
	imageCreateInfo.mipLevels = 1;
	imageCreateInfo.arrayLayers = 1;
	imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageCreateInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	if(device_dispatch[GetKey(device)].CreateImage(device, &imageCreateInfo, nullptr, &image) != VK_SUCCESS)
		throw std::runtime_error("failed to create image");

	VkBufferCreateInfo stagingBufferCreateInfo{};
	stagingBufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	stagingBufferCreateInfo.size = w*h*4;
	stagingBufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	stagingBufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	if(device_dispatch[GetKey(device)].CreateBuffer(device, &stagingBufferCreateInfo, nullptr, &stagingBuffer) != VK_SUCCESS)
		throw std::runtime_error("failed to staging buffer");

	VkMemoryRequirements imageRequirements;
	VkMemoryRequirements stagingBufferMemoryRequirements;
	device_dispatch[GetKey(device)].GetImageMemoryRequirements(device, image, &imageRequirements);
	device_dispatch[GetKey(device)].GetBufferMemoryRequirements(device, stagingBuffer, &stagingBufferMemoryRequirements);

	VkMemoryAllocateInfo allocateInfo{};
	allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocateInfo.allocationSize = stagingBufferMemoryRequirements.size;
	allocateInfo.memoryTypeIndex = findMemoryType(deviceInfos[device].memory, 
		imageRequirements.memoryTypeBits,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	if(device_dispatch[GetKey(device)].AllocateMemory(device, &allocateInfo, nullptr, &memory) != VK_SUCCESS)
		throw std::runtime_error("failed to allocate image memory");
	
	VkMemoryAllocateInfo stagingMemoryallocateInfo{};
	stagingMemoryallocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	stagingMemoryallocateInfo.allocationSize = stagingBufferMemoryRequirements.size;
	stagingMemoryallocateInfo.memoryTypeIndex = findMemoryType(deviceInfos[device].memory, 
		stagingBufferMemoryRequirements.memoryTypeBits,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	if(device_dispatch[GetKey(device)].AllocateMemory(device, &stagingMemoryallocateInfo, nullptr, &stagingMemory) != VK_SUCCESS)
		throw std::runtime_error("failed to allocate staging memory");

	if(device_dispatch[GetKey(device)].BindImageMemory(device, image, memory, 0) != VK_SUCCESS)
		throw std::runtime_error("failed to bind memory to image");
	if(device_dispatch[GetKey(device)].BindBufferMemory(device, stagingBuffer, stagingMemory, 0) != VK_SUCCESS)
		throw std::runtime_error("failed to bind memory to staging buffer");

	VkImageViewCreateInfo imageViewCreateInfo{};
	imageViewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	imageViewCreateInfo.image = image;
	imageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	imageViewCreateInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
	imageViewCreateInfo.components = {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY};
	imageViewCreateInfo.subresourceRange = VkImageSubresourceRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
	if(device_dispatch[GetKey(device)].CreateImageView(device, &imageViewCreateInfo, nullptr, &imageView) != VK_SUCCESS)
		throw std::runtime_error("failed to create image view");

	VkSamplerCreateInfo samplerCreateInfo{};
	samplerCreateInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	samplerCreateInfo.magFilter = VK_FILTER_NEAREST;
	samplerCreateInfo.minFilter = VK_FILTER_NEAREST;
	samplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	samplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerCreateInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerCreateInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerCreateInfo.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
	if(device_dispatch[GetKey(device)].CreateSampler(device, &samplerCreateInfo, nullptr, &sampler) != VK_SUCCESS)
		throw std::runtime_error("failed to create sampler");

	{
		void* stagingPointer;
		if(device_dispatch[GetKey(device)].MapMemory(device, stagingMemory, 0, VK_WHOLE_SIZE, 0, &stagingPointer) != VK_SUCCESS)
			throw std::runtime_error("failed to map staging memory");
		std::copy(buf, buf+w*h*4, (uint8_t*)stagingPointer);
		device_dispatch[GetKey(device)].UnmapMemory(device, stagingMemory);
	}

	VkCommandBuffer commandBuffer = transferCommandBuffers[device];
	if(device_dispatch[GetKey(device)].ResetCommandBuffer(commandBuffer, 0) != VK_SUCCESS)
		throw std::runtime_error("failed to reset command buffer");

	VkCommandBufferBeginInfo beginInfo{};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	if(device_dispatch[GetKey(device)].BeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS)
		throw std::runtime_error("failed to begin the command buffer");

	VkImageMemoryBarrier imagePre{};
	imagePre.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	imagePre.image = image;
	imagePre.srcAccessMask = 0;
	imagePre.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	imagePre.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	imagePre.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	imagePre.subresourceRange = VkImageSubresourceRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
	device_dispatch[GetKey(device)].CmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, {}, 
		0, nullptr, 0, nullptr, 1, &imagePre);

	VkBufferImageCopy copy{};
	copy.imageExtent = imageCreateInfo.extent;
	copy.imageSubresource = VkImageSubresourceLayers{VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
	device_dispatch[GetKey(device)].CmdCopyBufferToImage(commandBuffer, stagingBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

	VkImageMemoryBarrier imagePost{};
	imagePost.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	imagePost.image = image;
	imagePost.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	imagePost.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	imagePost.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	imagePost.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	imagePost.subresourceRange = VkImageSubresourceRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
	device_dispatch[GetKey(device)].CmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, {}, 
		0, nullptr, 0, nullptr, 1, &imagePost);

	if(device_dispatch[GetKey(device)].EndCommandBuffer(commandBuffer) != VK_SUCCESS)
		throw std::runtime_error("failed to end command buffer");
	
	VkQueue queue = transferQueues[device];

	if(device_dispatch[GetKey(device)].QueueWaitIdle(queue) != VK_SUCCESS)
		throw std::runtime_error("cannot wait for queue to be idle before copying");
	
	VkSubmitInfo submitInfo{};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &commandBuffer;
	if(device_dispatch[GetKey(device)].QueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS)
		throw std::runtime_error("failed to submit command buffer");

	if(device_dispatch[GetKey(device)].QueueWaitIdle(queue) != VK_SUCCESS)
		throw std::runtime_error("cannot wait for queue to be idle after copying");

	device_dispatch[GetKey(device)].DestroyBuffer(device, stagingBuffer, nullptr);
	device_dispatch[GetKey(device)].FreeMemory(device, stagingMemory, nullptr);

	if(m_textureType == Png)
		stbi_image_free(buf);
	if(m_textureType == Color)
		delete [] buf;

	m_renderTexture = {image, imageView, sampler, memory};
}

bool companion::hasTexture()
{
	return m_textureType != None;
}

VkDescriptorImageInfo companion::getTextureDescriptorInfo()
{
	return {
		.sampler = m_renderTexture.sampler,
		.imageView = m_renderTexture.imageView,
		.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
	};
}

void companion::draw(VkDevice device, VkCommandBuffer commandBuffer)
{
	device_dispatch[GetKey(device)].CmdBindIndexBuffer(commandBuffer, m_renderMesh.indexBuffer, 0, VK_INDEX_TYPE_UINT16);

	auto vertexBufferCount = m_renderMesh.vertexBuffers.size();
	std::vector<VkDeviceSize> offsets(vertexBufferCount);
	device_dispatch[GetKey(device)].CmdBindVertexBuffers(commandBuffer, 0, vertexBufferCount, m_renderMesh.vertexBuffers.data(), offsets.data());
	
	device_dispatch[GetKey(device)].CmdDrawIndexed(commandBuffer, m_renderMesh.indexCount, 1, 0, 0, 0);
}
