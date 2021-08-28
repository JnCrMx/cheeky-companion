#pragma once

#include "draw.hpp"

#include <glm/glm.hpp>
#include <vulkan/vulkan.h>
#include <string>
#include <vector>

class render_client
{
	public:
		render_client(std::string companion) : m_companion(companion) {}

		void init(VkDevice device);
		void destroy(VkDevice device);

		void update();
		
		VkDescriptorSet descriptor_set() {return m_descriptorSet;}
		std::string companion() {return m_companion;}

		glm::vec3 m_position;
		float m_yaw;
		float m_pitch;
	private:
		struct ClientVariables
		{
			glm::mat4 matrix;
		};

		std::string m_companion;

		VkDescriptorSet m_descriptorSet;
		VkBuffer m_variablesBuffer;
		VkDeviceMemory m_variablesMemory;

		ClientVariables* m_variables;
};
