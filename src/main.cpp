#include "rules/execution_env.hpp"
#include "rules/rules.hpp"
#include <ostream>

class init_action : public CheekyLayer::action
{
	public:
		init_action(CheekyLayer::selector_type type) : CheekyLayer::action(type) {}

		void read(std::istream& in) override
		{
			std::getline(in, m_game, ')');
		}

		void execute(CheekyLayer::selector_type, CheekyLayer::VkHandle, CheekyLayer::local_context& ctx, CheekyLayer::rule&) override
		{
			ctx.logger << "Companion initialized for " << m_game << "!";
		}

		std::ostream& print(std::ostream& out) override
		{
			out << "companion:init(" << m_game << ")";
			return out;
		}
	private:
		std::string m_game;

		static CheekyLayer::action_register<init_action> reg;
};
CheekyLayer::action_register<init_action> init_action::reg("companion:init");
