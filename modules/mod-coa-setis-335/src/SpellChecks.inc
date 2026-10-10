struct Check { uint32 id; std::array<uint32,3> effects, auras, triggers, radii; uint32 cast, duration, range, visual; int32 castMs, durationMs; };
constexpr std::array<Check, 12> CHECKS = {{
    { 2908801, {2, 0, 0}, {0, 0, 0}, {0, 0, 0}, {2881, 0, 0}, 2881, 0, 2881, 288004, 0, 0 },
    { 2908802, {2, 6, 0}, {0, 3, 0}, {0, 0, 0}, {2883, 2883, 0}, 2882, 2883, 2883, 288009, 4000, 30000 },
    { 2908803, {31, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, 2881, 0, 2882, 288012, 0, 0 },
    { 2908804, {6, 0, 0}, {23, 0, 0}, {2908801, 0, 0}, {0, 0, 0}, 2881, 2882, 2883, 288003, 0, 15000 },
    { 2908805, {2, 0, 0}, {0, 0, 0}, {0, 0, 0}, {2885, 0, 0}, 2881, 0, 2884, 288005, 0, 0 },
    { 2908806, {6, 6, 0}, {74, 74, 0}, {0, 0, 0}, {0, 0, 0}, 2881, 2885, 2883, 288001, 0, -1 },
    { 2908807, {6, 0, 0}, {79, 138, 0}, {0, 0, 0}, {0, 0, 0}, 2881, 2881, 2881, 288002, 0, 600000 },
    { 2908808, {31, 98, 6}, {0, 0, 192}, {0, 0, 0}, {2882, 2882, 2882}, 2881, 2882, 2881, 288007, 0, 15000 },
    { 2908809, {3, 6, 6}, {0, 87, 26}, {0, 0, 0}, {0, 0, 0}, 2883, 0, 2881, 288008, 15000, 0 },
    { 2908810, {2, 98, 0}, {0, 0, 0}, {0, 0, 0}, {2886, 2886, 0}, 2881, 0, 2883, 288006, 0, 0 },
    { 2908811, {3, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, 2882, 0, 2883, 288010, 4000, 0 },
    { 2908812, {2, 27, 0}, {0, 3, 0}, {0, 0, 0}, {2884, 2884, 0}, 2881, 2884, 2883, 288011, 0, 20000 }
}};
bool CheckSpells(bool verbose = false)
{
    auto bad = [verbose](uint32 id, char const* why)
    {
        if (verbose) LOG_WARN("server.loading", "COA880 Setis spell {}: {}", id, why);
        return false;
    };
    for (Check const& c : CHECKS)
    {
        SpellInfo const* s = sSpellMgr->GetSpellInfo(c.id);
        if (!s) return bad(c.id, "missing Spell.dbc row");
        if (!s->CastTimeEntry || s->CastTimeEntry->ID != c.cast || s->CastTimeEntry->CastTime != c.castMs)
            return bad(c.id, "SpellCastTimes mismatch");
        if ((s->DurationEntry ? s->DurationEntry->ID : 0) != c.duration
            || (s->DurationEntry ? s->DurationEntry->Duration[0] : 0) != c.durationMs)
            return bad(c.id, "SpellDuration mismatch");
        if (!s->RangeEntry || s->RangeEntry->ID != c.range) return bad(c.id, "SpellRange mismatch");
        if (s->SpellVisual[0] != c.visual || !sSpellVisualStore.LookupEntry(c.visual)) return bad(c.id, "SpellVisual mismatch");
        for (uint32 i = 0; i < 3; ++i)
        {
            if (uint32(s->Effects[i].Effect) != c.effects[i] || uint32(s->Effects[i].ApplyAuraName) != c.auras[i]
                || s->Effects[i].TriggerSpell != c.triggers[i]) return bad(c.id, "effect/aura/trigger mismatch");
            if ((s->Effects[i].RadiusEntry ? s->Effects[i].RadiusEntry->ID : 0) != c.radii[i])
                return bad(c.id, "SpellRadius mismatch");
        }
    }
    return true;
}
