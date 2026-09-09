#pragma once

#include <set>
#include "playerbot/strategy/Action.h"
#include "QuestAction.h"

namespace ai
{
    class SayAction : public Action, public Qualified
    {
    public:
        SayAction(PlayerbotAI* ai);
        virtual bool Execute(Event& event) override;
        virtual bool isUseful() override;
        virtual std::string getName() override { return "say::" + qualifier; }
        virtual bool isUsefulWhenStunned() override { return true; }

    private:
    };

    class SpeakAction : public Action, public Qualified
    {
    public:
        SpeakAction(PlayerbotAI* ai) : Action(ai, "speak"), Qualified() {};
        virtual bool Execute(Event& event) override;
        virtual bool isUsefulWhenStunned() override { return true; }

#ifdef GenerateBotHelp
        virtual std::string GetHelpName() { return "speak"; } //Must equal iternal name
        virtual std::string GetHelpDescription()
        {
            return "This action wil make bots speak a certain line\n"
                   "Use \\p, \\1 \\y ect to make bots use different channels.";
        }
        virtual std::vector<std::string> GetUsedActions() { return {}; }
        virtual std::vector<std::string> GetUsedValues() { return {""}; }
#endif    
    };
}
