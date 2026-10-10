/* Copyright (C) 2016+ AzerothCore, GNU AGPL v3.
 * Adapted from jealous-sound/azerothcore-wotlk-coa:
 * src/server/coa/AscensionAdventurerCache.cpp, revision 006900e2c20574b45cc094dc4d46e78766612531.
 */
#include "GlobalScript.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "LootMgr.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "Random.h"
#include <algorithm>
#include <array>
#include <vector>

namespace
{
constexpr uint32 AdventurerSatchel = 1397884;

enum RewardKind : uint8
{
    Food,
    Potion,
    Material,
    Armor,
    RewardKindCount
};

class CoAContent335Loot : public GlobalScript
{
public:
    CoAContent335Loot() : GlobalScript("CoAContent335Loot", {GLOBALHOOK_ON_BEFORE_LOOT_EQUAL_CHANCED}) { }

    bool OnBeforeLootEqualChanced(Player const* player, std::list<LootStoreItem*> entries,
        Loot& loot, LootStore const& store) override
    {
        if (!player || &store != &LootTemplates_Item)
            return true;

        Item const* container = player->GetItemByGuid(loot.containerGUID);
        if (!container || container->GetEntry() != AdventurerSatchel)
            return true;

        std::array<std::vector<LootStoreItem*>, RewardKindCount> rewards;
        std::array<uint32, RewardKindCount> bestLevel{};
        for (LootStoreItem* entry : entries)
        {
            ItemTemplate const* item = sObjectMgr->GetItemTemplate(entry->itemid);
            if (!item || entry->reference)
                continue;

            RewardKind kind;
            if (item->Class == ITEM_CLASS_TRADE_GOODS)
                kind = Material;
            else if (item->Class == ITEM_CLASS_ARMOR)
                kind = Armor;
            else if (item->Class == ITEM_CLASS_CONSUMABLE && item->SubClass == ITEM_SUBCLASS_FOOD)
                kind = Food;
            else if (item->IsPotion())
                kind = Potion;
            else
                continue;

            uint32 level = kind == Material ? item->ItemLevel : item->RequiredLevel;
            uint32 ceiling = player->GetLevel() + (kind == Material ? 5 : 0);
            if (level > ceiling || (kind != Material && player->CanUseItem(item) != EQUIP_ERR_OK) ||
                (kind == Armor && item->GetSkill() && !player->GetSkillValue(item->GetSkill())))
                continue;

            rewards[kind].push_back(entry);
            bestLevel[kind] = std::max(bestLevel[kind], level);
        }

        std::vector<uint8> kinds;
        for (uint8 kind = 0; kind < RewardKindCount; ++kind)
        {
            auto& candidates = rewards[kind];
            candidates.erase(std::remove_if(candidates.begin(), candidates.end(),
                [kind, &bestLevel](LootStoreItem const* entry)
                {
                    ItemTemplate const* item = sObjectMgr->GetItemTemplate(entry->itemid);
                    uint32 level = kind == Material ? item->ItemLevel : item->RequiredLevel;
                    return level + 10 < bestLevel[kind];
                }), candidates.end());
            if (!candidates.empty())
                kinds.push_back(kind);
        }

        if (!kinds.empty())
        {
            auto const& choices = rewards[kinds[urand(0, uint32(kinds.size() - 1))]];
            loot.AddItem(*choices[urand(0, uint32(choices.size() - 1))]);
        }
        return false;
    }
};
}

void Addmod_coa_content_335Scripts()
{
    new CoAContent335Loot();
}
