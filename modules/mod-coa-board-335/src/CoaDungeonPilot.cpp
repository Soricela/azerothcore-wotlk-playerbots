/* Copyright (C) 2016+ AzerothCore contributors. GNU AGPL v3. */
#include "GlobalScript.h"
#include "Group.h"
#include "LFGMgr.h"
#include "Map.h"
#include "Player.h"
#include "QuestDef.h"
#include "Log.h"

namespace
{
    constexpr uint32 NormalQuest = 81244;
    constexpr uint32 HeroicQuest = 81245;
    constexpr uint32 NormalCredit = 990081042;
    constexpr uint32 HeroicCredit = 990081041;

    class CoaDungeonPilot final : public GlobalScript
    {
    public:
        CoaDungeonPilot() : GlobalScript("CoaDungeonPilot", { GLOBALHOOK_ON_AFTER_UPDATE_ENCOUNTER_STATE }) { }

        void OnAfterUpdateEncounterState(Map* map, EncounterCreditType, uint32, Unit* source,
            Difficulty, std::list<DungeonEncounter const*> const*, uint32 dungeonId, bool updated) override
        {
            // The core supplies a nonzero dungeonId only for a final encounter.
            // This hook runs BEFORE LFGMgr::FinishDungeon changes the LFG state.
            if (!updated || !dungeonId || !map || !map->IsNonRaidDungeon()
                || !source || source->FindMap() != map)
                return;

            auto const* dungeon = sLFGMgr->GetLFGDungeon(dungeonId);
            bool const validLfgDungeon = dungeon && dungeon->map == map->GetId()
                && dungeon->difficulty == map->GetDifficulty();

            uint32 questId = 0, creditId = 0;
            switch (map->GetDifficulty())
            {
                case DUNGEON_DIFFICULTY_NORMAL: questId = NormalQuest; creditId = NormalCredit; break;
                case DUNGEON_DIFFICULTY_HEROIC: questId = HeroicQuest; creditId = HeroicCredit; break;
                default: return;
            }

            for (auto const& reference : map->GetPlayers())
            {
                Player* player = reference.GetSource();
                if (!player || !player->IsInWorld() || !player->IsAtGroupRewardDistance(source)
                    || player->GetQuestStatus(questId) != QUEST_STATUS_INCOMPLETE)
                    continue;
                Group const* group = player->GetGroup();
                bool const soloHeroic = questId == HeroicQuest && (!group || group->GetMembersCount() == 1);
                bool const eligibleLfg = validLfgDungeon && group && group->isLFGGroup() && sLFGMgr->GetDungeon(group->GetGUID()) == dungeonId
                    && sLFGMgr->GetOldState(group->GetGUID()) == lfg::LFG_STATE_DUNGEON
                    && sLFGMgr->GetOldState(player->GetGUID()) == lfg::LFG_STATE_DUNGEON;
                if (soloHeroic || eligibleLfg)
                    player->KilledMonsterCredit(creditId);
            }
        }
    };
}

void RegisterCoaDungeonPilot()
{
    new CoaDungeonPilot();
    LOG_INFO("server.loading", "COA dungeons 0.4.2: normal LFG; heroic LFG or solo; final encounter only");
}
