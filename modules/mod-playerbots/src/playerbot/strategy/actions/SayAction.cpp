
#include "playerbot/playerbot.h"
#include "SayAction.h"
#include "playerbot/PlayerbotTextMgr.h"
#include "playerbot/ServerFacade.h"

using namespace ai;

SayAction::SayAction(PlayerbotAI* ai) : Action(ai, "say"), Qualified()
{
}

bool SayAction::Execute(Event& event)
{
    std::string text = "";
    std::map<std::string, std::string> placeholders;
    Unit* target = AI_VALUE(Unit*, "tank target");
    if (!target) target = AI_VALUE(Unit*, "current target");

    // set replace std::strings
    if (target) placeholders["<target>"] = target->GetName();
    placeholders["<randomfaction>"] = IsAlliance(bot->getRace()) ? "Alliance" : "Horde";
    if (qualifier == "low ammo" || qualifier == "no ammo")
    {
        Item* const pItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_RANGED);
        if (pItem)
        {
            switch (pItem->GetProto()->SubClass)
            {
            case ITEM_SUBCLASS_WEAPON_GUN:
                placeholders["<ammo>"] = "bullets";
                break;
            case ITEM_SUBCLASS_WEAPON_BOW:
            case ITEM_SUBCLASS_WEAPON_CROSSBOW:
                placeholders["<ammo>"] = "arrows";
                break;
            }
        }
    }

    if (bot->IsInWorld())
    {
        if (AreaTableEntry const* area = GetAreaEntryByAreaID(sServerFacade.GetAreaId(bot)))
            placeholders["<subzone>"] = area->area_name[0];
    }

    // set delay before next say
    time_t lastSaid = AI_VALUE2(time_t, "last said", qualifier);
    uint32 nextTime = time(0) + urand(1, 30);
    ai->GetAiObjectContext()->GetValue<time_t>("last said", qualifier)->Set(nextTime);

    Group* group = bot->GetGroup();
    if (group)
    {
        std::vector<Player*> members;
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->getSource();
            PlayerbotAI* memberAi = GetBotAI(member);
            if (memberAi) members.push_back(member);
        }

        uint32 count = members.size();
        if (count > 1)
        {
            for (uint32 i = 0; i < count * 5; i++)
            {
                int i1 = urand(0, count - 1);
                int i2 = urand(0, count - 1);

                Player* item = members[i1];
                members[i1] = members[i2];
                members[i2] = item;
            }
        }

        int index = 0;
        for (std::vector<Player*>::iterator i = members.begin(); i != members.end(); ++i)
        {
            PlayerbotAI* memberAi = GetBotAI((*i));
            if (memberAi)
                memberAi->GetAiObjectContext()->GetValue<time_t>("last said", qualifier)->Set(nextTime + (20 * ++index) + urand(1, 15));
        }
    }

    // load text based on chance
    if (!sPlayerbotTextMgr.GetBotText(qualifier, text, placeholders))
        return false;

    if (text.find("/y ") == 0)
        ai->Yell(text.substr(3));
    else
        ai->Say(text);

    return true;
}

bool SayAction::isUseful()
{
    if (!ai->AllowActivity())
        return false;

    if (ai->HasStrategy("silent", BotState::BOT_STATE_NON_COMBAT))
        return false;

    time_t lastSaid = AI_VALUE2(time_t, "last said", qualifier);
    return (time(0) - lastSaid) > 30;
}

bool SpeakAction::Execute(Event& event)
{
    bool botsTalkLikePlayers = true;

    std::string text = event.getParam();
    if (text.find("/y ") == 0)
        ai->Yell(text.substr(3), botsTalkLikePlayers);
    else if (text.find("/p ") == 0)
        ai->SayToParty(text.substr(3), botsTalkLikePlayers);
    else if (text.find("/r ") == 0)
        ai->SayToRaid(text.substr(3));
    else if (text.find("/g ") == 0)
        ai->SayToGuild(text.substr(3), botsTalkLikePlayers);
    else if (text.find("/s ") == 0)
        ai->Say(text.substr(3), botsTalkLikePlayers);
    else if (text.find("/1 ") == 0)
        ai->SayToGeneral(text.substr(3));
    else if (text.find("/2 ") == 0)
        ai->SayToTrade(text.substr(3));
    else if (text.find("/3 ") == 0)
        ai->SayToLocalDefense(text.substr(3));
    else if (text.find("/4 ") == 0)
        ai->SayToLFG(text.substr(3));
    else
        ai->Say(text, botsTalkLikePlayers);

    return true;
}