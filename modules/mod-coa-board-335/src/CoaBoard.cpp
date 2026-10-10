/*
 * Copyright (C) 2016+ AzerothCore contributors.
 * This program is free software under the GNU Affero General Public License, version 3 or later.
 * Native quest-menu adaptation informed by CoA mod-hero-call-board (jealous-sound).
 */

#include "Chat.h"
#include "GameObject.h"
#include "GossipDef.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "QuestDef.h"
#include "World.h"
#include <ctime>
#include "ScriptMgr.h"
#include "ScriptedGossip.h"
#include <algorithm>
#include <array>
#include <string>

void RegisterCoaDungeonPilot();
void RegisterCoaPvP();
void RegisterCoaWeekly();

namespace
{
    enum BoardAction : uint32
    {
        ActionHome = 1,
        ActionSupplies = 2,
        ActionCombat = 3,
        ActionDungeons = 4,
        ActionPvP = 5,
        ActionWeekly = 6
    };

    constexpr std::array<uint32, 16> SupplyQuests =
    {
        26035, 26053, 1005092, 1005124, 1005170, 1005259, 1005301, 1005468,
        1005474, 1005647, 1005660, 1005664, 1007453, 1007496, 1038797, 1038884
    };

    constexpr std::array<uint32, 21> CombatQuests =
    {
        17838, 18017, 18018, 18030, 18034, 18036, 18044, 18045, 18046, 18054,
        18055, 18056, 18089, 18090, 18091, 18104, 18105, 18106, 18109, 18110, 18111
    };

    constexpr std::array<uint32, 3> DungeonQuests = { 81244, 81245, 81246 };

    constexpr std::array<uint32, 2> PvPQuests = { 80651, 81260 };

    constexpr std::array<uint32, 2> WeeklyQuests = { 100077, 100078 };

    // Indices shared with the addon catalog: seven PvE groups, supplies = 7, dungeons = 8.
    constexpr uint32 GroupOpen = 100;
    constexpr uint32 GroupAccept = 200;
    constexpr uint32 GroupReward = 300;

    uint32 QuestGroup(uint32 id)
    {
        switch (id)
        {
            case 81244: case 81245: case 81246: return 8;
            case 80651: case 81260: return 9;
            case 100077: case 100078: return 10;
            case 17838: return 0;
            case 18034: case 18036: return 1;
            case 18030: case 18044: case 18045: case 18046: return 2;
            case 18054: case 18055: case 18056: return 3;
            case 18017: case 18018: return 4;
            case 18089: case 18090: case 18091: return 5;
            case 18104: case 18105: case 18106: case 18109: case 18110: case 18111: return 6;
            default:
                return std::find(SupplyQuests.begin(), SupplyQuests.end(), id) != SupplyQuests.end() ? 7 : 11;
        }
    }

    bool IsBoard(GameObject* board)
    {
        return board && (board->GetEntry() == 402000 || board->GetEntry() == 402001);
    }

    // A signature in standard gossip options lets the addon recognize only this board.
    // Every category includes both navigation options; no custom client API is needed.
    void AddBoardNavigation(Player* player, char const* category)
    {
        if (std::string(category) != "Tablon COA: PvE")
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "PvE", GOSSIP_SENDER_MAIN, ActionCombat);
        if (std::string(category) != "Tablon COA: Profesiones")
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Profesiones", GOSSIP_SENDER_MAIN, ActionSupplies);
        if (std::string(category) != "Tablon COA: Mazmorras")
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Mazmorras", GOSSIP_SENDER_MAIN, ActionDungeons);
        if (std::string(category) != "Tablon COA: PvP")
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "PvP", GOSSIP_SENDER_MAIN, ActionPvP);
        if (std::string(category) == "Tablon COA: Inicio")
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Semanales", GOSSIP_SENDER_MAIN, ActionWeekly);
        std::string signature = category;
        if (signature == "Tablon COA: Semanales")
        {
            auto seconds = sWorld->GetNextWeeklyQuestsResetTime().count() - static_cast<int64>(std::time(nullptr));
            signature += " reset=" + std::to_string(std::max<int64>(0, seconds));
        }
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, signature, GOSSIP_SENDER_MAIN, ActionHome);
    }

    void ShowHome(Player* player, GameObject* board)
    {
        ClearGossipMenuFor(player);
        AddBoardNavigation(player, "Tablon COA: Inicio");
        SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, board->GetGUID());
    }

    template <std::size_t Size>
    void ShowQuests(Player* player, GameObject* board, std::array<uint32, Size> const& quests, char const* category, int32 group = -1)
    {
        static_assert(Size + 4 + (Size == CombatQuests.size() ? 7 : 1) <= GOSSIP_MAX_MENU_ITEMS, "Quest category exceeds the client menu limit");
        ClearGossipMenuFor(player);
        QuestMenu& menu = player->PlayerTalkClass->GetQuestMenu();
        for (uint32 questId : quests)
        {
            if (group >= 0 && QuestGroup(questId) != static_cast<uint32>(group))
                continue;
            Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
            if (!quest)
                continue;

            QuestStatus status = player->GetQuestStatus(questId);
            if (board->hasInvolvedQuest(questId)
                && (status == QUEST_STATUS_INCOMPLETE || status == QUEST_STATUS_COMPLETE))
                menu.AddMenuItem(questId, 4);
            else if (board->hasQuest(questId) && player->CanSeeStartQuest(quest)
                && player->CanTakeQuest(quest, false))
                menu.AddMenuItem(questId, 2);
        }

        AddBoardNavigation(player, category);
        if (group >= 0)
        {
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "COA bulk accept: " + std::to_string(group), GOSSIP_SENDER_MAIN, GroupAccept + group);
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "COA bulk reward: " + std::to_string(group), GOSSIP_SENDER_MAIN, GroupReward + group);
        }
        else
        {
            uint32 first = std::string(category) == "Tablon COA: Semanales" ? 10 : std::string(category) == "Tablon COA: PvP" ? 9 : Size == SupplyQuests.size() ? 7 : (Size == DungeonQuests.size() ? 8 : 0);
            uint32 last = std::string(category) == "Tablon COA: Semanales" ? 10 : std::string(category) == "Tablon COA: PvP" ? 9 : Size == SupplyQuests.size() ? 7 : (Size == DungeonQuests.size() ? 8 : 6);
            for (uint32 index = first; index <= last; ++index)
                AddGossipItemFor(player, GOSSIP_ICON_CHAT, "COA group: " + std::to_string(index), GOSSIP_SENDER_MAIN, GroupOpen + index);
        }
        SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, board->GetGUID());
    }
    void ShowCategory(Player* player, GameObject* board, uint32 group, bool onlyGroup)
    {
        if (group == 7)
            ShowQuests(player, board, SupplyQuests, "Tablon COA: Profesiones", onlyGroup ? 7 : -1);
        else if (group == 8)
            ShowQuests(player, board, DungeonQuests, "Tablon COA: Mazmorras", onlyGroup ? 8 : -1);
        else if (group == 9)
            ShowQuests(player, board, PvPQuests, "Tablon COA: PvP", onlyGroup ? 9 : -1);
        else if (group == 10)
            ShowQuests(player, board, WeeklyQuests, "Tablon COA: Semanales", onlyGroup ? 10 : -1);
        else
            ShowQuests(player, board, CombatQuests, "Tablon COA: PvE", onlyGroup ? static_cast<int32>(group) : -1);
    }

    template <std::size_t Size>
    void ApplyGroup(Player* player, GameObject* board, std::array<uint32, Size> const& quests, uint32 group, bool reward)
    {
        uint32 changed = 0, skipped = 0;
        for (uint32 id : quests)
        {
            if (QuestGroup(id) != group)
                continue;
            Quest const* quest = sObjectMgr->GetQuestTemplate(id);
            if (!quest)
                continue;
            if (!reward)
            {
                // Recheck on each iteration: log capacity and source-item space can change.
                if (board->hasQuest(id) && player->GetQuestStatus(id) == QUEST_STATUS_NONE
                    && player->CanSeeStartQuest(quest) && player->CanTakeQuest(quest, false))
                {
                    if (!player->CanAddQuest(quest, false)) { ++skipped; continue; }
                    player->AddQuestAndCheckCompletion(quest, board);
                    ++changed;
                }
            }
            else if (board->hasInvolvedQuest(id) && player->GetQuestStatus(id) == QUEST_STATUS_COMPLETE)
            {
                // Never choose a reward for the player. Native checks cover required items,
                // money, cooldowns and inventory capacity for all fixed rewards.
                if (quest->GetRewChoiceItemsCount() != 0 || !player->CanRewardQuest(quest, 0, false))
                { ++skipped; continue; }
                player->RewardQuest(quest, 0, board);
                ++changed;
            }
        }
        std::string summary = "Tablon COA: " + std::to_string(changed)
            + (reward ? " misiones entregadas; " : " misiones aceptadas; ")
            + std::to_string(skipped) + " omitidas por requisitos, espacio o recompensa a elegir.";
        ChatHandler(player->GetSession()).SendSysMessage(summary.c_str());
    }
}

class go_coa_native_board_335 final : public GameObjectScript
{
public:
    go_coa_native_board_335() : GameObjectScript("coa_native_board_335") { }

    bool OnGossipHello(Player* player, GameObject* board) override
    {
        if (!IsBoard(board))
            return false;

        ShowHome(player, board);
        return true;
    }

    bool OnGossipSelect(Player* player, GameObject* board, uint32 sender, uint32 action) override
    {
        if (!IsBoard(board) || sender != GOSSIP_SENDER_MAIN)
            return false;

        bool open = action >= GroupOpen && action < GroupOpen + 11;
        bool accept = action >= GroupAccept && action < GroupAccept + 11;
        bool reward = action >= GroupReward && action < GroupReward + 11;
        if (open || accept || reward)
        {
            uint32 group = action - (open ? GroupOpen : accept ? GroupAccept : GroupReward);
            if (open) ShowCategory(player, board, group, true);
            else
            {
                if (group == 7) ApplyGroup(player, board, SupplyQuests, group, reward);
                else if (group == 8) ApplyGroup(player, board, DungeonQuests, group, reward);
                else if (group == 9) ApplyGroup(player, board, PvPQuests, group, reward);
                else if (group == 10) ApplyGroup(player, board, WeeklyQuests, group, reward);
                else ApplyGroup(player, board, CombatQuests, group, reward);
                ShowCategory(player, board, group, false);
            }
            return true;
        }

        switch (action)
        {
            case ActionHome:
                ShowHome(player, board);
                break;
            case ActionSupplies:
                ShowQuests(player, board, SupplyQuests, "Tablon COA: Profesiones");
                break;
            case ActionCombat:
                ShowQuests(player, board, CombatQuests, "Tablon COA: PvE");
                break;
            case ActionDungeons:
                ShowQuests(player, board, DungeonQuests, "Tablon COA: Mazmorras");
                break;
            case ActionPvP:
                ShowQuests(player, board, PvPQuests, "Tablon COA: PvP");
                break;
            case ActionWeekly:
                ShowQuests(player, board, WeeklyQuests, "Tablon COA: Semanales");
                break;
            default:
                return false;
        }
        return true;
    }

    bool OnQuestReward(Player* player, GameObject* board, Quest const* quest, uint32 /*reward*/) override
    {
        if (!IsBoard(board))
            return false;

        uint32 id = quest->GetQuestId();
        if (std::find(SupplyQuests.begin(), SupplyQuests.end(), id) != SupplyQuests.end())
            ShowQuests(player, board, SupplyQuests, "Tablon COA: Profesiones");
        else if (std::find(DungeonQuests.begin(), DungeonQuests.end(), id) != DungeonQuests.end())
            ShowQuests(player, board, DungeonQuests, "Tablon COA: Mazmorras");
        else if (std::find(PvPQuests.begin(), PvPQuests.end(), id) != PvPQuests.end())
            ShowQuests(player, board, PvPQuests, "Tablon COA: PvP");
        else if (std::find(WeeklyQuests.begin(), WeeklyQuests.end(), id) != WeeklyQuests.end())
            ShowQuests(player, board, WeeklyQuests, "Tablon COA: Semanales");
        else if (std::find(CombatQuests.begin(), CombatQuests.end(), id) != CombatQuests.end())
            ShowQuests(player, board, CombatQuests, "Tablon COA: PvE");
        else
            return false;

        return true;
    }
};

void Addmod_coa_board_335Scripts()
{
    new go_coa_native_board_335();
    RegisterCoaDungeonPilot();
    RegisterCoaPvP();
    RegisterCoaWeekly();
    LOG_INFO("server.loading", "COA mythic board 1.0: quest 81246; native MythicPlus completion; category 8");
    LOG_INFO("server.loading", "COA native board 335: categories registered; addon interface 0.2; group bulk actions registered; normal and heroic dungeons 0.4.2");
}
