#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "ScriptedGossip.h"
#include "Player.h"
#include "Creature.h"
#include "World.h"
#include "Map.h"
#include "MapMgr.h"
#include "ObjectAccessor.h"
#include "Chat.h"
#include "GossipDef.h"
#include "Config.h"
#include "Group.h"
#include "TemporarySummon.h"
#include "Log.h"

#include <set>
#include <vector>
#include <string>
#include <sstream>
#include <chrono>

using namespace std::chrono_literals;

namespace InvasionData
{
    static constexpr uint32 NPC_EVENT_MASTER_ENTRY = 900091;
    static constexpr uint32 NPC_EVENT_PORTAL_ENTRY = 900090;
    static constexpr uint32 NPC_INVASION_ELITE_ENTRY = 900098;
    static constexpr uint32 NPC_INVASION_BOSS_ENTRY  = 900099;

    static constexpr uint32 CITY_MAP_ID = 0;
    static constexpr float CITY_PORTAL_X = -8833.37f;
    static constexpr float CITY_PORTAL_Y = 628.62f;
    static constexpr float CITY_PORTAL_Z = 94.01f;
    static constexpr float CITY_PORTAL_O = 0.88f;

    static constexpr uint32 EVENT_MAP_ID = 0;
    static constexpr float EVENT_CENTER_X = -11182.0f;
    static constexpr float EVENT_CENTER_Y = -3014.0f;
    static constexpr float EVENT_CENTER_Z = 7.40f;
    static constexpr float EVENT_CENTER_O = 2.10f;

    static constexpr float BOSS_X = -11178.0f;
    static constexpr float BOSS_Y = -3010.0f;
    static constexpr float BOSS_Z = 7.80f;
    static constexpr float BOSS_O = 1.57f;

    static constexpr float ELITE_1_X = -11162.0f;
    static constexpr float ELITE_1_Y = -3002.0f;
    static constexpr float ELITE_1_Z = 7.10f;
    static constexpr float ELITE_1_O = 1.10f;

    static constexpr float ELITE_2_X = -11195.0f;
    static constexpr float ELITE_2_Y = -3018.0f;
    static constexpr float ELITE_2_Z = 7.20f;
    static constexpr float ELITE_2_O = 2.40f;

    static constexpr float ELITE_3_X = -11186.0f;
    static constexpr float ELITE_3_Y = -2988.0f;
    static constexpr float ELITE_3_Z = 7.10f;
    static constexpr float ELITE_3_O = 3.80f;

    static constexpr float ELITE_4_X = -11203.0f;
    static constexpr float ELITE_4_Y = -2999.0f;
    static constexpr float ELITE_4_Z = 7.50f;
    static constexpr float ELITE_4_O = 4.50f;

    static constexpr uint32 EVENT_DURATION_MS = 30 * 60 * 1000;
    static constexpr float REWARD_RADIUS = 120.0f;
    static constexpr uint32 REWARD_GOLD_COPPER = 500000; // 50g

    static constexpr uint32 ACTION_START_EVENT = 1001;
    static constexpr uint32 ACTION_TELEPORT_TO_EVENT = 1002;
    static constexpr uint32 ACTION_EVENT_STATUS = 1003;
}

class InvasionMgr
{
public:
    static InvasionMgr& Instance()
    {
        static InvasionMgr instance;
        return instance;
    }

    bool IsActive() const
    {
        return _active;
    }

    uint32 GetRemainingMs() const
    {
        return _remainingMs;
    }

    ObjectGuid GetPortalGuid() const
    {
        return _portalGuid;
    }

    ObjectGuid GetBossGuid() const
    {
        return _bossGuid;
    }

    bool StartEvent(Player* starter)
    {
        if (!starter)
            return false;

        if (_active)
            return false;

        Map* eventMap = sMapMgr->CreateBaseMap(InvasionData::EVENT_MAP_ID);
        if (!eventMap)
            return false;

        _active = true;
        _remainingMs = InvasionData::EVENT_DURATION_MS;
        _spawnedCreatures.clear();
        _bossGuid.Clear();
        _portalGuid.Clear();

        SpawnPortal();
        SpawnEncounter(eventMap);

        BroadcastMessage(
            "[Invasion] Las fuerzas demoníacas han irrumpido en las Tierras Devastadas. "
            "Hablad con el portal en Ventormenta o con el Maestro del Evento para uniros al combate."
        );

        return true;
    }

    void Update(uint32 diff)
    {
        if (!_active)
            return;

        if (diff >= _remainingMs)
        {
            EndEvent(false, nullptr);
            return;
        }

        _remainingMs -= diff;
    }

    void EndEvent(bool success, Player* /*killer*/)
    {
        if (!_active)
            return;

        if (success)
        {
            RewardNearbyPlayers();
            BroadcastMessage("[Invasion] El boss final ha sido derrotado. La invasión ha terminado.");
        }
        else
        {
            BroadcastMessage("[Invasion] El tiempo ha expirado. Las fuerzas invasoras se retiran.");
        }

        CleanupSpawns();

        _active = false;
        _remainingMs = 0;
        _portalGuid.Clear();
        _bossGuid.Clear();
        _spawnedCreatures.clear();
    }

    void OnBossKilled(Creature* boss, Unit* killer)
    {
        if (!_active || !boss)
            return;

        Player* playerKiller = nullptr;

        if (killer)
        {
            if (killer->GetTypeId() == TYPEID_PLAYER)
                playerKiller = killer->ToPlayer();
            else if (Unit* owner = killer->GetOwner())
                if (owner->GetTypeId() == TYPEID_PLAYER)
                    playerKiller = owner->ToPlayer();
        }

        std::ostringstream msg;
        msg << "[Invasion] ";
        if (playerKiller)
            msg << playerKiller->GetName();
        else
            msg << "Un héroe";
        msg << " ha abatido al Señor de la Invasión.";

        BroadcastMessage(msg.str());

        EndEvent(true, playerKiller);
    }

    bool TeleportPlayerToEvent(Player* player)
    {
        if (!player)
            return false;

        if (!_active)
            return false;

        player->TeleportTo(
            InvasionData::EVENT_MAP_ID,
            InvasionData::EVENT_CENTER_X,
            InvasionData::EVENT_CENTER_Y,
            InvasionData::EVENT_CENTER_Z,
            InvasionData::EVENT_CENTER_O);

        return true;
    }

    std::string GetStatusText() const
    {
        if (!_active)
            return "No hay ninguna invasión activa en este momento.";

        uint32 totalSeconds = _remainingMs / 1000;
        uint32 minutes = totalSeconds / 60;
        uint32 seconds = totalSeconds % 60;

        std::ostringstream ss;
        ss << "La invasión está ACTIVA. Tiempo restante: " << minutes << "m " << seconds << "s.";
        return ss.str();
    }

private:
    InvasionMgr() = default;

    void BroadcastMessage(std::string const& text)
    {
        LOG_INFO("module", "{}", text);

        SendMessageToMapPlayers(InvasionData::CITY_MAP_ID, text);
        if (InvasionData::EVENT_MAP_ID != InvasionData::CITY_MAP_ID)
            SendMessageToMapPlayers(InvasionData::EVENT_MAP_ID, text);
    }

    void SendMessageToMapPlayers(uint32 mapId, std::string const& text)
    {
        Map* map = sMapMgr->CreateBaseMap(mapId);
        if (!map)
            return;

        Map::PlayerList const& players = map->GetPlayers();
        if (players.IsEmpty())
            return;

        for (Map::PlayerList::const_iterator itr = players.begin(); itr != players.end(); ++itr)
        {
            Player* player = itr->GetSource();
            if (!player || !player->IsInWorld())
                continue;

            ChatHandler(player->GetSession()).SendSysMessage(text.c_str());
        }
    }

    void SpawnPortal()
    {
        Map* cityMap = sMapMgr->CreateBaseMap(InvasionData::CITY_MAP_ID);
        if (!cityMap)
            return;

        Position pos;
        pos.Relocate(
            InvasionData::CITY_PORTAL_X,
            InvasionData::CITY_PORTAL_Y,
            InvasionData::CITY_PORTAL_Z,
            InvasionData::CITY_PORTAL_O);

        if (TempSummon* portal = cityMap->SummonCreature(
                InvasionData::NPC_EVENT_PORTAL_ENTRY,
                pos,
                nullptr,
                TEMPSUMMON_MANUAL_DESPAWN,
                0))
        {
            _portalGuid = portal->GetGUID();
            _spawnedCreatures.push_back(_portalGuid);
        }
    }

    void SpawnEncounter(Map* eventMap)
    {
        if (!eventMap)
            return;

        SpawnCreature(eventMap, InvasionData::NPC_INVASION_ELITE_ENTRY,
                      InvasionData::ELITE_1_X, InvasionData::ELITE_1_Y, InvasionData::ELITE_1_Z, InvasionData::ELITE_1_O);

        SpawnCreature(eventMap, InvasionData::NPC_INVASION_ELITE_ENTRY,
                      InvasionData::ELITE_2_X, InvasionData::ELITE_2_Y, InvasionData::ELITE_2_Z, InvasionData::ELITE_2_O);

        SpawnCreature(eventMap, InvasionData::NPC_INVASION_ELITE_ENTRY,
                      InvasionData::ELITE_3_X, InvasionData::ELITE_3_Y, InvasionData::ELITE_3_Z, InvasionData::ELITE_3_O);

        SpawnCreature(eventMap, InvasionData::NPC_INVASION_ELITE_ENTRY,
                      InvasionData::ELITE_4_X, InvasionData::ELITE_4_Y, InvasionData::ELITE_4_Z, InvasionData::ELITE_4_O);

        ObjectGuid bossGuid = SpawnCreature(eventMap, InvasionData::NPC_INVASION_BOSS_ENTRY,
                                            InvasionData::BOSS_X, InvasionData::BOSS_Y, InvasionData::BOSS_Z, InvasionData::BOSS_O);
        _bossGuid = bossGuid;
    }

    ObjectGuid SpawnCreature(Map* map, uint32 entry, float x, float y, float z, float o)
    {
        Position pos;
        pos.Relocate(x, y, z, o);

        if (TempSummon* summon = map->SummonCreature(entry, pos, nullptr, TEMPSUMMON_MANUAL_DESPAWN, 0))
        {
            _spawnedCreatures.push_back(summon->GetGUID());
            return summon->GetGUID();
        }

        return ObjectGuid::Empty;
    }

    void CleanupSpawns()
    {
        std::set<uint32> processedMaps;
        std::vector<uint32> mapsToCheck = {
            InvasionData::CITY_MAP_ID,
            InvasionData::EVENT_MAP_ID
        };

        for (uint32 mapId : mapsToCheck)
        {
            if (processedMaps.count(mapId))
                continue;

            processedMaps.insert(mapId);

            Map* map = sMapMgr->CreateBaseMap(mapId);
            if (!map)
                continue;

            for (ObjectGuid const& guid : _spawnedCreatures)
            {
                if (Creature* creature = map->GetCreature(guid))
                    creature->DespawnOrUnsummon(1000ms);
            }
        }
    }

    void RewardNearbyPlayers()
    {
        Map* map = sMapMgr->CreateBaseMap(InvasionData::EVENT_MAP_ID);
        if (!map)
            return;

        Creature* boss = nullptr;
        if (!_bossGuid.IsEmpty())
            boss = map->GetCreature(_bossGuid);

        Map::PlayerList const& players = map->GetPlayers();
        if (players.IsEmpty())
            return;

        for (Map::PlayerList::const_iterator itr = players.begin(); itr != players.end(); ++itr)
        {
            Player* player = itr->GetSource();
            if (!player || !player->IsInWorld() || !player->IsAlive())
                continue;

            if (boss)
            {
                if (player->GetDistance(boss) > InvasionData::REWARD_RADIUS)
                    continue;
            }
            else
            {
                if (player->GetDistance(InvasionData::BOSS_X, InvasionData::BOSS_Y, InvasionData::BOSS_Z) > InvasionData::REWARD_RADIUS)
                    continue;
            }

            player->ModifyMoney(InvasionData::REWARD_GOLD_COPPER);
            ChatHandler(player->GetSession()).PSendSysMessage(
                "Has recibido 50 oros por participar en la defensa contra la invasión demoníaca.");
        }
    }

private:
    bool _active = false;
    uint32 _remainingMs = 0;
    ObjectGuid _portalGuid;
    ObjectGuid _bossGuid;
    std::vector<ObjectGuid> _spawnedCreatures;
};

class npc_invasion_event_master : public CreatureScript
{
public:
    npc_invasion_event_master() : CreatureScript("npc_invasion_event_master") { }

    bool OnGossipHello(Player* player, Creature* creature) override
    {
        ClearGossipMenuFor(player);

        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Iniciar invasión demoníaca", GOSSIP_SENDER_MAIN, 1);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Teletransportarme a la invasión", GOSSIP_SENDER_MAIN, 2);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Estado del evento", GOSSIP_SENDER_MAIN, 3);

        SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature->GetGUID());
        return true;
    }

    bool OnGossipSelect(Player* player, Creature* creature, uint32 /*sender*/, uint32 action) override
    {
        ClearGossipMenuFor(player);

        switch (action)
        {
            case 1:
            {
                if (InvasionMgr::Instance().IsActive())
                    ChatHandler(player->GetSession()).PSendSysMessage("Ya hay una invasión activa.");
                else if (InvasionMgr::Instance().StartEvent(player))
                    ChatHandler(player->GetSession()).PSendSysMessage("La invasión demoníaca ha comenzado.");
                else
                    ChatHandler(player->GetSession()).PSendSysMessage("No se pudo iniciar la invasión.");
                break;
            }
            case 2:
            {
                if (!InvasionMgr::Instance().IsActive())
                    ChatHandler(player->GetSession()).PSendSysMessage("No hay ninguna invasión activa.");
                else
                    InvasionMgr::Instance().TeleportPlayerToEvent(player);
                break;
            }
            case 3:
            {
                ChatHandler(player->GetSession()).PSendSysMessage("%s", InvasionMgr::Instance().GetStatusText().c_str());
                break;
            }
            default:
                break;
        }

        CloseGossipMenuFor(player);
        return true;
    }
};

class npc_invasion_portal : public CreatureScript
{
public:
    npc_invasion_portal() : CreatureScript("npc_invasion_portal") { }

    bool OnGossipHello(Player* player, Creature* creature) override
    {
        ClearGossipMenuFor(player);

        if (InvasionMgr::Instance().IsActive())
        {
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Atravesar el portal hacia la invasión", GOSSIP_SENDER_MAIN, 1);
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Estado del evento", GOSSIP_SENDER_MAIN, 2);
        }
        else
        {
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "No hay invasión activa.", GOSSIP_SENDER_MAIN, 99);
        }

        SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature->GetGUID());
        return true;
    }

    bool OnGossipSelect(Player* player, Creature* /*creature*/, uint32 /*sender*/, uint32 action) override
    {
        ClearGossipMenuFor(player);

        switch (action)
        {
            case 1:
                InvasionMgr::Instance().TeleportPlayerToEvent(player);
                break;
            case 2:
                ChatHandler(player->GetSession()).PSendSysMessage("%s", InvasionMgr::Instance().GetStatusText().c_str());
                break;
            default:
                break;
        }

        CloseGossipMenuFor(player);
        return true;
    }
};

struct npc_invasion_final_bossAI : public ScriptedAI
{
    explicit npc_invasion_final_bossAI(Creature* creature) : ScriptedAI(creature) { }

    void JustDied(Unit* killer) override
    {
        InvasionMgr::Instance().OnBossKilled(me, killer);
    }
};

class npc_invasion_final_boss : public CreatureScript
{
public:
    npc_invasion_final_boss() : CreatureScript("npc_invasion_final_boss") { }

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_invasion_final_bossAI(creature);
    }
};

class invasion_world_script : public WorldScript
{
public:
    invasion_world_script() : WorldScript("invasion_world_script") { }

    void OnUpdate(uint32 diff) override
    {
        InvasionMgr::Instance().Update(diff);
    }
};

void Addmod_invasionsScripts()
{
    new npc_invasion_event_master();
    new npc_invasion_portal();
    new npc_invasion_final_boss();
    new invasion_world_script();
}
