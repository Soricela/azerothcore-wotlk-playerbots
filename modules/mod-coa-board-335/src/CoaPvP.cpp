/* Copyright (C) 2016+ AzerothCore contributors. GNU AGPL v3.
 * PvP events adapted from CoA mod-hero-call-board; original quest rewards preserved.
 */
#include "PlayerScript.h"
#include "AllBattlegroundScript.h"
#include "Battleground.h"
#include "Player.h"
#include "QuestDef.h"
#include "Log.h"

namespace
{
    constexpr uint32 DuelQuest = 80651;
    constexpr uint32 DuelCredit = 990080027;
    constexpr uint32 BattlegroundQuest = 81260;

    class CoaBoardDuels final : public PlayerScript
    {
    public:
        CoaBoardDuels() : PlayerScript("CoaBoardDuels", { PLAYERHOOK_ON_DUEL_END }) { }
        void OnPlayerDuelEnd(Player* winner, Player* loser, DuelCompleteType type) override
        {
            if (type != DUEL_WON || !winner || !loser || winner == loser
                || !winner->IsInWorld() || !loser->IsInWorld()
                || winner->GetQuestStatus(DuelQuest) != QUEST_STATUS_INCOMPLETE)
                return;
            winner->KilledMonsterCredit(DuelCredit);
        }
    };

    class CoaBoardBattlegrounds final : public AllBattlegroundScript
    {
    public:
        CoaBoardBattlegrounds() : AllBattlegroundScript("CoaBoardBattlegrounds",
            { ALLBATTLEGROUNDHOOK_ON_BATTLEGROUND_END_REWARD }) { }
        void OnBattlegroundEndReward(Battleground* bg, Player* player, TeamId winner) override
        {
            if (!bg || !player || bg->isArena() || winner == TEAM_NEUTRAL
                || player->GetBattleground() != bg || player->GetBgTeamId() != winner
                || player->GetQuestStatus(BattlegroundQuest) != QUEST_STATUS_INCOMPLETE)
                return;
            player->AreaExploredOrEventHappens(BattlegroundQuest);
        }
    };
}

void RegisterCoaPvP()
{
    new CoaBoardDuels();
    new CoaBoardBattlegrounds();
    LOG_INFO("server.loading", "COA PvP 0.5.0: five duel wins 80651/990080027; battleground win 81260; arenas excluded");
}
