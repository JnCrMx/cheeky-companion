#include "dispatch.hpp"
#include "logger.hpp"
#include "shared.hpp"
#include "draw.hpp"
#include "descriptors.hpp"

#include "rules/rules.hpp"
#include <exception>
#include <istream>
#include <stdexcept>

#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan.hpp>

using namespace CheekyLayer::rules;

class draw_action : public action
{
	public:
		draw_action(selector_type type) : action(type) {
			if(type != selector_type::Draw)
				throw std::runtime_error("the \"companion:draw\" action is only supported for draw selectors, but not for "+to_string(type)+" selectors");
		}

		void read(std::istream& in) override
		{
			check_stream(in, ')');
		}

		void execute(selector_type, VkHandle handle, local_context& ctx, rule&) override
		{
			if(!ready)
			{
				ctx.logger << CheekyLayer::logger::error << "companion not ready; maybe it was not initialized or the initialization failed";
				return;
			}
			try
			{
				updateGeneralVariables();
			}
			catch(const std::exception& ex)
			{
				ctx.logger << CheekyLayer::logger::error << "failed to perform general updates: " << ex.what();
			}

			VkRenderPassBeginInfo info{};
			info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
			info.renderPass = renderPass;
			info.framebuffer = ctx.commandBufferState->framebuffer;
			info.renderArea = rect2D_from_json(gameConfig["renderPassBegin"]["renderArea"]);

			std::vector<VkClearValue> clearValues(gameConfig["renderPass"]["subpasses"][0]["colorAttachments"].size()+1);
			info.pClearValues = clearValues.data();
			info.clearValueCount = clearValues.size();

			device_dispatch[GetKey(ctx.device)].CmdEndRenderPass(ctx.commandBuffer);
			device_dispatch[GetKey(ctx.device)].CmdBeginRenderPass(ctx.commandBuffer, &info, VK_SUBPASS_CONTENTS_INLINE);

			auto descriptorCount = gameConfig["descriptors"].size();
			std::vector<std::vector<uint32_t>> dynamicOffsets(clients.size());
			std::vector<VkDescriptorImageInfo> imageInfos;		imageInfos.reserve(descriptorCount * clients.size());
			std::vector<VkDescriptorBufferInfo> bufferInfos;	bufferInfos.reserve(descriptorCount * clients.size());
			std::vector<VkWriteDescriptorSet> writes;
			std::vector<VkCopyDescriptorSet> copies;
			for(int i=0; i<clients.size(); i++)
			{
				auto& client = clients[i];
				dynamicOffsets[i].resize(descriptorCount);
				for(auto& d : gameConfig["descriptors"])
				{
					int dstBinding = d["binding"];
					try
					{
						auto& source = d["_source"];
						if(source["type"]=="steal")
						{
							int srcBinding = source["binding"];
							VkDescriptorSet set = ctx.commandBufferState->descriptorSets.at(0);

							VkCopyDescriptorSet& copy = copies.emplace_back();
							copy.sType = VK_STRUCTURE_TYPE_COPY_DESCRIPTOR_SET;
							copy.srcSet = set;
							copy.srcBinding = srcBinding;
							copy.srcArrayElement = 0;
							copy.dstSet = client->descriptor_set();
							copy.dstBinding = dstBinding;
							copy.dstArrayElement = 0;
							copy.descriptorCount = 1;

							if(ctx.commandBufferState->descriptorDynamicOffsets.size() > srcBinding)
								dynamicOffsets[i][dstBinding] = ctx.commandBufferState->descriptorDynamicOffsets[srcBinding];
						}
					}
					catch(const std::exception& ex)
					{
						ctx.logger << CheekyLayer::logger::error << "failed to update descriptor binding " << dstBinding << ": " << ex.what();
					}
				}
			}
			device_dispatch[GetKey(ctx.device)].UpdateDescriptorSets(ctx.device, writes.size(), writes.data(), copies.size(), copies.data());
			device_dispatch[GetKey(ctx.device)].CmdBindPipeline(ctx.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

			for(int i=0; i<clients.size(); i++)
			{
				auto& client = clients[i];
				client->update();

				VkDescriptorSet set = client->descriptor_set();
				device_dispatch[GetKey(ctx.device)].CmdBindDescriptorSets(ctx.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 
					1, &set, dynamicOffsets.size(), dynamicOffsets[i].data());
				
				auto& companion = companions[client->companion()];
				companion->draw(ctx.device, ctx.commandBuffer);
			}
		}

		std::ostream& print(std::ostream& out) override
		{
			out << "companion:draw()";
			return out;
		}
	private:

		static action_register<draw_action> reg;
};
action_register<draw_action> draw_action::reg("companion:draw");
