#include "shared.hpp"
#include "companion.hpp"
#include "net/server.hpp"
#include "net/handler.hpp"

#include "logger.hpp"
#include "dispatch.hpp"
#include "shaders.hpp"
#include "rules/execution_env.hpp"
#include "rules/rules.hpp"
#include "reflection/reflectionparser.hpp"
#include "reflection/vkreflection.hpp"
#include "utils.hpp"

#include <cmath>
#include <memory>
#include <ostream>
#include <stdexcept>
#include <string>
#include <chrono>
#include <vulkan/vulkan_core.h>

#include <vulkan/vulkan.hpp>

#include <nlohmann/json.hpp>
#include <vulkan/vulkan_enums.hpp>

using nlohmann::json;
using namespace CheekyLayer::rules;

void parse_json_struct(json& json, void* p, std::string type)
{
	for(auto [key, value] : json.items())
	{
		if(key.starts_with("_"))
			continue;

		std::string expression = key + "=";
		if(value.is_number_float())
			expression += std::to_string((float)value);
		else if(value.is_number())
			expression += std::to_string((uint32_t)value);
		else if(value.is_string())
			expression += (std::string)value;
		CheekyLayer::reflection::parse_assign(expression, p, type);
	}
}

VkRect2D rect2D_from_json(json& j)
{
	VkRect2D r;
	r.offset = {j["offset"]["x"], j["offset"]["y"]};
	r.extent = {j["extent"]["width"], j["extent"]["height"]};
	return r;
}

void createDescriptors(json& json, VkDevice device, CheekyLayer::active_logger& log)
{
	VkDescriptorSetLayoutCreateInfo layoutInfo{};
	layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	std::vector<VkDescriptorSetLayoutBinding> bindings;
	for(auto& a : json) parse_json_struct(a, &bindings.emplace_back(), "VkDescriptorSetLayoutBinding");
	layoutInfo.bindingCount = bindings.size();
	layoutInfo.pBindings = bindings.data();

	descriptorBindings = bindings;
	log << "Found " << bindings.size() << " descriptor bindings.\n";

	VkResult r = device_dispatch[GetKey(device)].CreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayout);
	if(r != VK_SUCCESS)
		throw std::runtime_error("failed to create descriptor set layout: "+vk::to_string((vk::Result)r));
	
	std::map<VkDescriptorType, uint32_t> sizes;
	for(auto& a : bindings) sizes[a.descriptorType]++;

	int maxClients = mainConfig["maxClients"];
	for(auto& [a, b] : sizes) b *= maxClients;

	VkDescriptorPoolCreateInfo poolInfo{};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.maxSets = 1;
	std::vector<VkDescriptorPoolSize> poolSizes;
	for(auto& [type, size] : sizes) poolSizes.push_back(VkDescriptorPoolSize{.type = type, .descriptorCount = size});
	poolInfo.poolSizeCount = poolSizes.size();
	poolInfo.pPoolSizes = poolSizes.data();

	r = device_dispatch[GetKey(device)].CreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool);
	if(r != VK_SUCCESS)
		throw std::runtime_error("failed to create descriptor pool: "+vk::to_string((vk::Result)r));
}

void createPipelineLayout(json& json, VkDevice device)
{
	VkPipelineLayoutCreateInfo plCreateInfo{};
	plCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	plCreateInfo.setLayoutCount = 1;
	plCreateInfo.pSetLayouts = &descriptorSetLayout;
	plCreateInfo.pushConstantRangeCount = 0;
	
	VkResult r = device_dispatch[GetKey(device)].CreatePipelineLayout(device, &plCreateInfo, nullptr, &pipelineLayout);
	if(r != VK_SUCCESS)
		throw std::runtime_error("failed to create pipeline layout: "+vk::to_string((vk::Result)r));
}

void createRenderPass(json& json, VkDevice device)
{
	VkRenderPassCreateInfo rpCreateInfo{};
	rpCreateInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	rpCreateInfo.pNext = nullptr;
	rpCreateInfo.flags = json["flags"];

	std::vector<VkAttachmentDescription> attachments;
	for(auto& a : json["attachments"])
	{
		VkAttachmentDescription description{};
		parse_json_struct(a, &description, "VkAttachmentDescription");
		attachments.push_back(description);
	}
	rpCreateInfo.attachmentCount = attachments.size();
	rpCreateInfo.pAttachments = attachments.data();

	std::vector<VkSubpassDescription> subpasses;
	std::vector<std::vector<VkAttachmentReference>> subpassColorAttachments;
	std::vector<VkAttachmentReference> subpassDepthAttachments;
	for(auto& a : json["subpasses"])
	{
		VkSubpassDescription& subpass = subpasses.emplace_back();
		subpass.flags = a["flags"];
		subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;

		std::vector<VkAttachmentReference>& colorAttachments = subpassColorAttachments.emplace_back();
		for(auto& b : a["colorAttachments"])
		{
			VkAttachmentReference reference{};
			parse_json_struct(b, &reference, "VkAttachmentReference");
			colorAttachments.push_back(reference);
		}
		subpass.colorAttachmentCount = colorAttachments.size();
		subpass.pColorAttachments = colorAttachments.data();

		VkAttachmentReference& depthReference = subpassDepthAttachments.emplace_back();
		parse_json_struct(a["depthAttachment"], &depthReference, "VkAttachmentReference");
		subpass.pDepthStencilAttachment = &depthReference;
	}
	rpCreateInfo.subpassCount = subpasses.size();
	rpCreateInfo.pSubpasses = subpasses.data();

	std::vector<VkSubpassDependency> dependencies(2);
	dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
	dependencies[0].dstSubpass = 0;
	dependencies[0].srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependencies[0].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependencies[0].srcAccessMask = VK_ACCESS_NONE_KHR;
	dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	
	dependencies[1].srcSubpass = 0;
	dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
	dependencies[1].srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependencies[1].dstStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
	dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
	
	rpCreateInfo.dependencyCount = dependencies.size();
	rpCreateInfo.pDependencies = dependencies.data();

	VkResult r = device_dispatch[GetKey(device)].CreateRenderPass(device, &rpCreateInfo, nullptr, &renderPass);
	if(r != VK_SUCCESS)
		throw std::runtime_error("failed to create render pass: "+vk::to_string((vk::Result)r));
}

void createPipeline(std::string filebase, json& json, VkDevice device)
{
	std::vector<VkShaderModule> shaderModules;
	std::vector<VkPipelineShaderStageCreateInfo> shaderInfos;
	const char* main = "main";

	for(auto& a : json["shaderStages"])
	{
		VkShaderStageFlagBits stageBit = (VkShaderStageFlagBits)std::any_cast<uint32_t>(CheekyLayer::reflection::parse_rvalue(a["stage"], nullptr, "VkShaderStageFlagBits"));

		if(a["type"] == "glsl")
		{
			EShLanguage stage;
			switch(stageBit)
			{
				case VK_SHADER_STAGE_VERTEX_BIT:
					stage = EShLangVertex;
					break;
				case VK_SHADER_STAGE_FRAGMENT_BIT:
					stage = EShLangFragment;
					break;
				default:
					throw std::runtime_error("cannot compile shaders for this stage");
			}

			std::string code;
			std::ifstream in(filebase+"/"+(std::string)a["file"]);
			if(!in)
				throw std::runtime_error("cannot open file: "+filebase+"/"+(std::string)a["file"]);

			in.seekg(0, std::ios::end);
			code.reserve(in.tellg());
			in.seekg(0, std::ios::beg);
			code.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());

			std::vector<uint32_t> data;
			auto [ok, message] = compileShader(stage, code, data);
			if(!ok)
				throw std::runtime_error("failed to compile shader: "+message);
			
			VkShaderModuleCreateInfo sinfo{};
			sinfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
			sinfo.pCode = data.data();
			sinfo.codeSize = data.size() * sizeof(uint32_t);

			VkShaderModule m;
			VkResult r = device_dispatch[GetKey(device)].CreateShaderModule(device, &sinfo, nullptr, &m);
			if(r != VK_SUCCESS)
				throw std::runtime_error("failed to create shader: "+vk::to_string((vk::Result)r));
			shaderModules.push_back(m);
			
			VkPipelineShaderStageCreateInfo stageInfo{};
			stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			stageInfo.module = m;
			stageInfo.pName = main;
			stageInfo.stage = stageBit;
			shaderInfos.push_back(stageInfo);
		}
		else
			throw std::runtime_error("cannot handle shader of type "+(std::string)a["type"]);
	}

	VkGraphicsPipelineCreateInfo info{};
	info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	info.flags = json["flags"];
	info.layout = pipelineLayout;
	info.renderPass = renderPass;
	info.subpass = json["subpass"];

	info.stageCount = shaderInfos.size();
	info.pStages = shaderInfos.data();

	VkPipelineVertexInputStateCreateInfo vertexInputState{};
	vertexInputState.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertexInputState.flags = json["vertexInputState"]["flags"];
	std::vector<VkVertexInputBindingDescription> inputBindings;
	for(auto& a : json["vertexInputState"]["vertexBindingDescriptions"]) parse_json_struct(a, &inputBindings.emplace_back(), "VkVertexInputBindingDescription");
	vertexInputState.vertexBindingDescriptionCount = inputBindings.size();
	vertexInputState.pVertexBindingDescriptions = inputBindings.data();
	std::vector<VkVertexInputAttributeDescription> inputAttributes;
	for(auto& a : json["vertexInputState"]["vertexAttributeDescriptions"]) parse_json_struct(a, &inputAttributes.emplace_back(), "VkVertexInputAttributeDescription");
	vertexInputState.vertexAttributeDescriptionCount = inputAttributes.size();
	vertexInputState.pVertexAttributeDescriptions = inputAttributes.data();
	info.pVertexInputState = &vertexInputState;

	VkPipelineInputAssemblyStateCreateInfo inputAssemblyState{};
	inputAssemblyState.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	parse_json_struct(json["inputAssemblyState"], &inputAssemblyState, "VkPipelineInputAssemblyStateCreateInfo");
	info.pInputAssemblyState = &inputAssemblyState;

	info.pTessellationState = nullptr;

	VkPipelineViewportStateCreateInfo viewportState{};
	viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportState.flags = json["viewportState"]["flags"];
	std::vector<VkViewport> viewports;
	for(auto& a : json["viewportState"]["viewports"]) parse_json_struct(a, &viewports.emplace_back(), "VkViewport");
	viewportState.viewportCount = viewports.size();
	viewportState.pViewports = viewports.data();
	std::vector<VkRect2D> scissors;
	for(auto& a : json["viewportState"]["scissors"]) scissors.push_back(rect2D_from_json(a));
	viewportState.scissorCount = scissors.size();
	viewportState.pScissors = scissors.data();
	info.pViewportState = &viewportState;

	VkPipelineRasterizationStateCreateInfo rasterizationState{};
	rasterizationState.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	parse_json_struct(json["rasterizationState"], &rasterizationState, "VkPipelineRasterizationStateCreateInfo");
	info.pRasterizationState = &rasterizationState;

	VkPipelineMultisampleStateCreateInfo multisampleState{};
	multisampleState.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	parse_json_struct(json["multisampleState"], &multisampleState, "VkPipelineMultisampleStateCreateInfo");
	info.pMultisampleState = &multisampleState;

	VkPipelineDepthStencilStateCreateInfo depthStencilState{};
	depthStencilState.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	parse_json_struct(json["depthStencilState"], &depthStencilState, "VkPipelineDepthStencilStateCreateInfo");
	info.pDepthStencilState = &depthStencilState;

	VkPipelineColorBlendStateCreateInfo colorBlendState{};
	colorBlendState.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	parse_json_struct(json["colorBlendState"], &colorBlendState, "VkPipelineColorBlendStateCreateInfo");
	int attachmentCount = gameConfig["renderPass"]["subpasses"][info.subpass]["colorAttachments"].size();
	std::vector<VkPipelineColorBlendAttachmentState> blendAttachments(attachmentCount);
	for(int i=0; i<attachmentCount; i++)
	{
		blendAttachments[i].blendEnable = VK_FALSE;
		blendAttachments[i].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	}
	colorBlendState.attachmentCount = attachmentCount;
	colorBlendState.pAttachments = blendAttachments.data();
	info.pColorBlendState = &colorBlendState;

	info.pDynamicState = nullptr;

	VkResult r = device_dispatch[GetKey(device)].CreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline);
	if(r != VK_SUCCESS)
		throw std::runtime_error("failed to create pipeline: "+vk::to_string((vk::Result)r));
}

void createGeneralVariables(VkDevice device, CheekyLayer::active_logger& log)
{
	VkResult result;

	VkBufferCreateInfo bufferCreateInfo{};
	bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	bufferCreateInfo.size = sizeof(GeneralVariables);
	bufferCreateInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
	
	if((result = device_dispatch[GetKey(device)].CreateBuffer(device, &bufferCreateInfo, nullptr, &generalVariablesBuffer)) != VK_SUCCESS)
		throw std::runtime_error("failed to create general variables buffer: "+vk::to_string((vk::Result)result));

	VkMemoryRequirements requirements;
	device_dispatch[GetKey(device)].GetBufferMemoryRequirements(device, generalVariablesBuffer, &requirements);

	VkMemoryAllocateInfo memoryallocateInfo{};
	memoryallocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	memoryallocateInfo.memoryTypeIndex = findMemoryType(deviceInfos[device].memory, requirements.memoryTypeBits,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	memoryallocateInfo.allocationSize = requirements.size;

	if(device_dispatch[GetKey(device)].AllocateMemory(device, &memoryallocateInfo, nullptr, &generalVariablesMemory) != VK_SUCCESS)
		throw std::runtime_error("failed to allocate memory for general variables buffer");

	if(device_dispatch[GetKey(device)].BindBufferMemory(device, generalVariablesBuffer, generalVariablesMemory, 0) != VK_SUCCESS)
		throw std::runtime_error("failed to bind memory to general variables buffer");

	if(device_dispatch[GetKey(device)].MapMemory(device, generalVariablesMemory, 0, sizeof(GeneralVariables), 0, (void**)&generalVariables) != VK_SUCCESS)
		throw std::runtime_error("failed to map memory for general variables buffer");
}

void updateGeneralVariables()
{
	if(generalVariables == nullptr)
		throw std::runtime_error("general variables point to null");

	using Clock = std::chrono::high_resolution_clock;

	auto time = Clock::now();
	auto duration = time.time_since_epoch();
	std::chrono::duration<uint32_t> seconds = std::chrono::floor<std::chrono::seconds>(duration);
	std::chrono::duration<float> partialSeconds = duration - seconds;

	generalVariables->seconds = seconds.count();
	generalVariables->partial_seconds = partialSeconds.count();
}

void loadCompanion(std::string directory, std::string name)
{
	json json;
	{
		std::ifstream in(directory+"/companions/"+name+"/companion.json");
		in >> json;
	}
	std::unique_ptr<companion> c = std::make_unique<companion>(json, directory+"/companions/"+name);
	companions[c->id()] = std::move(c);
}

using network::clientID;
using network::server;
using network::network_handler;

std::unique_ptr<network_handler> handlerFactory(clientID client, std::string name, class server* server)
{
	return std::make_unique<network_handler>(client, name, server);
}

class init_action : public action
{
	public:
		init_action(selector_type type) : action(type) {
			if(type != selector_type::DeviceCreate)
				throw std::runtime_error("the \"companion:init\" action is only supported for device_create selectors, but not for "+to_string(type)+" selectors");
		}

		void read(std::istream& in) override
		{
			std::getline(in, m_directory, ',');
			skip_ws(in);
			std::getline(in, m_game, ')');
		}

		void execute(selector_type, VkHandle handle, local_context& ctx, rule&) override
		{
			VkDevice device = (VkDevice) handle;
			globalDevice = device;

			{
				std::ifstream in(m_directory+"/config.json");
				in >> mainConfig;
			}
			std::string serverType = mainConfig["network"]["type"];
			if(serverType == "basic_server")
			{
				int port = mainConfig["network"]["port"];
				ctx.logger << "Starting basic_server on port " << port << "\n";
				ctx.logger.flush();

				if(server)
					delete server;
				server = new network::basic_server(port, &handlerFactory);
			}

			{
				std::ifstream in(m_directory+"/games/"+m_game+"/game.json");
				in >> gameConfig;
			}

			createDescriptors(gameConfig["descriptors"], device, ctx.logger);
			createPipelineLayout(gameConfig["pipelineLayout"], device);
			createRenderPass(gameConfig["renderPass"], device);
			createPipeline(m_directory+"/games/"+m_game, gameConfig["pipeline"], device);
			createGeneralVariables(device, ctx.logger);

			loadCompanion(m_directory, "TheCube");
			loadCompanion(m_directory, "Monke");
			for(auto& [name, companion] : companions)
			{
				companion->uploadMesh(device, ctx.logger);
				companion->uploadTexture(device, ctx.logger);
			}

			ctx.logger << "Companion initialized for " << m_game << " in directory " << m_directory << "\n";
			ready = true;
		}

		std::ostream& print(std::ostream& out) override
		{
			out << "companion:init(" << m_directory << ", " << m_game << ")";
			return out;
		}
	private:
		std::string m_directory;
		std::string m_game;

		static action_register<init_action> reg;
};
action_register<init_action> init_action::reg("companion:init");
