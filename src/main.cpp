#include "shared.hpp"

#include "dispatch.hpp"
#include "shaders.hpp"
#include "rules/execution_env.hpp"
#include "rules/rules.hpp"
#include "reflection/reflectionparser.hpp"
#include "reflection/vkreflection.hpp"

#include <ostream>
#include <stdexcept>
#include <string>
#include <vulkan/vulkan_core.h>

#include <vulkan/vulkan.hpp>

#include <nlohmann/json.hpp>
#include <vulkan/vulkan_enums.hpp>
using nlohmann::json;

void parse_json_struct(json& json, void* p, std::string type)
{
	for(auto [key, value] : json.items())
	{
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

VkRect2D rect2D_from_json(json j)
{
	VkRect2D r;
	r.offset = {j["offset"]["x"], j["offset"]["y"]};
	r.extent = {j["extent"]["width"], j["extent"]["height"]};
	return r;
}

void createRenderPass(json& json, VkDevice device)
{
	VkRenderPassCreateInfo rpCreateInfo{};
	rpCreateInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	rpCreateInfo.pNext = nullptr;
	rpCreateInfo.flags = json["flags"];

	std::vector<VkAttachmentDescription> attachments;
	for(auto a : json["attachments"])
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
	for(auto a : json["subpasses"])
	{
		VkSubpassDescription& subpass = subpasses.emplace_back();
		subpass.flags = a["flags"];
		subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;

		std::vector<VkAttachmentReference>& colorAttachments = subpassColorAttachments.emplace_back();
		for(auto b : a["colorAttachments"])
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

	for(auto a : json["shaderStages"])
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
	for(auto a : json["vertexInputState"]["vertexBindingDescriptions"]) parse_json_struct(a, &inputBindings.emplace_back(), "VkVertexInputBindingDescription");
	vertexInputState.vertexBindingDescriptionCount = inputBindings.size();
	vertexInputState.pVertexBindingDescriptions = inputBindings.data();
	std::vector<VkVertexInputAttributeDescription> inputAttributes;
	for(auto a : json["vertexInputState"]["vertexAttributeDescriptions"]) parse_json_struct(a, &inputAttributes.emplace_back(), "VkVertexInputAttributeDescription");
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
	for(auto a : json["viewportState"]["viewports"]) parse_json_struct(a, &viewports.emplace_back(), "VkViewport");
	viewportState.viewportCount = viewports.size();
	viewportState.pViewports = viewports.data();
	std::vector<VkRect2D> scissors;
	for(auto a : json["viewportState"]["scissors"]) scissors.push_back(rect2D_from_json(a));
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

void uploadTestMesh(VkDevice device)
{
}

class init_action : public CheekyLayer::action
{
	public:
		init_action(CheekyLayer::selector_type type) : CheekyLayer::action(type) {
			if(type != CheekyLayer::selector_type::DeviceCreate)
				throw std::runtime_error("the \"companion:init\" action is only supported for device_create selectors, but not for "+to_string(type)+" selectors");
		}

		void read(std::istream& in) override
		{
			std::getline(in, m_directory, ',');
			CheekyLayer::skip_ws(in);
			std::getline(in, m_game, ')');
		}

		void execute(CheekyLayer::selector_type, CheekyLayer::VkHandle handle, CheekyLayer::local_context& ctx, CheekyLayer::rule&) override
		{
			ctx.logger << "Companion initialized for " << m_game << " in directory " << m_directory << "\n";

			VkDevice device = (VkDevice) handle;

			{
				std::ifstream in(m_directory+"/games/"+m_game+"/game.json");
				in >> gameConfig;
			}

			VkPipelineLayoutCreateInfo plCreateInfo{};
			plCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
			plCreateInfo.setLayoutCount = 0;
			plCreateInfo.pushConstantRangeCount = 0;
			if(device_dispatch[GetKey(device)].CreatePipelineLayout(device, &plCreateInfo, nullptr, &pipelineLayout) != VK_SUCCESS)
				throw std::runtime_error("failed to create pipeline layout");

			createRenderPass(gameConfig["renderPass"], device);
			createPipeline(m_directory+"/games/"+m_game, gameConfig["pipeline"], device);
		}

		std::ostream& print(std::ostream& out) override
		{
			out << "companion:init(" << m_directory << ", " << m_game << ")";
			return out;
		}
	private:
		std::string m_directory;
		std::string m_game;

		static CheekyLayer::action_register<init_action> reg;
};
CheekyLayer::action_register<init_action> init_action::reg("companion:init");
