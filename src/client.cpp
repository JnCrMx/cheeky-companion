#include "client.hpp"
#include "draw.hpp"
#include "shared.hpp"

#include "dispatch.hpp"
#include "rules/execution_env.hpp"
#include "utils.hpp"

#include <glm/ext/matrix_transform.hpp>
#include <glm/fwd.hpp>
#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan.hpp>

void render_client::init(VkDevice device)
{
	VkResult r;

	// client variables buffer
	VkBufferCreateInfo bufferCreateInfo{};
	bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	bufferCreateInfo.size = sizeof(ClientVariables);
	bufferCreateInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
	
	if((r = device_dispatch[GetKey(device)].CreateBuffer(device, &bufferCreateInfo, nullptr, &m_variablesBuffer)) != VK_SUCCESS)
		throw std::runtime_error("failed to create client variables buffer: "+vk::to_string((vk::Result)r));

	VkMemoryRequirements requirements;
	device_dispatch[GetKey(device)].GetBufferMemoryRequirements(device, m_variablesBuffer, &requirements);

	VkMemoryAllocateInfo memoryallocateInfo{};
	memoryallocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	memoryallocateInfo.memoryTypeIndex = findMemoryType(deviceInfos[device].memory, requirements.memoryTypeBits,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	memoryallocateInfo.allocationSize = requirements.size;

	if(device_dispatch[GetKey(device)].AllocateMemory(device, &memoryallocateInfo, nullptr, &m_variablesMemory) != VK_SUCCESS)
		throw std::runtime_error("failed to allocate memory for client variables buffer");

	if(device_dispatch[GetKey(device)].BindBufferMemory(device, m_variablesBuffer, m_variablesMemory, 0) != VK_SUCCESS)
		throw std::runtime_error("failed to bind memory to client variables buffer");

	if(device_dispatch[GetKey(device)].MapMemory(device, m_variablesMemory, 0, sizeof(ClientVariables), 0, (void**)&m_variables) != VK_SUCCESS)
		throw std::runtime_error("failed to map memory for client variables buffer");

	// descriptor set
	VkDescriptorSetAllocateInfo allocateInfo{};
	allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocateInfo.descriptorPool = descriptorPool;
	allocateInfo.descriptorSetCount = 1;
	allocateInfo.pSetLayouts = &descriptorSetLayout;

	r = device_dispatch[GetKey(device)].AllocateDescriptorSets(device, &allocateInfo, &m_descriptorSet);
	if(r != VK_SUCCESS)
		throw std::runtime_error("failed to allocate descriptor set: "+vk::to_string((vk::Result)r));

	// update descriptor set
	auto& companion = companions[m_companion];
	auto descriptorCount = gameConfig["descriptors"].size();

	std::vector<VkDescriptorBufferInfo> bufferInfos;	bufferInfos.reserve(descriptorCount);
	std::vector<VkDescriptorImageInfo> imageInfos;		imageInfos.reserve(descriptorCount);
	std::vector<VkWriteDescriptorSet> writes;
	for(auto& d : gameConfig["descriptors"])
	{
		auto& source = d["_source"];
		int dstBinding = d["binding"];
		if(source["type"]=="vars")
		{
			VkDescriptorType t = descriptorBindings.at(dstBinding).descriptorType;
			
			VkWriteDescriptorSet& write = writes.emplace_back();
			write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			write.dstSet = m_descriptorSet;
			write.dstBinding = dstBinding;
			write.dstArrayElement = 0;
			write.descriptorCount = 1;
			write.descriptorType = t;

			VkDescriptorBufferInfo& bufferInfo = bufferInfos.emplace_back();
			bufferInfo.buffer = generalVariablesBuffer;
			bufferInfo.offset = 0;
			bufferInfo.range = sizeof(GeneralVariables);
			write.pBufferInfo = &bufferInfo;
		}
		else if(source["type"]=="texture")
		{
			if(companion->hasTexture())
			{
				VkWriteDescriptorSet& write = writes.emplace_back();
				write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				write.dstSet = m_descriptorSet;
				write.dstBinding = dstBinding;
				write.dstArrayElement = 0;
				write.descriptorCount = 1;
				write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;

				imageInfos.push_back(companion->getTextureDescriptorInfo());
				write.pImageInfo = &imageInfos.back();
			}
		}
		else if(source["type"]=="client")
		{
			VkDescriptorType t = descriptorBindings.at(dstBinding).descriptorType;

			VkWriteDescriptorSet& write = writes.emplace_back();
			write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			write.dstSet = m_descriptorSet;
			write.dstBinding = dstBinding;
			write.dstArrayElement = 0;
			write.descriptorCount = 1;
			write.descriptorType = t;
				
			VkDescriptorBufferInfo& bufferInfo = bufferInfos.emplace_back();
			bufferInfo.buffer = m_variablesBuffer;
			bufferInfo.offset = 0;
			bufferInfo.range = sizeof(ClientVariables);
			write.pBufferInfo = &bufferInfo;
		}
	}
	device_dispatch[GetKey(device)].UpdateDescriptorSets(device, writes.size(), writes.data(), 0, nullptr);
}

void render_client::destroy(VkDevice device)
{
	device_dispatch[GetKey(device)].FreeDescriptorSets(device, descriptorPool, 1, &m_descriptorSet);

	device_dispatch[GetKey(device)].DestroyBuffer(device, m_variablesBuffer, nullptr);

	device_dispatch[GetKey(device)].UnmapMemory(device, m_variablesMemory);
	device_dispatch[GetKey(device)].FreeMemory(device, m_variablesMemory, nullptr);
}

void render_client::update()
{
	glm::mat4 rotate = glm::rotate(glm::mat4(1.0), m_yaw, glm::vec3(0.0, 1.0, 0.0));
	glm::mat4 translate = glm::translate(glm::mat4(1.0), m_position);
	m_variables->matrix = translate * rotate;
	//m_variables->matrix = glm::mat4(1.0);
}
