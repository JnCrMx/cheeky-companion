#include "dispatch.hpp"
#include "shared.hpp"
#include "draw.hpp"

#include "rules/rules.hpp"
#include <istream>
#include <vulkan/vulkan_core.h>

class draw_action : public CheekyLayer::action
{
	public:
		draw_action(CheekyLayer::selector_type type) : CheekyLayer::action(type) {
			if(type != CheekyLayer::selector_type::Draw)
				throw std::runtime_error("the \"companion:draw\" action is only supported for draw selectors, but not for "+to_string(type)+" selectors");
		}

		void read(std::istream& in) override
		{
			CheekyLayer::check_stream(in, ')');
		}

		void execute(CheekyLayer::selector_type, CheekyLayer::VkHandle handle, CheekyLayer::local_context& ctx, CheekyLayer::rule&) override
		{
			ctx.logger << "Draw: ";

			VkRenderPassBeginInfo info{};
			info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
			info.renderPass = renderPass;
			info.framebuffer = ctx.commandBufferState->framebuffer;
			info.renderArea = rect2D_from_json(gameConfig["renderPassBegin"]["renderArea"]);

			std::vector<VkClearValue> clearValues(gameConfig["renderPass"]["subpasses"][0]["colorAttachments"].size()+1);
			info.pClearValues = clearValues.data();
			info.clearValueCount = clearValues.size();
			ctx.logger << "clear values: "<< info.clearValueCount << " framebuffer: " << info.framebuffer << " renderPass: " << info.renderPass << " old renderPass: " << ctx.commandBufferState->renderpass;

			device_dispatch[GetKey(ctx.device)].CmdEndRenderPass(ctx.commandBuffer);
			device_dispatch[GetKey(ctx.device)].CmdBeginRenderPass(ctx.commandBuffer, &info, VK_SUBPASS_CONTENTS_INLINE);

			device_dispatch[GetKey(ctx.device)].CmdBindPipeline(ctx.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

			// TODO: draw
			device_dispatch[GetKey(ctx.device)].CmdDraw(ctx.commandBuffer, 3, 1, 0, 0);
		}

		std::ostream& print(std::ostream& out) override
		{
			out << "companion:draw()";
			return out;
		}
	private:

		static CheekyLayer::action_register<draw_action> reg;
};
CheekyLayer::action_register<draw_action> draw_action::reg("companion:draw");
