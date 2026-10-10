/*
 * COA 890 pilot adaptation. Source: jealous-sound/azerothcore-wotlk-coa
 * commit 864c7607fcf3993e38e98cb4af34e7953578e308.
 * Isolated encounter; no global spell edits, flex or loot hooks.
 * GPL-2.0-or-later, consistent with the AzerothCore source project.
 */
#include "Config.h"
#include "DBCStores.h"
#include "InstanceMapScript.h"
#include "InstanceScript.h"
#include "Containers.h"
#include "CreatureScript.h"
#include "Log.h"
#include "Map.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "SpellAuras.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "TemporarySummon.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <vector>
#include <sstream>
#include <string>

namespace
{
using namespace std::chrono_literals;
constexpr uint32 SPELL_FIERCE_BLOW = 975011;
constexpr std::array<uint32,28> RequiredSpells = {975011,2119801,2119802,2119806,2119807,2119810,2119811,2119816,2119817,2119819,2119820,2119825,2119826,2119828,2119830,2119831,2119832,2119836,2119837,2119839,2119840,2119844,2119845,2119847,2119848,2119849,2119853,2119857};
bool Ready()
{
    if (!sConfigMgr->GetOption<bool>("COA890.Enable", false)) return false;
    for (uint32 id : RequiredSpells)
        if (!sSpellMgr->GetSpellInfo(id)) return false;
    return sSpellMgr->GetSpellInfo(2119816)->GetDuration() == 100500
        && sSpellMgr->GetSpellInfo(2119837)->Effects[0].ApplyAuraName == SPELL_AURA_MOD_ATTACK_POWER;
}
bool WeeklyPeriodReady()
{
    MapDifficulty const* difficulty = GetMapDifficultyData(890, Difficulty(0));
    return difficulty && difficulty->resetTime == 604800 && difficulty->maxPlayers == 40;
}
constexpr uint32 DATA_COA890_VALID = 89005;
struct instance_coa890 : InstanceScript
{
    explicit instance_coa890(InstanceMap* map) : InstanceScript(map) { SetBossNumber(1); }
    void Initialize() override { InstanceScript::SetBossState(0, NOT_STARTED); }
    uint32 GetData(uint32 type) const override { return type == DATA_COA890_VALID && _valid ? 1 : 0; }
    std::string GetSaveData() override
    {
        if (!_valid) return _invalidData;
        return "COA890V1 " + std::to_string(uint32(GetBossState(0)));
    }
    void Load(char const* data) override
    {
        if (!data || !*data) return; // Existing empty saves: no retrospective kill credit.
        std::istringstream in(data);
        std::string header, extra;
        uint32 state = 0;
        if (!(in >> header >> state) || header != "COA890V1" || state > uint32(SPECIAL) || (in >> extra))
        {
            _valid = false;
            _invalidData = data;
            LOG_ERROR("scripts", "COA890 save invalid; instance {} is inactive; existing data preserved", instance->GetInstanceId());
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
            && (creature->GetEntry() == 990189001 || creature->GetEntry() == 990189002 || creature->GetEntry() == 990189005))
        {
            creature->SetUnitFlag(UNIT_FLAG_NON_ATTACKABLE | UNIT_FLAG_NOT_SELECTABLE);
            creature->SetReactState(REACT_PASSIVE);
            creature->DespawnOrUnsummon(1ms);
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
    return script && script->GetData(DATA_COA890_VALID) == 1 && script->GetBossState(0) != DONE;
}
float DamageFactor()
{
    return std::clamp(sConfigMgr->GetOption<float>("COA890.DamageMultiplier", 0.35f), 0.01f, 10.0f);
}
void ApplyHealth(Creature* me)
{
    uint32 health = 20000;
    switch (me->GetEntry())
    {
        case 990189001: health = sConfigMgr->GetOption<uint32>("COA890.AtalzulHealth",750000); break;
        case 990189002: health = sConfigMgr->GetOption<uint32>("COA890.ZulroggHealth",400000); break;
        case 990189005: health = sConfigMgr->GetOption<uint32>("COA890.VesselHealth",35000); break;
        default: health = sConfigMgr->GetOption<uint32>("COA890.AddHealth",20000); break;
    }
    health = std::clamp<uint32>(health,100,100000000);
    me->SetCreateHealth(health);
    me->SetStatFlatModifier(UNIT_MOD_HEALTH,BASE_VALUE,float(health));
    me->UpdateMaxHealth();
    me->SetFullHealth();
}
std::vector<Player*> Players(Creature* me, std::function<bool(Player*)> const& accept)
{
    std::vector<Player*> out;
    me->GetMap()->DoForAllPlayers([&](Player* p)
    {
        if (p->IsAlive() && !p->IsGameMaster() && accept(p)) out.push_back(p);
    });
    return out;
}
void CleanPlayers(Creature* me)
{
    me->GetMap()->DoForAllPlayers([](Player* p)
    {
        for (uint32 id : RequiredSpells) p->RemoveAurasDueToSpell(id);
    });
}
int32 Info(Unit const*, uint32 id)
{
    SpellInfo const* info = sSpellMgr->GetSpellInfo(id);
    return info ? info->Effects[0].BasePoints + 1 : 0;
}
void Hit(Unit* caster, Unit* target, uint32 spell, int32 amount)
{
    if (target && amount > 0) caster->CastCustomSpell(target,spell,&amount,nullptr,nullptr,true);
}
void AddStack(Unit* caster, Unit* target, uint32 spell, uint8 max = 100)
{
    if (Aura* aura = target->GetAura(spell,caster->GetGUID()))
    {
        if (aura->GetStackAmount() < max) aura->ModStackAmount(1);
        aura->RefreshDuration();
    }
    else caster->AddAura(spell,target);
}
struct custom_world_boss_coa : ScriptedAI
{
    explicit custom_world_boss_coa(Creature* c) : ScriptedAI(c) {}
    void Reset() override { events.Reset(); ApplyHealth(me); }
    void JustEngagedWith(Unit*) override { Schedule(); }
    virtual void Schedule() = 0;
    virtual void Execute(uint32 id) = 0;
    void DamageDealt(Unit* victim,uint32& damage,DamageEffectType type,SpellSchoolMask school) override
    {
        ScriptedAI::DamageDealt(victim,damage,type,school);
        damage = uint32(std::min<double>(double(damage)*DamageFactor(),double(UINT32_MAX)));
    }
    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim()) return;
        events.Update(diff);
        if (me->HasUnitState(UNIT_STATE_CASTING)) return;
        while (uint32 event = events.ExecuteEvent())
        {
            Execute(event);
            if (me->HasUnitState(UNIT_STATE_CASTING)) break;
        }
        DoMeleeAttackIfReady();
    }
    Unit* RandomPlayer(bool notTank = true)
    {
        if (Unit* u = SelectTarget(SelectTargetMethod::Random,notTank ? 1 : 0,100.0f,true)) return u;
        return me->GetVictim();
    }
    void CastAt(Position const& pos,uint32 spell)
    {
        me->CastSpell(pos.GetPositionX(),pos.GetPositionY(),pos.GetPositionZ(),spell,true);
    }
    EventMap events;
};
    enum AtalzulSpells
    {
        SPELL_SPECTRAL_BOLT     = 2119801,
        SPELL_SPECTRAL_BOLT_HIT = 2119802,
        SPELL_LAND_OF_THE_DEAD  = 2119806,
        SPELL_RAISE_DEAD        = 2119807,
        SPELL_CHOKING_CLOUD     = 2119810,
        SPELL_SPECTRAL_VEIL     = 2119816,
        SPELL_VEIL_TRIGGER      = 2119817,
        SPELL_SPECTRAL_BOMB     = 2119819,
        SPELL_SPECTRAL_BOMB_HIT = 2119820,
        SPELL_HAND_OF_SOULREAVER= 2119825,
        SPELL_TITANIC_CRUSH     = 2119826,
        SPELL_TITANIC_CRUSH_HIT = 2119828,
        SPELL_TRAUMA            = 2119830,
        SPELL_ENDLESS_HUNGER    = 2119831,
        SPELL_CONSUME_LIFE      = 2119832,
        SPELL_CONSUME_LIFE_HEAL = 2119836,
        SPELL_CONSUME_LIFE_AP   = 2119837,
        SPELL_SOUL_RELEASE      = 2119839,
        SPELL_SOUL_RELEASE_HIT  = 2119840,
        SPELL_SOUL_VESSEL       = 2119844,
        SPELL_SOUL_BIND         = 2119845,
        SPELL_SOUL_LINK         = 2119847,
        SPELL_SPECTRAL_VOLLEY   = 2119848,
        SPELL_VEIL_PULSE        = 2119853,
        SPELL_VEIL_POWER        = 2119857,

        NPC_ATALZUL             = 990189001,
        NPC_ZULROGG             = 990189002,
        NPC_DAMNED_WIGHT        = 990189003,
        NPC_RESTLESS_SPIRIT     = 990189004,
        NPC_SOUL_VESSEL         = 990189005,
    };

    enum AtalzulEvents { EA_BOLT = 1, EA_VOLLEY, EA_LAND };

    // Casts the Spectral Bomb where it appears and is gone 2s later; killing it
    // first stops the bomb.
    struct npc_coa890_spirit : public ScriptedAI
    {
        explicit npc_coa890_spirit(Creature* creature) : ScriptedAI(creature) { }

        void Reset() override
        {
            ApplyHealth(me);
            me->SetReactState(REACT_PASSIVE);
        }

        void IsSummonedBy(WorldObject* /*summoner*/) override { DoCastSelf(SPELL_SPECTRAL_BOMB); }

        void OnSpellCast(SpellInfo const* spell) override
        {
            if (spell->Id != SPELL_SPECTRAL_BOMB)
                return;
            Unit* caster = me;
            if (TempSummon* s = me->ToTempSummon())
                if (Unit* atal = s->GetSummonerUnit())
                    caster = atal;
            caster->CastSpell(me->GetPositionX(), me->GetPositionY(), me->GetPositionZ(), SPELL_SPECTRAL_BOMB_HIT, true);
            me->DespawnOrUnsummon(100ms);
        }

        void UpdateAI(uint32 /*diff*/) override { }
    };

    // The phylacteries: asleep until Spectral Veil binds one; its death breaks
    // the veil and releases the soul onto the raid.
    struct npc_coa890_vessel : public ScriptedAI
    {
        explicit npc_coa890_vessel(Creature* creature) : ScriptedAI(creature) { }

        void Reset() override
        {
            ApplyHealth(me);
            me->SetReactState(REACT_PASSIVE);
            me->SetControlled(true, UNIT_STATE_ROOT);
            DoCastSelf(SPELL_SOUL_VESSEL, true);
            Sleep(true);
        }

        void Sleep(bool asleep)
        {
            if (asleep)
                me->SetUnitFlag(UNIT_FLAG_NOT_SELECTABLE | UNIT_FLAG_NON_ATTACKABLE);
            else
                me->RemoveUnitFlag(UNIT_FLAG_NOT_SELECTABLE | UNIT_FLAG_NON_ATTACKABLE);
        }

        void JustDied(Unit* /*killer*/) override
        {
            for (Player* p : Players(me, [&](Player* x) { return me->IsWithinDistInMap(x, 100.0f); }))
            {
                me->CastSpell(p, SPELL_SOUL_RELEASE_HIT, true);
                AddStack(me, p, SPELL_SOUL_RELEASE);
            }
            if (Creature* atal = me->FindNearestCreature(NPC_ATALZUL, 200.0f))
                atal->AI()->DoAction(1);
        }

        void UpdateAI(uint32 /*diff*/) override { }
    };

    struct boss_coa890_atalzul : public custom_world_boss_coa
    {
        explicit boss_coa890_atalzul(Creature* creature) : custom_world_boss_coa(creature), _summons(creature) { }

        void Reset() override
        {
            if (InstanceScript* script = me->GetMap()->ToInstanceMap()->GetInstanceScript())
            {
                if (script->GetBossState(0) == DONE) { me->DespawnOrUnsummon(1ms); return; }
                script->SetBossState(0, NOT_STARTED);
            }
            custom_world_boss_coa::Reset();
            _summons.DespawnAll();
            _nextVeil = 80;
            _raise = 0;
            CleanPlayers(me);
            if (Creature* rogg = me->FindNearestCreature(NPC_ZULROGG, 200.0f))
                if (rogg->IsInCombat()) rogg->AI()->EnterEvadeMode();
            me->RemoveAurasDueToSpell(SPELL_VEIL_POWER);
            std::list<Creature*> vessels;
            me->GetCreatureListWithEntryInGrid(vessels, NPC_SOUL_VESSEL, 100.0f);
            for (Creature* v : vessels)
            {
                if (!v->IsAlive())
                    v->Respawn(true);
                else if (auto* ai = dynamic_cast<npc_coa890_vessel*>(v->AI()))
                {
                    v->SetFullHealth();
                    ai->Sleep(true);
                }
            }
        }

        void JustEngagedWith(Unit* who) override
        {
            if (InstanceScript* script = me->GetMap()->ToInstanceMap()->GetInstanceScript()) script->SetBossState(0, IN_PROGRESS);
            custom_world_boss_coa::JustEngagedWith(who);
            if (Creature* rogg = me->FindNearestCreature(NPC_ZULROGG, 100.0f))
                if (!rogg->IsInCombat())
                    rogg->AI()->AttackStart(who);
        }

        void JustSummoned(Creature* summon) override
        {
            _summons.Summon(summon);
            if (summon->GetEntry() == NPC_DAMNED_WIGHT)
                if (Unit* t = SelectTarget(SelectTargetMethod::Random, 0, 100.0f, true))
                    summon->AI()->AttackStart(t);
        }

        void JustDied(Unit* /*killer*/) override
        {
            if (InstanceScript* script = me->GetMap()->ToInstanceMap()->GetInstanceScript())
                if (script->SetBossState(0, DONE))
                    if (InstanceMap* map = me->GetMap()->ToInstanceMap())
                    {
                        map->PermBindAllPlayers();
                        LOG_INFO("scripts", "COA890 encounter DONE; instance {}; native raid binding applied", map->GetInstanceId());
                    }
            _summons.DespawnAll();
            CleanPlayers(me);
            if (Creature* rogg = me->FindNearestCreature(NPC_ZULROGG, 200.0f))
                rogg->DespawnOrUnsummon(10s);
        }

        void Schedule() override
        {
            events.ScheduleEvent(EA_BOLT, 700ms);
            events.ScheduleEvent(EA_VOLLEY, 8400ms);
            events.ScheduleEvent(EA_LAND, 11400ms);
        }

        void Execute(uint32 eventId) override
        {
            switch (eventId)
            {
                case EA_BOLT:   DoCastVictim(SPELL_SPECTRAL_BOLT);  events.Repeat(1500ms);  break;
                case EA_VOLLEY: DoCastVictim(SPELL_SPECTRAL_VOLLEY); events.Repeat(12s);    break;
                case EA_LAND:   DoCastSelf(SPELL_LAND_OF_THE_DEAD); events.Repeat(61400ms); break;
            }
        }

        void OnSpellCast(SpellInfo const* spell) override
        {
            if (spell->Id == SPELL_SPECTRAL_BOLT)
            {
                if (Unit* v = me->GetVictim())
                    me->CastSpell(v, SPELL_SPECTRAL_BOLT_HIT, true);
            }
            else if (spell->Id == SPELL_SPECTRAL_VEIL)
                BindVessel();
        }

        // Land of the Dead and Spectral Veil tick through their trigger spells.
        void SpellHit(Unit* /*caster*/, SpellInfo const* spell) override
        {
            if (spell->Id == SPELL_RAISE_DEAD)
            {
                Unit* t = RandomPlayer(false);
                if (!t)
                    return;
                // A wight every second; a choking cloud every other.
                me->SummonCreature(NPC_DAMNED_WIGHT, t->GetPositionX() + frand(-4.0f, 4.0f), t->GetPositionY() + frand(-4.0f, 4.0f),
                    t->GetPositionZ(), 0, TEMPSUMMON_CORPSE_TIMED_DESPAWN, 10000);
                if (_raise++ % 2 == 0)
                    CastAt(t->GetPosition(), SPELL_CHOKING_CLOUD);
            }
            else if (spell->Id == SPELL_VEIL_TRIGGER)
            {
                DoCastAOE(SPELL_VEIL_PULSE, true);
                AddStack(me, me, SPELL_VEIL_POWER);
                if (Unit* t = RandomPlayer(false))
                    me->SummonCreature(NPC_RESTLESS_SPIRIT, t->GetPositionX() + frand(-2.0f, 2.0f), t->GetPositionY() + frand(-2.0f, 2.0f),
                        t->GetPositionZ(), 0, TEMPSUMMON_TIMED_DESPAWN, 5000);
            }
        }

        void DamageTaken(Unit* attacker, uint32& damage, DamageEffectType type, SpellSchoolMask school) override
        {
            custom_world_boss_coa::DamageTaken(attacker, damage, type, school);
            if (me->HasAura(SPELL_SPECTRAL_VEIL)) { damage = 0; return; }
            if (_nextVeil && !me->HasAura(SPELL_SPECTRAL_VEIL) && me->HealthBelowPctDamaged(_nextVeil, damage))
            {
                uint32 floor = std::max<uint32>(1, uint32(uint64(me->GetMaxHealth()) * _nextVeil / 100));
                damage = std::min(damage, me->GetHealth() > floor ? me->GetHealth() - floor : 0);
                _nextVeil = _nextVeil > 20 ? _nextVeil - 20 : 0;
                me->InterruptNonMeleeSpells(false);
                DoCastSelf(SPELL_SPECTRAL_VEIL, true);
            }
        }

        void BindVessel()
        {
            std::list<Creature*> vessels;
            me->GetCreatureListWithEntryInGrid(vessels, NPC_SOUL_VESSEL, 100.0f);
            vessels.remove_if([](Creature* v) { return !v->IsAlive() || !v->HasUnitFlag(UNIT_FLAG_NOT_SELECTABLE); });
            if (vessels.empty())
                return;
            Creature* v = Acore::Containers::SelectRandomContainerElement(vessels);
            if (auto* ai = dynamic_cast<npc_coa890_vessel*>(v->AI()))
                ai->Sleep(false);
            v->CastSpell(me, SPELL_SOUL_BIND, true);
            v->CastSpell(me, SPELL_SOUL_LINK, true);
        }

        // A vessel died: the veil breaks.
        void DoAction(int32 action) override
        {
            if (action != 1 || !me->HasAura(SPELL_SPECTRAL_VEIL))
                return;
            me->InterruptNonMeleeSpells(false);
            me->RemoveAurasDueToSpell(SPELL_SPECTRAL_VEIL);
            me->RemoveAurasDueToSpell(SPELL_SOUL_LINK);
            me->RemoveAurasDueToSpell(SPELL_VEIL_POWER);
        }

    private:
        SummonList _summons;
        uint8 _nextVeil = 80;
        uint32 _raise = 0;
    };

    enum ZulroggEvents { EZ_CRUSH = 1, EZ_FIERCE };

    struct boss_coa890_zulrogg : public custom_world_boss_coa
    {
        explicit boss_coa890_zulrogg(Creature* creature) : custom_world_boss_coa(creature) { }

        void Reset() override
        {
            custom_world_boss_coa::Reset();
            DoCastSelf(SPELL_HAND_OF_SOULREAVER, true);
            DoCastSelf(SPELL_ENDLESS_HUNGER, true);
        }

        void JustEngagedWith(Unit* who) override
        {
            custom_world_boss_coa::JustEngagedWith(who);
            if (Creature* atal = me->FindNearestCreature(NPC_ATALZUL, 100.0f))
                if (!atal->IsInCombat())
                    atal->AI()->AttackStart(who);
        }

        void Schedule() override
        {
            events.ScheduleEvent(EZ_FIERCE, 5400ms);
            events.ScheduleEvent(EZ_CRUSH, 7200ms);
        }

        void Execute(uint32 eventId) override
        {
            switch (eventId)
            {
                case EZ_FIERCE: DoCastVictim(SPELL_FIERCE_BLOW);   events.Repeat(7600ms);  break;
                case EZ_CRUSH:  DoCastVictim(SPELL_TITANIC_CRUSH); events.Repeat(10800ms); break;
            }
        }

        void OnSpellCast(SpellInfo const* spell) override
        {
            if (spell->Id != SPELL_TITANIC_CRUSH)
                return;
            Unit* target = me->GetVictim();
            if (!target)
                return;
            for (Player* p : Players(me, [&](Player* x) { return target->IsWithinDistInMap(x, 5.0f); }))
                me->CastSpell(p, SPELL_TITANIC_CRUSH_HIT, true);
            me->CastSpell(target, SPELL_TRAUMA, true);
        }

        // Endless Hunger: every Consume Life that lands heals him, or at full
        // health raises his attack power.
        void SpellHitTarget(Unit* target, SpellInfo const* spell) override
        {
            if (spell->Id != SPELL_CONSUME_LIFE || !target->IsPlayer())
                return;
            if (me->IsFullHealth())
                DoCastSelf(SPELL_CONSUME_LIFE_AP, true);
            else
                Hit(me, me, SPELL_CONSUME_LIFE_HEAL, Info(me, SPELL_CONSUME_LIFE) - 1);
        }

        // Hand of the Soulreaver: he cannot die while Atal'zul is bound.
        void DamageTaken(Unit* attacker, uint32& damage, DamageEffectType type, SpellSchoolMask school) override
        {
            custom_world_boss_coa::DamageTaken(attacker, damage, type, school);
            if (damage >= me->GetHealth())
                if (Creature* atal = me->FindNearestCreature(NPC_ATALZUL, 200.0f))
                    if (atal->IsAlive())
                        damage = me->GetHealth() > 1 ? me->GetHealth() - 1 : 0;
        }
    };


struct npc_coa890_wight : custom_world_boss_coa
{
    explicit npc_coa890_wight(Creature* c) : custom_world_boss_coa(c) {}
    void Schedule() override { events.ScheduleEvent(1,8s); }
    void Execute(uint32) override { DoCastVictim(SPELL_FIERCE_BLOW); events.Repeat(8s); }
};
struct coa890_inactive : ScriptedAI
{
    explicit coa890_inactive(Creature* c) : ScriptedAI(c) {}
    void Reset() override
    {
        me->SetFaction(35);
        me->SetUnitFlag(UNIT_FLAG_NON_ATTACKABLE);
        me->SetReactState(REACT_PASSIVE);
    }
    void UpdateAI(uint32) override {}
};
template<class AI> class coa890_script : public CreatureScript
{
public:
    explicit coa890_script(char const* name) : CreatureScript(name) {}
    CreatureAI* GetAI(Creature* creature) const override
    {
        if (creature->GetMapId()!=890 || !Ready() || !WeeklyPeriodReady() || !InstanceReady(creature)) return new coa890_inactive(creature);
        return new AI(creature);
    }
};
class coa890_startup : public WorldScript
{
public:
    coa890_startup() : WorldScript("coa890_startup", { WORLDHOOK_ON_STARTUP }) {}
    void OnStartup() override
    {
        if (Ready() && WeeklyPeriodReady()) LOG_INFO("server.loading","COA890 combat pilot 0.5.0: enabled; 28 spells present; weekly period ready; instance script required");
        else LOG_WARN("server.loading","COA890 combat pilot 0.5.0: inactive (config, spells or weekly DBC period); NPCs remain friendly");
    }
};
}
void Addmod_coa_atalzul_335Scripts()
{
    RegisterInstanceScript(instance_coa890, 890);
    new coa890_startup();
    new coa890_script<boss_coa890_atalzul>("coa890_atalzul");
    new coa890_script<boss_coa890_zulrogg>("coa890_zulrogg");
    new coa890_script<npc_coa890_wight>("coa890_wight");
    new coa890_script<npc_coa890_spirit>("coa890_spirit");
    new coa890_script<npc_coa890_vessel>("coa890_vessel");
}
