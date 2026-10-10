/* Copyright (C) 2016+ AzerothCore contributors. GNU AGPL v3.
 * Weekly leadership rewards adapted from CoA; solo play supported.
 */
#include "PlayerScript.h"
#include "GlobalScript.h"
#include "Creature.h"
#include "Group.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "QuestDef.h"
#include "Log.h"
#include <array>

namespace
{
    constexpr uint32 RaidQuest = 100077, RaidCredit = 990101000;
    constexpr uint32 DungeonQuest = 100078, DungeonCredit = 990101001;
    struct FinalRaid { uint32 map, boss; };
    // Original COA finals plus the locally validated Atalzul pilot on map 890.
    constexpr std::array<FinalRaid, 9> FinalRaids = {{
        {249,10184}, {409,11502}, {469,11583}, {309,14834},
        {509,15339}, {531,15727}, {533,15990}, {890,990189001}, {880,9901881}
    }};

    class CoaWeeklyRaids final : public PlayerScript
    {
    public:
        CoaWeeklyRaids() : PlayerScript("CoaWeeklyRaids",
            { PLAYERHOOK_ON_CREATURE_KILL, PLAYERHOOK_ON_CREATURE_KILLED_BY_PET }) { }
        void OnPlayerCreatureKill(Player* player, Creature* killed) override { Credit(player, killed); }
        void OnPlayerCreatureKilledByPet(Player* owner, Creature* killed) override { Credit(owner, killed); }
    private:
        static void Credit(Player* killer, Creature* killed)
        {
            if (!killer || !killed || !killed->GetMap() || !killed->GetMap()->IsRaid()) return;
            bool final = false;
            for (auto const& raid : FinalRaids)
                if (killed->GetMap()->GetId() == raid.map && killed->GetOriginalEntry() == raid.boss) final = true;
            if (!final) return;
            Group const* group = killer->GetGroup();
            Player* recipient = group ? ObjectAccessor::FindPlayer(group->GetLeaderGUID()) : killer;
            if (!recipient || !recipient->IsInWorld() || recipient->FindMap() != killed->GetMap()
                || !recipient->IsAtGroupRewardDistance(killed)
                || recipient->GetQuestStatus(RaidQuest) != QUEST_STATUS_INCOMPLETE) return;
            recipient->KilledMonsterCredit(RaidCredit);
        }
    };

    class CoaWeeklyDungeons final : public GlobalScript
    {
    public:
        CoaWeeklyDungeons() : GlobalScript("CoaWeeklyDungeons", { GLOBALHOOK_ON_AFTER_UPDATE_ENCOUNTER_STATE }) { }
        void OnAfterUpdateEncounterState(Map* map, EncounterCreditType, uint32, Unit* source,
            Difficulty, std::list<DungeonEncounter const*> const*, uint32 dungeonId, bool updated) override
        {
            if (!updated || !dungeonId || !map || !map->IsNonRaidDungeon() || !source || source->FindMap() != map) return;
            if (map->GetDifficulty() != DUNGEON_DIFFICULTY_NORMAL && map->GetDifficulty() != DUNGEON_DIFFICULTY_HEROIC) return;
            for (auto const& reference : map->GetPlayers())
            {
                Player* player = reference.GetSource();
                if (!player || !player->IsInWorld() || !player->IsAtGroupRewardDistance(source)
                    || player->GetQuestStatus(DungeonQuest) != QUEST_STATUS_INCOMPLETE) continue;
                Group const* group = player->GetGroup();
                if (!group || group->IsLeader(player->GetGUID())) player->KilledMonsterCredit(DungeonCredit);
            }
        }
    };
}

void RegisterCoaWeekly()
{
    new CoaWeeklyRaids();
    new CoaWeeklyDungeons();
    LOG_INFO("server.loading", "COA weekly 0.9.0: dungeon 100078; nine supported raid finals 100077 including Atalzul 890 and Setis 880; solo or group leader");
}
