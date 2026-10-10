/* GPL-2.0-or-later. Setis rotation adapted from jealous-sound COA,
 * boss_world_custom_coa.cpp @864c7607fcf3993e38e98cb4af34e7953578e308.
 * Target: Soricela a7cfafb479d7e12495f6eb2bb6ab16d34b5814d3.
 * Isolated spell IDs and map/entry gate. Rewards09 supplies corpse loot and weekly credit externally; native weekly instance lock. */
#include "Log.h"
#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "Config.h"
#include "Log.h"
#include "Map.h"
#include "Player.h"
#include "SpellMgr.h"
#include "SpellInfo.h"
#include "DBCStructure.h"
#include "DBCStores.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <limits>
#include "InstanceScript.h"
#include <sstream>
#include <string>

namespace
{
constexpr uint32 ENTRY = 9901881, MAP = 880;
enum Spells : uint32
{
    PLAGUE_EFFECT = 2908801, METEOR = 2908802, FIERCE = 2908803,
    PLAGUE = 2908804, STORM = 2908805, REFLECT = 2908806,
    FRENZY = 2908807, STOMP = 2908808, ARMAGEDDON = 2908809,
    ARMAGEDDON_BLAST = 2908810, LIGHTNING = 2908811, LIGHTNING_STRIKE = 2908812
};
constexpr std::array<uint32, 12> IDS = {PLAGUE_EFFECT, METEOR, FIERCE, PLAGUE,
    STORM, REFLECT, FRENZY, STOMP, ARMAGEDDON, ARMAGEDDON_BLAST, LIGHTNING, LIGHTNING_STRIKE};
constexpr UnitFlags PASSIVE_FLAGS = UnitFlags(UNIT_FLAG_NON_ATTACKABLE | UNIT_FLAG_IMMUNE_TO_PC | UNIT_FLAG_IMMUNE_TO_NPC);
bool enabled = false;
uint32 health = 750000;
float damageMultiplier = 0.75f;

#include "SpellChecks.inc"

bool WeeklyPeriodReady()
{
    MapDifficulty const* difficulty = GetMapDifficultyData(880, Difficulty(0));
    return difficulty && difficulty->resetTime == 604800 && difficulty->maxPlayers == 40;
}
constexpr uint32 DATA_COA880_VALID = 88005;
struct instance_coa880 : InstanceScript
{
    explicit instance_coa880(InstanceMap* map) : InstanceScript(map) { SetBossNumber(1); }
    void Initialize() override { InstanceScript::SetBossState(0, NOT_STARTED); }
    uint32 GetData(uint32 type) const override { return type == DATA_COA880_VALID && _valid ? 1 : 0; }
    std::string GetSaveData() override
    {
        if (!_valid) return _invalidData;
        return "COA880V1 " + std::to_string(uint32(GetBossState(0)));
    }
    void Load(char const* data) override
    {
        if (!data || !*data) return; // Existing empty saves: no retrospective kill credit.
        std::istringstream in(data);
        std::string header, extra;
        uint32 state = 0;
        if (!(in >> header >> state) || header != "COA880V1" || state > uint32(SPECIAL) || (in >> extra))
        {
            _valid = false;
            _invalidData = data;
            LOG_ERROR("scripts", "COA880 save invalid; instance {} is inactive; existing data preserved", instance->GetInstanceId());
            return;
        }
        // A process restart during combat is a wipe, not a victory.
        InstanceScript::SetBossState(0, state == uint32(DONE) ? DONE : NOT_STARTED);
    }
    bool SetBossState(uint32 id, EncounterState state) override
    {
        if (!_valid || id != 0 || GetBossState(0) == DONE) return false;
        return InstanceScript::SetBossState(id, state);
    }
    void OnCreatureCreate(Creature* creature) override
    {
        InstanceScript::OnCreatureCreate(creature);
        // Keep the fresh dead corpse for its normal loot lifetime.
        if (_valid && GetBossState(0) == DONE && creature->IsAlive()
            && (creature->GetEntry() == 9901881))
        {
            creature->SetUnitFlag(UNIT_FLAG_NON_ATTACKABLE | UNIT_FLAG_NOT_SELECTABLE);
            creature->SetReactState(REACT_PASSIVE);
            creature->DespawnOrUnsummon(Milliseconds(1));
        }
    }
private:
    bool _valid = true;
    std::string _invalidData;
};
bool InstanceReady(Creature* creature)
{
    InstanceMap* map = creature->GetMap()->ToInstanceMap();
    InstanceScript* script = map ? map->GetInstanceScript() : nullptr;
    return script && script->GetData(DATA_COA880_VALID) == 1 && script->GetBossState(0) != DONE;
}

bool Ready()
{
    return enabled && std::isfinite(damageMultiplier) && damageMultiplier > 0.0f
        && damageMultiplier <= 100.0f && health >= 1000 && health <= 100000000
        && CheckSpells() && WeeklyPeriodReady();
}

class Settings : public WorldScript
{
public:
    Settings() : WorldScript("coa880_setis_settings") { }
    void OnAfterConfigLoad(bool /*reload*/) override
    {
        enabled = sConfigMgr->GetOption<bool>("COA880.Enable", false);
        health = sConfigMgr->GetOption<uint32>("COA880.SetisHealth", 750000);
        damageMultiplier = sConfigMgr->GetOption<float>("COA880.DamageMultiplier", 0.75f);
    }
    void OnStartup() override
    {
        if (enabled) CheckSpells(true);
        if (Ready())
            LOG_INFO("server.loading", "COA880 Setis pilot 0.10.0: enabled; 12 isolated spells; weekly period ready; instance_coa880 required");
        else
            LOG_WARN("server.loading", "COA880 Setis pilot 0.10.0: inactive; check Enable, calibration, spells and MapDifficulty weekly period");
    }
};

enum Events : uint32 { E_PLAGUE = 1, E_FIERCE, E_STORM, E_STOMP, E_METEOR, E_LIGHTNING };

struct SetisAI : ScriptedAI
{
    explicit SetisAI(Creature* c) : ScriptedAI(c) { }
    EventMap events;
    bool active = false, frenzy = false, armageddon = false;

    void ClearPilotAuras()
    {
        me->RemoveAllDynObjects();
        for (auto const& ref : me->GetMap()->GetPlayers())
            if (Player* p = ref.GetSource())
                for (uint32 id : IDS)
                    p->RemoveAurasDueToSpell(id, me->GetGUID());
        for (uint32 id : IDS)
            me->RemoveAurasDueToSpell(id);
    }

    void Reset() override
    {
        events.Reset();
        frenzy = armageddon = false;
        ClearPilotAuras();
        active = me->GetEntry() == ENTRY && me->GetMapId() == MAP && Ready() && InstanceReady(me);
        if (active) me->GetInstanceScript()->SetBossState(0, NOT_STARTED);
        // Preserve COA immunity -294 without borrowing/modifying a world DB row.
        // Arcane school 0x40 plus the exact source mechanics mask.
        constexpr uint32 mechanicsMask = 1235123966;
        me->ApplySpellImmune(REFLECT, IMMUNITY_SCHOOL, 64, active);
        for (uint32 mechanic = 1; mechanic < 32; ++mechanic)
            if (mechanicsMask & (uint32(1) << mechanic))
                me->ApplySpellImmune(REFLECT, IMMUNITY_MECHANIC, mechanic, active);
        if (!active)
        {
            me->SetFaction(35);
            me->SetUnitFlag(PASSIVE_FLAGS);
            me->SetReactState(REACT_PASSIVE);
            return;
        }
        me->SetFaction(14);
        me->RemoveUnitFlag(PASSIVE_FLAGS);
        me->SetReactState(REACT_AGGRESSIVE);
        me->SetCreateHealth(health);
        me->SetStatFlatModifier(UNIT_MOD_HEALTH, BASE_VALUE, float(health));
        me->UpdateMaxHealth();
        me->SetFullHealth();
        DoCastSelf(REFLECT, true);
    }

    void JustEngagedWith(Unit* /*who*/) override
    {
        if (!active || !InstanceReady(me)) { EnterEvadeMode(); return; }
        me->GetInstanceScript()->SetBossState(0, IN_PROGRESS);
        events.ScheduleEvent(E_PLAGUE, Milliseconds(3800));
        events.ScheduleEvent(E_FIERCE, Milliseconds(6200));
        events.ScheduleEvent(E_STORM, Milliseconds(12000));
        events.ScheduleEvent(E_STOMP, Milliseconds(16300));
        events.ScheduleEvent(E_METEOR, Milliseconds(26400));
        events.ScheduleEvent(E_LIGHTNING, Milliseconds(30500));
    }

    Unit* RandomPlayer()
    {
        if (Unit* u = SelectTarget(SelectTargetMethod::Random, 1, 100.0f, true))
            return u;
        return me->GetVictim();
    }

    void OnSpellCast(SpellInfo const* spell) override
    {
        if (!active || !me->IsInCombat()) return;
        if (spell->Id == LIGHTNING)
        {
            for (auto const& ref : me->GetMap()->GetPlayers())
                if (Player* p = ref.GetSource())
                    if (p->IsAlive() && !p->IsGameMaster() && me->IsWithinDistInMap(p, 100.0f))
                        me->CastSpell(p->GetPositionX(), p->GetPositionY(), p->GetPositionZ(), LIGHTNING_STRIKE, true);
        }
        else if (spell->Id == ARMAGEDDON)
            DoCastAOE(ARMAGEDDON_BLAST, true);
    }

    void DamageDealt(Unit* /*victim*/, uint32& damage, DamageEffectType /*type*/, SpellSchoolMask /*school*/) override
    {
        if (active)
            damage = uint32(std::min(double(std::numeric_limits<uint32>::max()), double(damage) * damageMultiplier));
    }

    void DamageTaken(Unit* /*attacker*/, uint32& damage, DamageEffectType /*type*/, SpellSchoolMask /*school*/) override
    {
        if (!active) { damage = 0; return; }
        if (!frenzy && me->HealthBelowPctDamaged(30, damage))
        {
            frenzy = true;
            DoCastSelf(FRENZY, true);
        }
        if (!armageddon && me->HealthBelowPctDamaged(3, damage))
        {
            armageddon = true;
            me->InterruptNonMeleeSpells(false);
            events.Reset();
            // Pilot guard: the crossing hit cannot skip the final 15-second cast.
            uint32 floor = std::max(uint32(1), me->CountPctFromMaxHealth(3));
            if (me->GetHealth() > floor)
                damage = std::min(damage, me->GetHealth() - floor);
            else damage = 0;
            DoCastSelf(ARMAGEDDON);
        }
    }

    void JustDied(Unit* /*killer*/) override
    {
        events.Reset();
        ClearPilotAuras();
        if (active && InstanceReady(me) && me->GetInstanceScript()->SetBossState(0, DONE))
            me->GetMap()->ToInstanceMap()->PermBindAllPlayers();
        LOG_INFO("scripts", "COA880 Setis pilot: defeated in instance {}; Guardado10: native encounter saved; corpse loot and CoaWeekly retained", me->GetInstanceId());
    }

    void UpdateAI(uint32 diff) override
    {
        if (!active || !UpdateVictim()) return;
        events.Update(diff);
        if (me->HasUnitState(UNIT_STATE_CASTING)) return;
        while (uint32 id = events.ExecuteEvent())
        {
            switch (id)
            {
                case E_PLAGUE:
                    if (Unit* t = RandomPlayer()) me->CastSpell(t, PLAGUE, true);
                    events.Repeat(Milliseconds(5100)); break;
                case E_FIERCE: DoCastVictim(FIERCE); events.Repeat(Milliseconds(8800)); break;
                case E_STORM: DoCastSelf(STORM); events.Repeat(Milliseconds(13600)); break;
                case E_STOMP: DoCastSelf(STOMP); events.Repeat(Milliseconds(32000)); break;
                case E_METEOR:
                    if (Unit* t = RandomPlayer()) DoCast(t, METEOR);
                    events.Repeat(Milliseconds(32000)); break;
                case E_LIGHTNING:
                    if (Unit* t = RandomPlayer()) DoCast(t, LIGHTNING);
                    events.Repeat(Milliseconds(35100)); break;
            }
            if (me->HasUnitState(UNIT_STATE_CASTING)) break;
        }
        DoMeleeAttackIfReady();
    }
};

class SetisScript : public CreatureScript
{
public:
    SetisScript() : CreatureScript("boss_setis_coa880_pilot") { }
    CreatureAI* GetAI(Creature* c) const override { return new SetisAI(c); }
};
}

void AddCoaSetisScripts()
{
    RegisterInstanceScript(instance_coa880, 880);
    new Settings();
    new SetisScript();
}
