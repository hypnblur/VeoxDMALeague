#include "ChampionCombo.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <unordered_set>

namespace ChampionCombo {
namespace {

std::string Upper(std::string value) {
    for (char& c : value) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return value;
}

int RetryForSlot(const Settings& s, int slot) {
    const int retries[4] = {s.retryQMs, s.retryWMs, s.retryEMs, s.retryRMs};
    return slot >= 0 && slot < 4 ? retries[slot] : 1000;
}

Action Position(const Query& q, const Settings& settings, int slot, float range,
                float delay, float speed, const std::string& label, int lockMs) {
    Action out{};
    const auto& spell = q.spells[slot];
    if (!q.retryReady[slot] || !spell.ready || q.distance > range) return out;
    out.valid = true;
    out.slot = slot;
    out.key = spell.key;
    out.castType = CastType::Position;
    out.range = range;
    out.delaySeconds = delay;
    out.projectileSpeed = speed;
    out.retryMs = RetryForSlot(settings, slot);
    out.lockMs = lockMs;
    out.label = label;
    return out;
}

Action Self(const Query& q, const Settings& settings, int slot,
            const std::string& label, int lockMs) {
    Action out{};
    const auto& spell = q.spells[slot];
    if (!q.retryReady[slot] || !spell.ready) return out;
    out.valid = true;
    out.slot = slot;
    out.key = spell.key;
    out.castType = CastType::Self;
    out.retryMs = RetryForSlot(settings, slot);
    out.lockMs = lockMs;
    out.label = label;
    return out;
}

Action Auxiliary(const Query& q, const Settings& settings, int slot,
                 float range, const std::string& label, int lockMs) {
    Action out{};
    const auto& spell = q.spells[slot];
    if (!q.hasAuxTarget || !q.retryReady[slot] || !spell.ready ||
        q.auxTargetDistance > range) return out;
    out.valid = true;
    out.slot = slot;
    out.key = spell.key;
    out.castType = CastType::Position;
    out.range = range;
    out.retryMs = RetryForSlot(settings, slot);
    out.lockMs = lockMs;
    out.useAuxTarget = true;
    out.label = label;
    return out;
}

bool UnsafeGenericName(const std::string& raw) {
    const std::string name = Upper(raw);
    if (name.empty()) return true;
    static const std::unordered_set<std::string> exact = {
        "YASUOW", "SAMIRAW", "FIORAW", "VLADIMIRSANGUINEPOOL",
        "XAYAHQ2", "LEBLANCWRETURN", "AHRIQRETURN", "AKALIE2",
        "ZEDW2", "ZEDR2", "SHACODECEIVE", "EZREALEARCANESHIFT"
    };
    if (exact.count(name)) return true;
    static const char* fragments[] = {
        "RETURN", "RECAST", "TOGGLE", "STANCE", "FORM", "DASH", "BLINK",
        "TELEPORT", "CHANNEL", "CANCEL", "ATTACK", "PASSIVE"
    };
    for (const char* fragment : fragments)
        if (name.find(fragment) != std::string::npos) return true;
    return false;
}

float EstimatedSpeed(const SpellState& spell) {
    if (spell.shape == Shape::Circle) return 0.0f;
    const int flightMs = spell.activeMs - spell.delayMs;
    if (flightMs > 120 && spell.range > 0.0f) {
        const float calculated = spell.range * 1000.0f / flightMs;
        return (std::max)(800.0f, (std::min)(calculated, 5000.0f));
    }
    return spell.shape == Shape::Cone ? 2000.0f : 1800.0f;
}

float PhysicalAfterArmor(float rawDamage, float armor) {
    if (!std::isfinite(rawDamage) || rawDamage <= 0.0f) return 0.0f;
    const float multiplier = armor >= 0.0f
        ? 100.0f / (100.0f + armor)
        : 2.0f - 100.0f / (100.0f - armor);
    return rawDamage * multiplier;
}

float EstimateZedRComboDamage(const Query& q) {
    if (q.spells[3].level <= 0 || q.targetHp <= 0.0f ||
        q.totalAttackDamage <= 0.0f) return 0.0f;

    float storedRaw = q.totalAttackDamage; // one reliable post-R basic attack
    if (q.spells[2].ready && q.spells[2].level > 0) {
        static const float eBase[5] = {70, 95, 120, 145, 170};
        const int level = (std::max)(1, (std::min)(q.spells[2].level, 5));
        storedRaw += eBase[level - 1] + 0.80f * q.bonusAttackDamage;
    }
    if (q.spells[0].ready && q.spells[0].level > 0) {
        static const float qBase[5] = {80, 120, 160, 200, 240};
        const int level = (std::max)(1, (std::min)(q.spells[0].level, 5));
        // Q can miss even after Death Mark. Count only one shuriken and apply
        // a conservative hit-confidence factor; shadow duplicates/items/passive
        // are deliberately omitted from the execute decision.
        storedRaw += (qBase[level - 1] + q.bonusAttackDamage) * 0.75f;
    }

    static const float markRatios[3] = {0.25f, 0.40f, 0.55f};
    const int rLevel = (std::max)(1, (std::min)(q.spells[3].level, 3));
    const float markRaw = q.totalAttackDamage +
        storedRaw * markRatios[rLevel - 1];
    // All components in this conservative model are physical damage. A final
    // confidence margin covers regeneration and small temporary shields.
    return PhysicalAfterArmor(storedRaw + markRaw, q.targetArmor) * 0.90f;
}

Action Generic(const Query& q, const Settings& settings) {
    if (!settings.genericEnabled || !q.hasSpellWindow) return {};

    // Q, W, E, then optionally R. Prefer spells with known positional shapes;
    // allow Unknown shape when DMA provided a plausible cast range (common when
    // shape metadata is unavailable).
    const int order[4] = {0, 1, 2, 3};
    for (int slot : order) {
        const auto& spell = q.spells[slot];
        if (slot == 3 && !settings.genericUltimate) continue;
        if (!spell.ready || !q.retryReady[slot] || UnsafeGenericName(spell.name)) continue;
        bool shapeOk = (spell.shape == Shape::Line || spell.shape == Shape::Circle ||
                        spell.shape == Shape::Cone);
        if (!shapeOk && spell.shape != Shape::Unknown) continue;
        if (!std::isfinite(spell.range) || spell.range < 250.0f || spell.range > 2500.0f)
            continue;
        if (q.distance > spell.range * 1.05f) continue;
        // Unknown-shape spells are cast conservatively (no prediction metadata).
        float delay = 0.f, speed = 0.f;
        if (shapeOk) {
            delay = (std::max)(0, spell.delayMs) / 1000.0f;
            speed = EstimatedSpeed(spell);
        }
        auto act = Position(q, settings, slot, spell.range, delay, speed,
                            "Generic " + (spell.name.empty() ? std::string(1, "QWER"[slot]) : spell.name), 320);
        if (act.valid) return act;
    }
    return {};
}

} // namespace

bool Supports(const std::string& champion) {
    // Unknown/new champions still receive the conservative generic profile.
    return !champion.empty();
}

bool IsSpecialProfile(const std::string& champion) {
    const std::string name = Upper(champion);
    return name == "KOGMAW" || name == "EZREAL" || name == "CAITLYN" ||
           name == "ASHE" ||
           name == "KALISTA" ||
           name == "MISSFORTUNE" || name == "DRAVEN" || name == "ZOE" ||
           name == "AHRI" || name == "CASSIOPEIA" || name == "LUX" ||
           name == "ZED" || name == "LUCIAN" || name == "YASUO" ||
           name == "VAYNE" || name == "AATROX" || name == "XERATH" ||
           name == "MORGANA" || name == "VARUS" || name == "JINX" ||
           name == "BRAND" || name == "VELKOZ" || name == "KARTHUS" ||
           name == "NILAH" || name == "SYNDRA" || name == "ZIGGS" ||
           name == "TRISTANA" || name == "SIVIR" || name == "SERAPHINE" ||
           name == "SWAIN" || name == "TWITCH" || name == "KAISA" ||
           name == "YUNARA";
}

float ScanRange(const std::string& champion, const Settings& settings) {
    const std::string name = Upper(champion);
    if (name == "KOGMAW") return 1850.0f;
    if (name == "EZREAL") return settings.ezrealR ? settings.ezrealRMaxRange : 1200.0f;
    if (name == "CAITLYN") return settings.caitlynR ? settings.caitlynRMaxRange : 1300.0f;
    if (name == "ASHE") return settings.asheR ? settings.asheRMaxRange : 1200.0f;
    if (name == "KALISTA") return 1150.0f;
    if (name == "MISSFORTUNE") return settings.missFortuneR ? 1400.0f : 1000.0f;
    if (name == "DRAVEN") return settings.dravenR ? settings.dravenRMaxRange : 1050.0f;
    if (name == "ZOE") return 1175.0f;
    if (name == "AHRI") return 1000.0f;
    if (name == "CASSIOPEIA") return 850.0f;
    if (name == "LUX") return settings.luxR ? settings.luxRMaxRange : 1100.0f;
    if (name == "ZED") return 950.0f;
    if (name == "LUCIAN") return 900.0f;
    if (name == "YASUO") return 1400.0f;
    if (name == "VAYNE") return 650.0f;
    if (name == "AATROX") return 800.0f;
    if (name == "XERATH") return 1400.0f;
    if (name == "MORGANA") return 1250.0f;
    if (name == "VARUS") return 1450.0f;
    if (name == "JINX") return settings.jinxR ? 2500.0f : 1450.0f;
    if (name == "BRAND") return 1050.0f;
    if (name == "VELKOZ") return 1050.0f;
    if (name == "KARTHUS") return 875.0f;
    if (name == "NILAH") return 650.0f;
    if (name == "SYNDRA") return 800.0f;
    if (name == "ZIGGS") return settings.ziggsR ? settings.ziggsRMaxRange : 900.0f;
    if (name == "TRISTANA") return 650.0f;
    if (name == "SIVIR") return 1200.0f;
    if (name == "SERAPHINE") return 1300.0f;
    if (name == "SWAIN") return 850.0f;
    if (name == "TWITCH") return 1200.0f;
    if (name == "KAISA") return 1800.0f;
    if (name == "YUNARA") return 1150.0f;
    return settings.genericUltimate ? 1800.0f : 1600.0f;
}

float HarassRange(const std::string& champion) {
    const std::string name = Upper(champion);
    if (name == "KOGMAW") return 1175.0f;
    if (name == "EZREAL") return 1200.0f;
    if (name == "CAITLYN") return 1300.0f;
    if (name == "ASHE") return 1200.0f;
    if (name == "KALISTA") return 1150.0f;
    if (name == "MISSFORTUNE") return 1000.0f;
    if (name == "DRAVEN") return 1050.0f;
    // Zoe's star travels far, but a Q1 fired at maximum reach is rarely worth
    // dropping the wave for, so the harass window uses her practical range.
    if (name == "ZOE") return 1175.0f;
    if (name == "AHRI") return 1000.0f;
    if (name == "CASSIOPEIA") return 850.0f;
    if (name == "LUX") return 1175.0f;
    if (name == "ZED") return 925.0f;
    if (name == "LUCIAN") return 900.0f;
    if (name == "YASUO") return 1150.0f;
    if (name == "VAYNE") return 650.0f;
    if (name == "AATROX") return 800.0f;
    if (name == "XERATH") return 1400.0f;
    if (name == "MORGANA") return 1250.0f;
    if (name == "VARUS") return 1450.0f;
    if (name == "JINX") return 1450.0f;
    if (name == "BRAND") return 1050.0f;
    if (name == "VELKOZ") return 1050.0f;
    if (name == "KARTHUS") return 875.0f;
    if (name == "NILAH") return 600.0f;
    if (name == "SYNDRA") return 800.0f;
    if (name == "ZIGGS") return 900.0f;
    if (name == "TRISTANA") return 625.0f;
    if (name == "SIVIR") return 1200.0f;
    if (name == "SERAPHINE") return 1300.0f;
    if (name == "SWAIN") return 850.0f;
    if (name == "TWITCH") return 950.0f;
    if (name == "KAISA") return 1500.0f;
    if (name == "YUNARA") return 1150.0f;
    // Generic profiles cap their skillshots at 1800 but have no verified
    // targeting model, so they commit to harass only at a close distance.
    return 1000.0f;
}

bool AcceptTarget(const std::string& champion, float distance,
                  float hpPct, const Settings& settings) {
    if (distance > ScanRange(champion, settings)) return false;
    const std::string name = Upper(champion);
    // A healthy remote target must not steal Ezreal's target from Q/W range.
    if (name == "EZREAL" && distance > 1200.0f && hpPct > settings.ezrealRHpPct)
        return false;
    if (name == "CAITLYN" && distance > 1300.0f && hpPct > settings.caitlynRHpPct)
        return false;
    if (name == "ASHE" && distance > 1200.0f && hpPct > settings.asheRHpPct)
        return false;
    if (name == "MISSFORTUNE" && distance > 1000.0f &&
        hpPct > settings.missFortuneRHpPct)
        return false;
    if (name == "DRAVEN" && distance > 1050.0f && hpPct > settings.dravenRHpPct)
        return false;
    if (name == "LUX" && distance > 1100.0f && hpPct > settings.luxRHpPct)
        return false;
    if (name == "JINX" && distance > 1450.0f && hpPct > settings.jinxRHpPct)
        return false;
    if (name == "ZIGGS" && distance > 900.0f && hpPct > settings.ziggsRHpPct)
        return false;
    return true;
}

float TargetScoreBonus(const std::string& champion, float distance) {
    const std::string name = Upper(champion);
    if (name == "EZREAL" && distance <= 1200.0f) return 1.25f;
    if (name == "CAITLYN" && distance <= 1300.0f) return 1.25f;
    if (name == "ASHE" && distance <= 1200.0f) return 1.25f;
    if (name == "DRAVEN" && distance <= 1050.0f) return 1.25f;
    if (name == "AHRI" && distance <= 1000.0f) return 1.25f;
    if (name == "CASSIOPEIA" && distance <= 700.0f) return 1.40f;
    if (name == "LUX" && distance <= 1100.0f) return 1.25f;
    if (name == "ZED" && distance <= 950.0f) return 1.35f;
    if (name == "LUCIAN" && distance <= 900.0f) return 1.30f;
    if (name == "YASUO" && distance <= 1150.0f) return 1.35f;
    if (name == "VAYNE" && distance <= 650.0f) return 1.35f;
    if (name == "AATROX" && distance <= 800.0f) return 1.30f;
    if (name == "XERATH" && distance <= 1400.0f) return 1.25f;
    if (name == "MORGANA" && distance <= 1250.0f) return 1.25f;
    if (name == "VARUS" && distance <= 1450.0f) return 1.25f;
    if (name == "JINX" && distance <= 1450.0f) return 1.25f;
    if (name == "BRAND" && distance <= 1050.0f) return 1.25f;
    if (name == "VELKOZ" && distance <= 1050.0f) return 1.25f;
    if (name == "KARTHUS" && distance <= 875.0f) return 1.30f;
    if (name == "NILAH" && distance <= 600.0f) return 1.35f;
    if (name == "SYNDRA" && distance <= 800.0f) return 1.30f;
    if (name == "ZIGGS" && distance <= 900.0f) return 1.25f;
    if (name == "TRISTANA" && distance <= 650.0f) return 1.30f;
    if (name == "SIVIR" && distance <= 1200.0f) return 1.25f;
    if (name == "SERAPHINE" && distance <= 1300.0f) return 1.25f;
    if (name == "SWAIN" && distance <= 850.0f) return 1.30f;
    if (name == "TWITCH" && distance <= 950.0f) return 1.25f;
    if (name == "KAISA" && distance <= 1500.0f) return 1.20f;
    if (name == "YUNARA" && distance <= 1150.0f) return 1.25f;
    return 0.0f;
}

Action Choose(const Query& q, const Settings& settings) {
    const std::string champion = Upper(q.champion);
    const auto& Q = q.spells[0];
    const auto& W = q.spells[1];
    const auto& E = q.spells[2];
    const auto& R = q.spells[3];

    if (champion == "KOGMAW") {
        const int wLevel = (std::max)(1, (std::min)(W.level, 5));
        const float wRange = q.localAttackRange + 110.0f + 20.0f * wLevel + 100.0f;
        if (q.targetIsMinion) {
            // V lane-clear: Bio-Arcane Barrage is the sustainable push tool and
            // costs no cast time, so it stays on the normal AA guard. Q is a
            // single-target line and is throttled; E/R are kept for champions.
            if (settings.kogW && W.ready && q.retryReady[1] && q.distance <= wRange &&
                q.hasAttackTarget && q.aaGapMs >= 55)
                return Self(q, settings, 1, "KogMaw W LaneClear", 45);
            if (q.hasSpellWindow && settings.kogQ && Q.ready) {
                auto action = Position(q, settings, 0, 1175.0f, 0.25f, 1650.0f,
                                       "KogMaw Q LaneClear", 260);
                if (action.valid) {
                    action.retryMs = 900;
                    return action;
                }
            }
            return {};
        }
        if (settings.kogW && W.ready && q.retryReady[1] && q.distance <= wRange &&
            (!q.hasAttackTarget || q.aaGapMs >= 55))
            return Self(q, settings, 1, "KogMaw W", 45);
        if (q.hasSpellWindow && settings.kogQ && Q.ready) {
            auto action = Position(q, settings, 0, 1175.0f, 0.25f, 1650.0f, "KogMaw Q", 260);
            if (action.valid) {
                // Caustic Spittle stops on the first enemy hit, so the wave
                // blocks it. Missile width is 70 in the Evade database.
                action.collisionRadius = 35.0f;
                action.collisionHits = 1;
                action.maxLead = 260.0f;
                action.requireStableMotion = true;
                return action;
            }
        }
        if (q.hasSpellWindow && settings.kogE && E.ready) {
            auto action = Position(q, settings, 2, 1200.0f, 0.25f, 1400.0f, "KogMaw E", 260);
            if (action.valid) return action;
        }
        if (q.hasSpellWindow && settings.kogR && R.ready && R.level > 0 &&
            q.targetHpPct > 0.0f && q.targetHpPct <= settings.kogRHpPct) {
            const float range = 1050.0f + 250.0f * (std::max)(1, (std::min)(R.level, 3));
            return Position(q, settings, 3, range, 0.85f, 0.0f, "KogMaw R", 280);
        }
        return {};
    }

    if (champion == "EZREAL" && q.hasSpellWindow) {
        if (q.targetIsMinion) {
            // V lane-clear: W does not affect lane minions and E/R are unsafe or
            // wasteful here. Weave Q only when it cannot delay the next AA.
            const bool aaSafeForQ = !q.hasAttackTarget || q.aaGapMs >= 380;
            if (settings.ezrealQ && Q.ready && aaSafeForQ) {
                auto action = Position(q, settings, 0, 1150.0f, 0.25f, 2000.0f,
                                       q.targetIsSecureMinion
                                           ? "Ezreal Q LastHit"
                                           : "Ezreal Q LaneClear", 260);
                if (action.valid) {
                    if (q.targetIsSecureMinion) {
                        // Securing one specific creep: a shot that stops on the
                        // minion in front loses the gold, so the line must be
                        // clear. Retry quickly, the window is short.
                        action.collisionRadius = 30.0f;
                        action.collisionHits = 1;
                        // 200 was shorter than the delay before the game's own
                        // cooldown flag reflects the cast, so the slot still
                        // read ready and Q was pressed a second time at the
                        // same creep. Stay above that refresh window.
                        action.retryMs = 450;
                    } else {
                        // Pushing the wave: stopping on the front minion still
                        // deals damage and still stacks Rising Spell Force, so
                        // holding the shot would only lose tempo.
                        action.retryMs = 500;
                    }
                    return action;
                }
            }
            return {};
        }
        // Preserve every available AA. W is useful only when Q can proc it or
        // an in-range AA has enough time to do so before the next attack.
        const bool aaSafeForW = !q.hasAttackTarget || q.aaGapMs >= 700;
        const bool canProcW = (settings.ezrealQ && Q.ready && q.retryReady[0]) ||
            (q.hasAttackTarget && q.aaGapMs >= 700);
        // 1150 is the cast range. The 1200 previously used here is the missile's
        // travel distance from the Evade database; aiming past 1150 made the
        // game clamp the cast and drop the shot short of the target.
        if (settings.ezrealW && W.ready && aaSafeForW && canProcW) {
            auto action = Position(q, settings, 1, 1150.0f, 0.25f, 1700.0f, "Ezreal W", 260);
            if (action.valid) {
                action.maxLead = 220.0f;
                action.requireStableMotion = true;
                return action;
            }
        }
        const bool aaSafeForQ = !q.hasAttackTarget || q.aaGapMs >= 380;
        if (settings.ezrealQ && Q.ready && aaSafeForQ) {
            auto action = Position(q, settings, 0, 1150.0f, 0.25f, 2000.0f, "Ezreal Q", 260);
            if (action.valid) {
                // At 1000 units the intercept horizon is ~0.78 s, which at
                // normal move speed leads over 300 units while the missile is
                // only 30 units wide. Cap the lead and demand a consistent
                // path, the same protection Zed's long-range Q already had.
                action.maxLead = 260.0f;
                action.requireStableMotion = true;
                action.collisionRadius = 30.0f;
                action.collisionHits = 1;
                return action;
            }
        }
        if (settings.ezrealR && R.ready && R.level > 0 && q.targetHpPct > 0.0f &&
            q.targetHpPct <= settings.ezrealRHpPct && q.distance >= 1000.0f &&
            !q.hasAttackTarget) {
            auto action = Position(q, settings, 3, settings.ezrealRMaxRange,
                                   1.0f, 2000.0f, "Ezreal R", 1050);
            if (action.valid) {
                // Trueshot Barrage spends 1.025 s before launch, so every legal
                // cast needs the full intercept horizon rather than a capped
                // lead. The stability gate is what keeps that honest.
                action.requireStableMotion = true;
            }
            return action;
        }
        return {};
    }

    if (champion == "CAITLYN" && q.hasSpellWindow) {
        if (q.targetIsMinion) {
            // V lane-clear: Piercing Shot travels through the entire wave, so
            // it is the only spell worth spending here. The trap, the net and
            // Ace in the Hole all stay reserved for champions.
            const bool aaSafeForQ = !q.hasAttackTarget || q.aaGapMs >= 700;
            if (settings.caitlynQ && Q.ready && aaSafeForQ) {
                auto action = Position(q, settings, 0, 1300.0f, 0.625f, 2200.0f,
                                       "Caitlyn Q LaneClear", 650);
                if (action.valid) {
                    action.retryMs = 1200;
                    return action;
                }
            }
            return {};
        }
        // Q has a long cast animation, so do not trade an available headshot/AA
        // for it. Outside AA range it remains the primary poke spell.
        const bool aaSafeForQ = !q.hasAttackTarget || q.aaGapMs >= 700;
        if (settings.caitlynQ && Q.ready && aaSafeForQ) {
            auto action = Position(q, settings, 0, 1300.0f, 0.625f, 2200.0f,
                                   "Caitlyn Q", 650);
            if (action.valid) {
                // The longest horizon in the profile set: 0.65 s of cast time
                // plus over half a second of flight at maximum range leads more
                // than 470 units for a 60-unit-wide shot. Piercing Shot goes
                // through minions, so only the lead needs constraining.
                action.maxLead = 260.0f;
                action.requireStableMotion = true;
                return action;
            }
        }
        // Trap is deliberately low-frequency. Without a verified CC/buff
        // reader it is used only when a full AA cycle leaves enough room.
        if (settings.caitlynW && W.ready &&
            (!q.hasAttackTarget || q.aaGapMs >= 900)) {
            auto action = Position(q, settings, 1, 800.0f, 0.80f, 0.0f,
                                   "Caitlyn W", 220);
            if (action.valid) {
                action.retryMs = (std::max)(action.retryMs, 3000);
                return action;
            }
        }
        // E moves Caitlyn backwards. It stays opt-in until the destination can
        // be checked against walls, enemy range and active Evade hazards.
        if (settings.caitlynE && E.ready && !q.hasAttackTarget &&
            q.distance <= 700.0f) {
            auto action = Position(q, settings, 2, 700.0f, 0.15f, 1600.0f,
                                   "Caitlyn E (opt-in)", 300);
            if (action.valid) return action;
        }
        if (settings.caitlynR && R.ready && R.level > 0 && !q.hasAttackTarget &&
            q.distance >= 1000.0f && q.distance <= settings.caitlynRMaxRange &&
            q.targetHpPct > 0.0f && q.targetHpPct <= settings.caitlynRHpPct) {
            auto action = Position(q, settings, 3, settings.caitlynRMaxRange,
                                   0.0f, 0.0f, "Caitlyn R", 1200);
            if (action.valid) action.retryMs = (std::max)(action.retryMs, 5000);
            return action;
        }
        return {};
    }

    if (champion == "ASHE") {
        // Ranger's Focus becomes castable only after the required passive
        // stacks. Use it as an AA steroid, then keep a local lock for its buff
        // duration so a stale ready flag cannot generate repeated Q presses.
        if (settings.asheQ && Q.ready && q.retryReady[0] && q.hasAttackTarget &&
            q.aaGapMs >= 55) {
            auto action = Self(q, settings, 0, "Ashe Q", 45);
            if (action.valid) action.retryMs = 4500;
            return action;
        }
        if (q.hasSpellWindow && settings.asheW && W.ready) {
            auto action = Position(q, settings, 1, 1200.0f, 0.25f, 2000.0f,
                                   q.targetIsMinion ? "Ashe W LaneClear" : "Ashe W", 260);
            if (action.valid) return action;
        }
        // Hawkshot is utility and needs fog/objective intent, so E remains off.
        if (!q.targetIsMinion && q.hasSpellWindow && settings.asheR && R.ready &&
            R.level > 0 && !q.hasAttackTarget && q.distance >= 800.0f &&
            q.distance <= settings.asheRMaxRange && q.targetHpPct > 0.0f &&
            q.targetHpPct <= settings.asheRHpPct) {
            auto action = Position(q, settings, 3, settings.asheRMaxRange,
                                   0.25f, 1600.0f, "Ashe R", 500);
            if (action.valid) {
                // Over two seconds of flight at maximum range: only commit
                // while the target is holding a consistent path.
                action.requireStableMotion = true;
                action.retryMs = (std::max)(action.retryMs, 5000);
            }
            return action;
        }
        return {};
    }

    if (champion == "KALISTA" && q.hasSpellWindow) {
        if (settings.kalistaQ && Q.ready) {
            auto action = Position(q, settings, 0, 1150.0f, 0.25f, 2400.0f, "Kalista Q", 260);
            if (action.valid) {
                // Pierce stops on the first enemy hit. At 40 units it is the
                // thinnest missile in the profile set, so it gets the tightest
                // lead cap as well.
                action.collisionRadius = 20.0f;
                action.collisionHits = 1;
                action.maxLead = 240.0f;
                action.requireStableMotion = true;
                return action;
            }
        }
        if (settings.kalistaE && E.ready && q.retryReady[2] &&
            q.kalistaRendStacks > 0 &&
            q.kalistaRendKillable)
            return Self(q, settings, 2, "Kalista E Execute", 80);
        return {};
    }

    if (champion == "AHRI") {
        if (q.targetIsMinion) {
            // V lane-clear uses Q only. Charm cannot pass through minions and
            // W target selection is not deterministic enough for farming.
            const bool aaSafeForQ = !q.hasAttackTarget || q.aaGapMs >= 500;
            if (q.hasSpellWindow && settings.ahriQ && Q.ready && aaSafeForQ) {
                auto action = Position(q, settings, 0, 970.0f, 0.25f, 1550.0f,
                                       "Ahri Q LaneClear", 280);
                if (action.valid) {
                    action.retryMs = 700;
                    return action;
                }
            }
            return {};
        }

        // Charm first so Q/W can benefit from the controlled target. Both
        // skillshots are scheduled only in a real AA backswing window.
        const bool aaSafeForSkillshot = !q.hasAttackTarget || q.aaGapMs >= 500;
        if (q.hasSpellWindow && settings.ahriE && E.ready && aaSafeForSkillshot) {
            auto action = Position(q, settings, 2, 975.0f, 0.25f, 1550.0f,
                                   "Ahri E", 280);
            if (action.valid) {
                action.collisionRadius = 50.0f; // Charm is 100 units wide.
                action.collisionHits = 1;
                // Charm is the most expensive spell in this profile to waste.
                // Keep its lead short: a shot that needs 280 units of guessing
                // is a coin flip, and losing it costs a 12 s cooldown.
                action.maxLead = 200.0f;
                action.requireStableMotion = true;
                return action;
            }
        }
        if (settings.ahriW && W.ready && q.retryReady[1] &&
            q.distance <= 700.0f && (!q.hasAttackTarget || q.aaGapMs >= 55)) {
            auto action = Self(q, settings, 1, "Ahri W", 45);
            if (action.valid) action.retryMs = 1200;
            return action;
        }
        if (q.hasSpellWindow && settings.ahriQ && Q.ready && aaSafeForSkillshot) {
            auto action = Position(q, settings, 0, 970.0f, 0.25f, 1550.0f,
                                   "Ahri Q", 280);
            if (action.valid) {
                // Orb of Deception passes through everything and returns, so
                // it needs no collision test, only a bounded lead.
                action.maxLead = 300.0f;
                action.requireStableMotion = true;
                return action;
            }
        }
        // R intentionally remains manual: an aim point alone is insufficient
        // to prove that the dash destination is outside walls/turret danger.
        return {};
    }

    if (champion == "CASSIOPEIA") {
        if (q.targetIsMinion) {
            // V lane-clear: Twin Fang is the primary farming weave. Preserve a
            // ready AA, then cast E throughout the remaining attack period.
            const bool aaSafeForE = !q.hasAttackTarget || q.aaGapMs >= 140;
            if (settings.cassiopeiaE && E.ready && q.retryReady[2] &&
                q.distance <= 700.0f && aaSafeForE) {
                auto action = Position(q, settings, 2, 700.0f, 0.0f, 0.0f,
                                       "Cassiopeia E LaneClear", 0);
                if (action.valid) action.retryMs = 560;
                return action;
            }
            // Use Q during E cooldown rather than allowing it to delay E.
            if (q.hasSpellWindow && settings.cassiopeiaQ && Q.ready) {
                auto action = Position(q, settings, 0, 850.0f, 0.75f, 0.0f,
                                       "Cassiopeia Q LaneClear", 260);
                if (action.valid) {
                    action.retryMs = 850;
                    return action;
                }
            }
            return {};
        }

        // Twin Fang is targeted and is Cassiopeia's main DPS loop. A short
        // pre-AA guard prevents it from starving a ready auto attack, but E is
        // otherwise always preferred over Q/W so it fires on every cooldown.
        const bool aaSafeForE = !q.hasAttackTarget || q.aaGapMs >= 140;
        if (settings.cassiopeiaE && E.ready && q.retryReady[2] &&
            q.distance <= 700.0f && aaSafeForE) {
            auto action = Position(q, settings, 2, 700.0f, 0.0f, 0.0f,
                                   "Cassiopeia E", 0);
            if (action.valid) action.retryMs = 560;
            return action;
        }

        // Establish/refresh poison while E is cooling down. Q is
        // ground-targeted, so projectile speed 0 uses its fixed delay.
        if (q.hasSpellWindow && settings.cassiopeiaQ && Q.ready) {
            auto action = Position(q, settings, 0, 850.0f, 0.75f, 0.0f,
                                   "Cassiopeia Q", 260);
            if (action.valid) return action;
        }

        // Miasma has a minimum placement distance; never cast it underneath a
        // very close target where the game would clamp the aim unexpectedly.
        if (q.hasSpellWindow && settings.cassiopeiaW && W.ready &&
            q.distance >= 500.0f && q.distance <= 750.0f) {
            auto action = Position(q, settings, 1, 750.0f, 0.25f, 2500.0f,
                                   "Cassiopeia W", 260);
            if (action.valid) {
                action.retryMs = 2000;
                return action;
            }
        }

        // R depends on enemy facing direction; damage-only casting is not worth
        // spending it blindly, so it stays manual even when configured on.
        return {};
    }

    if (champion == "LUX") {
        const std::string eName = Upper(E.name);
        const bool eRecast = eName.find("DETONATE") != std::string::npos ||
            eName.find("TOGGLE") != std::string::npos ||
            eName.find("LUXE2") != std::string::npos;

        if (eRecast && settings.luxE && E.ready && q.retryReady[2]) {
            auto action = Self(q, settings, 2, "Lux E Detonate", 45);
            if (action.valid) action.retryMs = 3500;
            return action;
        }

        if (q.targetIsMinion) {
            // E covers the wave around the selected minion. Q is preserved for
            // champions because spending a two-target bind on lane clear is
            // usually poor mana/control usage.
            if (q.hasSpellWindow && settings.luxE && E.ready && !eRecast) {
                auto action = Position(q, settings, 2, 1100.0f, 0.25f, 1200.0f,
                                       "Lux E Zone LaneClear", 260);
                if (action.valid) {
                    action.retryMs = 650;
                    return action;
                }
            }
            return {};
        }

        const bool aaSafeForSpell = !q.hasAttackTarget || q.aaGapMs >= 500;
        if (q.hasSpellWindow && settings.luxQ && Q.ready && aaSafeForSpell) {
            auto action = Position(q, settings, 0, 1175.0f, 0.25f, 1200.0f,
                                   "Lux Q", 280);
            if (action.valid) {
                // Light Binding is 80 units wide and passes through one unit,
                // so it is held only when two blockers consume both hits.
                action.collisionRadius = 40.0f;
                action.collisionHits = 2;
                action.maxLead = 260.0f;
                action.requireStableMotion = true;
                return action;
            }
        }
        if (q.hasSpellWindow && settings.luxE && E.ready && !eRecast &&
            aaSafeForSpell) {
            auto action = Position(q, settings, 2, 1100.0f, 0.25f, 1200.0f,
                                   "Lux E Zone", 260);
            if (action.valid) {
                action.retryMs = 650;
                return action;
            }
        }
        // W needs ally/self-threat selection rather than an enemy aim point.
        // Keep it manual until that defensive context is available.
        if (q.hasSpellWindow && settings.luxR && R.ready && R.level > 0 &&
            q.retryReady[3] && !q.hasAttackTarget && q.distance >= 700.0f &&
            q.distance <= settings.luxRMaxRange && q.targetHpPct > 0.0f &&
            q.targetHpPct <= settings.luxRHpPct) {
            auto action = Position(q, settings, 3, settings.luxRMaxRange,
                                   1.0f, 0.0f, "Lux R", 1050);
            if (action.valid) action.retryMs = 5000;
            return action;
        }
        return {};
    }

    if (champion == "ZED") {
        const std::string wName = Upper(W.name);
        const std::string rName = Upper(R.name);
        const bool wRecast = wName.find("ZEDW2") != std::string::npos ||
            wName.find("RECAST") != std::string::npos;
        const bool rRecast = rName.find("ZEDR2") != std::string::npos ||
            rName.find("RECAST") != std::string::npos;

        if (q.targetIsMinion) {
            // V lane-clear: do not spend Living Shadow or Death Mark on a
            // minion. Q is allowed in a real AA backswing; E is used only when
            // the selected minion is already in the local slash radius.
            if (q.hasSpellWindow && settings.zedE && E.ready &&
                q.retryReady[2] && q.distance <= 300.0f) {
                auto action = Self(q, settings, 2, "Zed E LaneClear", 45);
                if (action.valid) action.retryMs = 500;
                return action;
            }
            if (q.hasSpellWindow && settings.zedQ && Q.ready) {
                auto action = Position(q, settings, 0, 925.0f, 0.0f, 0.0f,
                                       "Zed Q LaneClear", 260);
                if (action.valid) action.retryMs = 700;
                return action;
            }
            return {};
        }

        // Death Mark is targeted. Cast it only when the conservative post-armor
        // combo estimate is lethal, and never press the R2 recast.
        if (settings.zedR && R.ready && R.level > 0 && !rRecast &&
            q.retryReady[3] && q.hasSpellWindow && q.distance <= 625.0f &&
            q.targetHp > 0.0f) {
            const float estimatedDamage = EstimateZedRComboDamage(q);
            if (estimatedDamage >= q.targetHp + settings.zedRDamageBuffer) {
                auto action = Position(q, settings, 3, 625.0f, 0.0f, 0.0f,
                                       "Zed R Execute", 850);
                if (action.valid) {
                    action.retryMs = 5000;
                    action.estimatedDamage = estimatedDamage;
                }
                return action;
            }
        }

        const bool localERange = q.distance <= 300.0f;
        const bool shadowERange = wRecast && q.distance <= 950.0f;

        // At melee range an extra W wastes energy and can create an unsafe
        // recast. Slash first, then weave Q without delaying an available AA.
        if (q.hasSpellWindow && settings.zedE && E.ready && q.retryReady[2] &&
            (localERange || shadowERange)) {
            auto action = Self(q, settings, 2,
                               shadowERange && !localERange ? "Zed E Shadow" : "Zed E",
                               45);
            if (action.valid) action.retryMs = 500;
            return action;
        }

        // Place W only when Q or E can immediately follow. Aiming at targets up
        // to 900 lets League clamp the shadow to its cast range; shadow E still
        // covers the remaining gap. W2 is deliberately never selected.
        const bool hasFollowUp =
            (settings.zedQ && Q.ready && q.retryReady[0]) ||
            (settings.zedE && E.ready && q.retryReady[2]);
        if (q.hasSpellWindow && settings.zedW && W.ready && !wRecast &&
            q.retryReady[1] && hasFollowUp && q.distance >= 350.0f &&
            q.distance <= 900.0f) {
            auto action = Position(q, settings, 1, 900.0f, 0.0f, 0.0f,
                                   "Zed W Shadow", 220);
            if (action.valid) action.retryMs = 5000;
            return action;
        }

        // Razor Shuriken passes through minions (with reduced subsequent-hit
        // damage), so unlike Ezreal Q it must not be held for collision.
        if (q.hasSpellWindow && settings.zedQ && Q.ready && q.retryReady[0]) {
            auto action = Position(q, settings, 0, 925.0f, 0.0f, 0.0f,
                                   "Zed Q", 260);
            if (action.valid) action.retryMs = 700;
            return action;
        }
        return {};
    }

    if (champion == "LUCIAN") {
        if (q.targetIsMinion) {
            // Piercing Light is targeted and continues through the selected
            // minion. Preserve W, E and R during lane clear.
            if (q.hasSpellWindow && settings.lucianQ && Q.ready &&
                q.retryReady[0]) {
                auto action = Position(q, settings, 0, 500.0f, 0.0f, 0.0f,
                                       "Lucian Q LaneClear", 280);
                if (action.valid) action.retryMs = 650;
                return action;
            }
            return {};
        }

        // Each cast is emitted only in a real AA backswing. The executor adds
        // a passive lock after a successful Q/W and clears it on the next AA,
        // producing spell -> Lightslinger rather than Q -> W input dumping.
        if (q.hasSpellWindow && settings.lucianQ && Q.ready &&
            q.retryReady[0] && q.distance <= 500.0f) {
            auto action = Position(q, settings, 0, 500.0f, 0.0f, 0.0f,
                                   "Lucian Q", 280);
            if (action.valid) action.retryMs = 650;
            return action;
        }
        if (q.hasSpellWindow && settings.lucianW && W.ready &&
            q.retryReady[1]) {
            auto action = Position(q, settings, 1, 900.0f, 0.25f, 1600.0f,
                                   "Lucian W", 280);
            if (action.valid) {
                action.collisionRadius = 40.0f; // Ardent Blaze is 80 wide.
                action.collisionHits = 1;
                action.maxLead = 260.0f;
                action.requireStableMotion = true;
                action.retryMs = 1200;
            }
            return action;
        }

        // E stays disabled until cursor-intent plus wall/turret safety can be
        // verified. R remains manual so orbwalker movement cannot break or
        // mis-steer The Culling channel.
        return {};
    }

    if (champion == "YASUO") {
        const std::string qName = Upper(Q.name);
        const bool tornadoQ = qName.find("Q3") != std::string::npos ||
            qName.find("TORNADO") != std::string::npos ||
            qName.find("WHIRLWIND") != std::string::npos;

        // Last Breath is never guessed from distance or animation timing. The
        // executor exposes it only while the target has a live Knockup/Knockback
        // buff entry, and it always remains champion-only.
        if (!q.targetIsMinion && settings.yasuoR && R.ready && R.level > 0 &&
            q.retryReady[3] && q.targetAirborne) {
            auto action = Position(q, settings, 3, 1400.0f, 0.0f, 0.0f,
                                   "Yasuo R Airborne", 420);
            if (action.valid) action.retryMs = 3000;
            return action;
        }

        if (q.targetIsMinion) {
            if (settings.yasuoQ && Q.ready && q.retryReady[0] &&
                q.hasSpellWindow && q.distance <= 475.0f) {
                auto action = Position(q, settings, 0, 475.0f, 0.0f, 0.0f,
                                       "Yasuo Q LaneClear", 80);
                if (action.valid) action.retryMs = 300;
                return action;
            }
            return {};
        }

        // E is targeted at the auxiliary unit selected by the executor: it is
        // in 475 range, improves distance to the champion, and was not dashed
        // through in the last ten seconds. A ready Q is pressed during the dash
        // on the following scan, creating the circular E-Q strike.
        if (settings.yasuoE && E.ready && Q.ready && q.retryReady[2] &&
            q.retryReady[0] && q.hasAuxTarget &&
            !q.yasuoEqPending && q.hasSpellWindow &&
            q.distance > q.localAttackRange + 70.0f) {
            auto action = Auxiliary(q, settings, 2, 475.0f,
                                    tornadoQ ? "Yasuo E-Q3 Gapclose" :
                                               "Yasuo E-Q Gapclose", 80);
            if (action.valid) action.retryMs = 140;
            return action;
        }

        const bool qWindow = q.hasSpellWindow || q.yasuoEqPending;
        if (settings.yasuoQ && Q.ready && q.retryReady[0] && qWindow) {
            if (tornadoQ) {
                // The Evade database puts the tornado missile at 1100, not the
                // 1150 previously assumed, so a cast past that was clamped.
                auto action = Position(q, settings, 0, 1100.0f, 0.25f, 1200.0f,
                                       q.yasuoEqPending ? "Yasuo E-Q3 Spin" :
                                                          "Yasuo Q3", 100);
                if (action.valid) {
                    action.maxLead = 280.0f;
                    action.requireStableMotion = true;
                    action.retryMs = 320;
                }
                return action;
            }
            auto action = Position(q, settings, 0, 475.0f, 0.0f, 0.0f,
                                   q.yasuoEqPending ? "Yasuo E-Q Spin" :
                                                      "Yasuo Q", 80);
            if (action.valid) action.retryMs = 300;
            return action;
        }

        // Wind Wall needs incoming-projectile threat selection rather than an
        // enemy aim point, so it stays outside the offensive combo profile.
        return {};
    }

    if (champion == "VAYNE") {
        // Condemn is targeted, but the stun depends on the knockback ray. The
        // caller proves that ray against the live NavGrid before enabling it.
        if (!q.targetIsMinion && settings.vayneE && E.ready && q.retryReady[2] &&
            q.vayneCondemnWall && q.distance <= 550.0f) {
            auto action = Position(q, settings, 2, 550.0f, 0.0f, 0.0f,
                                   "Vayne E WallStun", 180);
            if (action.valid) action.retryMs = 1500;
            return action;
        }

        // Final Hour is reserved for a real sustained fight: the target must be
        // in AA range and healthy enough that one ordinary hit will not finish
        // it. This avoids spending R on trivial cleanup attacks.
        if (!q.targetIsMinion && settings.vayneR && R.ready && R.level > 0 &&
            q.retryReady[3] && q.hasAttackTarget &&
            q.distance <= q.localAttackRange + 65.0f &&
            q.targetHp > q.totalAttackDamage * 2.2f) {
            auto action = Self(q, settings, 3, "Vayne R", 45);
            if (action.valid) action.retryMs = 5000;
            return action;
        }

        // Keep the original low-health peel as a fallback even when no wall is
        // present; this branch is deliberately limited to a very close target.
        if (!q.targetIsMinion && settings.vayneE && E.ready && q.retryReady[2] &&
            q.localHpPct <= settings.vayneDefensiveEHpPct &&
            q.distance <= 425.0f) {
            auto action = Position(q, settings, 2, 550.0f, 0.0f, 0.0f,
                                   "Vayne E Defensive", 180);
            if (action.valid) action.retryMs = 1500;
            return action;
        }

        // Tumble follows the player's physical mouse direction. It is emitted
        // after an AA command and the executor advances nextAtk so the empowered
        // shot is consumed immediately instead of waiting a full normal period.
        if (settings.vayneQ && Q.ready && q.retryReady[0] &&
            q.hasAttackTarget && q.aaGapMs >= 70 &&
            q.distance <= q.localAttackRange + 65.0f) {
            auto action = Self(q, settings, 0,
                               q.targetIsMinion ? "Vayne Q LaneClear" : "Vayne Q",
                               90);
            if (action.valid) action.retryMs = 450;
            return action;
        }
        // Silver Bolts is passive and therefore has no cast action.
        return {};
    }

    if (champion == "MISSFORTUNE") {
        if (q.targetIsMinion) {
            // V lane-clear: Double Up bounces from the selected minion to a
            // second unit behind it, which is the efficient wave tool. Strut is
            // a cheap self steroid and is kept on a long throttle. Make It Rain
            // and Bullet Time remain champion-only.
            if (settings.missFortuneW && W.ready && q.retryReady[1] &&
                q.hasAttackTarget && q.distance <= q.localAttackRange + 100.0f &&
                q.aaGapMs >= 55) {
                auto action = Self(q, settings, 1, "MissFortune W LaneClear", 45);
                if (action.valid) action.retryMs = 2500;
                return action;
            }
            if (q.hasSpellWindow && settings.missFortuneQ && Q.ready) {
                auto action = Position(q, settings, 0, 650.0f, 0.0f, 0.0f,
                                       "MissFortune Q LaneClear", 120);
                if (action.valid) {
                    action.retryMs = 900;
                    return action;
                }
            }
            return {};
        }
        // W is a self attack-speed steroid and may fit in a short pre-AA gap.
        if (settings.missFortuneW && W.ready && q.retryReady[1] &&
            q.hasAttackTarget && q.distance <= q.localAttackRange + 100.0f &&
            q.aaGapMs >= 55)
            return Self(q, settings, 1, "MissFortune W", 45);
        if (q.hasSpellWindow && settings.missFortuneQ && Q.ready) {
            // Double Up is targeted: zero delay/speed makes the executor use
            // the target's current W2S point instead of leading it.
            auto action = Position(q, settings, 0, 650.0f, 0.0f, 0.0f,
                                   "MissFortune Q", 120);
            if (action.valid) return action;
        }
        if (q.hasSpellWindow && settings.missFortuneE && E.ready) {
            auto action = Position(q, settings, 2, 1000.0f, 0.25f, 0.0f,
                                   "MissFortune E", 260);
            if (action.valid) return action;
        }
        if (q.hasSpellWindow && settings.missFortuneR && R.ready && R.level > 0 &&
            q.retryReady[3] && q.distance >= 450.0f && q.distance <= 1400.0f &&
            q.targetHpPct > 0.0f && q.targetHpPct <= settings.missFortuneRHpPct) {
            auto action = Position(q, settings, 3, 1400.0f, 0.0f, 0.0f,
                                   "MissFortune R", 3000);
            if (action.valid) action.retryMs = (std::max)(action.retryMs, 5000);
            return action;
        }
        return {};
    }

    if (champion == "DRAVEN") {
        if (q.targetIsMinion) {
            // V lane-clear: Spinning Axe is the auto-attack steroid and its
            // cooldown refreshes on every catch, so it is the only efficient
            // farming spell. Blood Rush, Stand Aside and Whirling Death are
            // all preserved for champions.
            if (settings.dravenQ && Q.ready && q.retryReady[0] &&
                q.hasAttackTarget && q.aaGapMs >= 55) {
                auto action = Self(q, settings, 0, "Draven Q LaneClear", 45);
                if (action.valid) action.retryMs = 900;
                return action;
            }
            return {};
        }
        const bool fighting = q.hasAttackTarget && q.aaGapMs >= 55;
        const bool chasing = !q.hasAttackTarget &&
            q.distance > q.localAttackRange && q.distance <= 1000.0f;
        if (settings.dravenQ && Q.ready && q.retryReady[0] && fighting) {
            auto action = Self(q, settings, 0, "Draven Q", 45);
            if (action.valid) action.retryMs = 900;
            return action;
        }
        if (settings.dravenW && W.ready && q.retryReady[1] && (fighting || chasing)) {
            auto action = Self(q, settings, 1, "Draven W", 45);
            // Catching an axe refreshes W, so the generic 4s anti-spam lock is
            // too long for Draven. Still retain a small mana/input throttle.
            if (action.valid) action.retryMs = 1200;
            return action;
        }
        if (q.hasSpellWindow && settings.dravenE && E.ready) {
            auto action = Position(q, settings, 2, 1050.0f, 0.25f, 1400.0f,
                                   "Draven E", 260);
            if (action.valid) {
                // Stand Aside is 130 units wide and hits everything in its
                // path, so it tolerates more lead than the thin skillshots.
                action.maxLead = 340.0f;
                action.requireStableMotion = true;
                return action;
            }
        }
        const std::string rName = Upper(R.name);
        const bool initialR = rName.find("DRAVENRCAST") != std::string::npos &&
            rName.find("DOUBLE") == std::string::npos &&
            rName.find("RETURN") == std::string::npos;
        if (q.hasSpellWindow && settings.dravenR && R.ready && R.level > 0 &&
            q.retryReady[3] && initialR && q.distance >= 650.0f &&
            q.distance <= settings.dravenRMaxRange && q.targetHpPct > 0.0f &&
            q.targetHpPct <= settings.dravenRHpPct) {
            auto action = Position(q, settings, 3, settings.dravenRMaxRange,
                                   0.50f, 2000.0f, "Draven R", 500);
            if (action.valid) {
                action.requireStableMotion = true;
                action.retryMs = 5000;
            }
            return action;
        }
        return {};
    }

    if (champion == "ZOE" && q.hasSpellWindow) {
        const std::string qName = Upper(Q.name);
        const bool qRecast = qName.find("ZOEQRECAST") != std::string::npos;
        if (q.targetIsMinion) {
            // V lane-clear: the Q chain only. A star that already launched must
            // still be recast or it is simply lost, so Q2 keeps its priority
            // here as well. Paddle Star is Zoe's wave tool; Sleepy Trouble
            // Bubble and the stolen spell stay reserved for champions.
            if (settings.zoeQ && Q.ready) {
                if (qRecast) {
                    auto action = Position(q, settings, 0, 1975.0f, 0.0f, 2500.0f,
                                           "Zoe Q2 LaneClear", 180);
                    if (action.valid) action.retryMs = 3000;
                    return action;
                }
                auto action = Position(q, settings, 0, 1975.0f, 0.0f, 1200.0f,
                                       "Zoe Q1 LaneClear", 180);
                if (action.valid) action.retryMs = 400;
                return action;
            }
            return {};
        }
        // Q2 must be preserved above all else: the star vanishes if the recast
        // window is spent on another spell.
        if (qRecast) {
            if (settings.zoeQ && Q.ready) {
                auto action = Position(q, settings, 0, 1975.0f, 0.0f, 2500.0f,
                                       "Zoe Q2", 180);
                if (action.valid) action.retryMs = 3000;
                return action;
            }
            return {};
        }
        // Q1 takes priority over E. Aiming straight at the predicted target is
        // strictly better than the old backwards toss: at close range the star
        // lands on the enemy instantly, and at long range it stops at its 800
        // unit launch point so the Q2 recast reaches up to ~1975 units.
        if (settings.zoeQ && Q.ready) {
            auto action = Position(q, settings, 0, 1975.0f, 0.0f, 1200.0f,
                                   "Zoe Q1", 180);
            if (action.valid) {
                // Q2 must not be pressed inside Q1's own cast time or the
                // game drops the recast and the star never leaves its resting
                // point. Allow the launch animation to complete first.
                action.retryMs = 400;
            }
            return action;
        }
        if (settings.zoeE && E.ready) {
            // 1850 is the missile speed derived from the Evade database entry
            // (range / (active_ms - 350)); 1700 over-led the bubble slightly.
            auto action = Position(q, settings, 2, 800.0f, 0.30f, 1850.0f,
                                   "Zoe E", 300);
            if (action.valid) {
                // Sleepy Trouble Bubble stops on the first enemy hit and is
                // only 40 units wide.
                action.collisionRadius = 20.0f;
                action.collisionHits = 1;
                action.maxLead = 240.0f;
                action.requireStableMotion = true;
                return action;
            }
        }
        // W needs the currently stolen spell identity/targeting semantics. R
        // needs a portal-safe point plus Q missile state. Both remain disabled
        // until those state readers are verified.
        return {};
    }

    if (champion == "AATROX") {
        const std::string qName = Upper(Q.name);
        const bool qStage3 = qName.find("Q3") != std::string::npos;
        const bool qStage2 = qName.find("Q2") != std::string::npos ||
            qName.find("RECAST") != std::string::npos;

        // Q2/Q3 are recast windows (1 s static cooldown, 4 s window), not full
        // spell casts. They fire on their own cadence without waiting for an AA
        // backswing so the three swipes stay chained.
        if (settings.aatroxQ && Q.ready && q.retryReady[0]) {
            if (qStage3 && q.distance <= 475.0f) {
                // Q3 slam: 300-radius circle, sweet spot at the center. Aim
                // the circle center at the target's lead position.
                auto action = Position(q, settings, 0, 475.0f, 0.60f, 0.0f,
                                       "Aatrox Q3", 110);
                if (action.valid) action.retryMs = 1000;
                return action;
            }
            if (qStage2 && q.distance <= 475.0f) {
                // Q2 trapezoid reaches 475; sweet spot is the far edge.
                auto action = Position(q, settings, 0, 475.0f, 0.60f, 0.0f,
                                       "Aatrox Q2", 110);
                if (action.valid) {
                    action.aimMode = ChampionCombo::AimMode::TipAim;
                    action.aimDistance = 475.0f;
                    action.retryMs = 1000;
                }
                return action;
            }
        }
        // Q1 opener: the rectangle reaches 625 and its sweet spot is the far
        // edge, so aim at the tip instead of the target itself. Closer enemies
        // are still inside the rectangle and take normal damage.
        if (q.hasSpellWindow && settings.aatroxQ && Q.ready && q.retryReady[0] &&
            q.distance <= 625.0f) {
            auto action = Position(q, settings, 0, 625.0f, 0.60f, 0.0f,
                                   "Aatrox Q", 110);
            if (action.valid) {
                action.aimMode = ChampionCombo::AimMode::TipAim;
                action.aimDistance = 625.0f;
                action.retryMs = 1000;
            }
            return action;
        }

        // Umbral Dash is a 300-unit dash that can be cast during Q's cast time.
        // Use it to close the gap so Q1 reaches its sweet-spot edge. The
        // executor verifies the dash landing point against walls/Evade before
        // committing, and never interrupts an in-progress Q chain.
        // On a wave the Q chain alone is the lane-clear tool: never spend a
        // reposition dash to reach a minion.
        if (!q.targetIsMinion &&
            settings.aatroxE && E.ready && q.retryReady[2] &&
            !qStage2 && !qStage3 && settings.aatroxQ && Q.ready &&
            q.hasSpellWindow && q.distance > 550.0f && q.distance <= 950.0f) {
            auto action = Position(q, settings, 2, 300.0f, 0.0f, 0.0f,
                                   "Aatrox E", 45);
            if (action.valid) action.retryMs = 2000;
            return action;
        }

        const std::string wName = Upper(W.name);
        const bool wRecast = wName.find("W2") != std::string::npos ||
            wName.find("RECAST") != std::string::npos;
        // The tether recast pulls the chained target back to the anchor. It is
        // a re-trigger, so a plain key tap is enough and needs no aim point.
        if (settings.aatroxW && W.ready && q.retryReady[1] && wRecast &&
            q.distance <= 800.0f) {
            auto action = Self(q, settings, 1, "Aatrox W Recast", 45);
            if (action.valid) action.retryMs = 2500;
            return action;
        }
        // W1 is a slow wide line skillshot. The short retry keeps the recast
        // reachable once the chain latches. A tether pull is wasted on a wave,
        // so it is champion-only.
        if (!q.targetIsMinion &&
            q.hasSpellWindow && settings.aatroxW && W.ready && q.retryReady[1] &&
            !wRecast && q.distance <= 800.0f) {
            // Infernal Chains travels at 1800, not 1200. The old value inflated
            // the estimated flight time by 50% and aimed the chain ahead of the
            // target; the database entry (825 range, 808 ms) gives 1801.
            auto action = Position(q, settings, 1, 800.0f, 0.25f, 1800.0f,
                                   "Aatrox W", 90);
            if (action.valid) action.retryMs = 110;
            return action;
        }

        // World Ender is a self buff: pop it while committing to a low-HP
        // target in range so the bonus AD lands on the killing blow.
        if (!q.targetIsMinion && settings.aatroxR && R.ready && R.level > 0 &&
            q.retryReady[3] && q.hasAttackTarget && q.distance <= 700.0f &&
            q.targetHpPct > 0.0f && q.targetHpPct <= settings.aatroxRHpPct) {
            auto action = Self(q, settings, 3, "Aatrox R", 45);
            if (action.valid) action.retryMs = 5000;
            return action;
        }
        return {};
    }


    if (champion == "XERATH") {
        if (q.targetIsMinion) {
            // V lane-clear: Eye of Destruction covers the wave; Q charge and
            // the E stun are kept for champions.
            if (q.hasSpellWindow && settings.xerathW && W.ready) {
                auto action = Position(q, settings, 1, 1000.0f, 1.0f, 0.0f,
                                       "Xerath W LaneClear", 260);
                if (action.valid) {
                    action.retryMs = 1200;
                    return action;
                }
            }
            return {};
        }
        if (!q.hasSpellWindow) return {};

        // Shocking Orb first: a landed stun turns everything that follows
        // into a cc-locked guaranteed hit. Stops on the first enemy (70 wide).
        if (settings.xerathE && E.ready) {
            auto action = Position(q, settings, 2, 1000.0f, 0.25f, 1600.0f,
                                   "Xerath E", 280);
            if (action.valid) {
                action.collisionRadius = 35.0f;
                action.collisionHits = 1;
                action.maxLead = 240.0f;
                action.requireStableMotion = true;
                return action;
            }
        }

        // Arcanopulse is a charged beam: base reach 750, growing while the
        // key is held. The executor thread blocks during the hold, so the
        // charge is capped at 1000 ms (~1200 effective reach) - beyond that
        // Evade and the orbwalker would be blind for too long.
        if (settings.xerathQ && Q.ready && q.retryReady[0] &&
            q.distance <= 1200.0f) {
            const float need = q.distance + 120.0f;
            // Same executor-blocking limit as Varus Q; 600 ms of charge is
            // roughly 1030 effective reach.
            const float chargeMs = need > 750.0f
                ? (std::min)((need - 750.0f) / 650.0f * 1400.0f, 600.0f)
                : 0.0f;
            auto action = Position(q, settings, 0, 1200.0f,
                                   chargeMs / 1000.0f + 0.55f, 0.0f,
                                   "Xerath Q", (int)chargeMs + 450);
            if (action.valid) {
                action.castHoldMs = 60 + (int)chargeMs;
                action.maxLead = 320.0f;
                action.requireStableMotion = true;
                action.retryMs = 900;
                return action;
            }
        }

        // W poke filler on the ~1 s impact delay.
        if (settings.xerathW && W.ready) {
            auto action = Position(q, settings, 1, 1000.0f, 1.0f, 0.0f,
                                   "Xerath W", 280);
            if (action.valid) {
                action.maxLead = 300.0f;
                action.requireStableMotion = true;
                action.retryMs = 1500;
                return action;
            }
        }
        // R: locus overlay + per-shot recasts; stays manual (see Settings).
        return {};
    }

    if (champion == "MORGANA") {
        if (q.targetIsMinion) {
            // V lane-clear: Tormented Shadow is the wave tool; the binding is
            // far too valuable to spend on a minion.
            if (q.hasSpellWindow && settings.morganaW && W.ready) {
                auto action = Position(q, settings, 1, 900.0f, 0.25f, 0.0f,
                                       "Morgana W LaneClear", 260);
                if (action.valid) {
                    action.retryMs = 1200;
                    return action;
                }
            }
            return {};
        }
        if (!q.hasSpellWindow) return {};

        // Dark Binding: slow missile (1200) that stops on the first enemy.
        // A hit roots for up to 3 s and the cc-lock layer then lands W - and
        // any follow-up - for free.
        if (settings.morganaQ && Q.ready) {
            auto action = Position(q, settings, 0, 1250.0f, 0.25f, 1200.0f,
                                   "Morgana Q", 280);
            if (action.valid) {
                action.collisionRadius = 35.0f;
                action.collisionHits = 1;
                action.maxLead = 300.0f;
                action.requireStableMotion = true;
                return action;
            }
        }
        // W zone: large enough that it needs no stability gate; the retry
        // roughly matches the pool duration so it is not respread instantly.
        if (settings.morganaW && W.ready && q.retryReady[1]) {
            auto action = Position(q, settings, 1, 900.0f, 0.25f, 0.0f,
                                   "Morgana W", 260);
            if (action.valid) {
                action.maxLead = 260.0f;
                action.retryMs = 1500;
                return action;
            }
        }
        // E needs ally/threat selection, R is a self channel that orbwalker
        // movement would interrupt; both stay manual.
        return {};
    }

    if (champion == "VARUS") {
        if (q.targetIsMinion) {
            // V lane-clear: Hail of Arrows covers the wave. The charged Q is
            // kept for champions and R is never spent on a minion.
            if (q.hasSpellWindow && settings.varusE && E.ready) {
                auto action = Position(q, settings, 2, 925.0f, 0.9f, 0.0f,
                                       "Varus E LaneClear", 260);
                if (action.valid) {
                    action.retryMs = 1500;
                    return action;
                }
            }
            return {};
        }

        // Chain of Corruption: engage root. A hit chains into cc-locked Q/E.
        // Speed derived from the Evade DB missile entry.
        if (settings.varusR && R.ready && R.level > 0 && q.retryReady[3] &&
            q.hasSpellWindow && q.distance >= 600.0f && q.distance <= 1250.0f) {
            auto action = Position(q, settings, 3, 1300.0f, 0.24f, 1950.0f,
                                   "Varus R", 420);
            if (action.valid) {
                action.maxLead = 300.0f;
                action.requireStableMotion = true;
                action.retryMs = 5000;
                return action;
            }
        }

        // Piercing Arrow: charged, base 925 growing while held; capped at
        // 900 ms hold (~1400 effective) for the same executor-blocking reason
        // as Xerath Q. The arrow pierces, so no collision test.
        if (q.hasSpellWindow && settings.varusQ && Q.ready && q.retryReady[0] &&
            q.distance <= 1400.0f) {
            const float need = q.distance + 120.0f;
            // Capped at 600 ms: the executor thread is blocked for the whole
            // charge, so a longer hold leaves Evade and the orbwalker blind.
            // The aim is refreshed just before release, so the shot still
            // tracks the target across the charge.
            const float chargeMs = need > 925.0f
                ? (std::min)((need - 925.0f) / 670.0f * 1250.0f, 600.0f)
                : 0.0f;
            auto action = Position(q, settings, 0, 1400.0f,
                                   chargeMs / 1000.0f + 0.15f, 1900.0f,
                                   "Varus Q", (int)chargeMs + 350);
            if (action.valid) {
                action.castHoldMs = 60 + (int)chargeMs;
                action.maxLead = 300.0f;
                action.requireStableMotion = true;
                action.retryMs = 900;
                return action;
            }
        }

        // Hail of Arrows: ground zone, ~0.9 s to land.
        if (q.hasSpellWindow && settings.varusE && E.ready) {
            auto action = Position(q, settings, 2, 925.0f, 0.9f, 0.0f,
                                   "Varus E", 260);
            if (action.valid) {
                action.maxLead = 320.0f;
                action.requireStableMotion = true;
                action.retryMs = 1500;
                return action;
            }
        }
        // W active only empowers the next Q; left to the player.
        return {};
    }


    if (champion == "JINX") {
        // Q is a stance toggle and there is no safe spell to spend on a wave;
        // lane clear stays pure auto attacks (rockets are the player's call).
        if (q.targetIsMinion) return {};

        // Super Mega Death Rocket: long-range execute. Damage scales with
        // missing HP, so the HP gate keeps it for real kill windows only.
        if (settings.jinxR && R.ready && R.level > 0 && q.retryReady[3] &&
            q.hasSpellWindow && !q.hasAttackTarget &&
            q.distance >= 900.0f && q.distance <= 2500.0f &&
            q.targetHpPct > 0.0f && q.targetHpPct <= settings.jinxRHpPct) {
            auto action = Position(q, settings, 3, 2500.0f, 0.6f, 1700.0f,
                                   "Jinx R", 600);
            if (action.valid) {
                action.maxLead = 340.0f;
                action.requireStableMotion = true;
                action.retryMs = 5000;
                return action;
            }
        }

        // Zap! has a long wind-up, so never trade an available AA for it.
        // Thin (60 wide) and stops on the first unit hit.
        if (q.hasSpellWindow && settings.jinxW && W.ready &&
            (!q.hasAttackTarget || q.aaGapMs >= 600)) {
            auto action = Position(q, settings, 1, 1450.0f, 0.6f, 3200.0f,
                                   "Jinx W", 420);
            if (action.valid) {
                action.collisionRadius = 30.0f;
                action.collisionHits = 1;
                action.maxLead = 300.0f;
                action.requireStableMotion = true;
                action.retryMs = 1200;
                return action;
            }
        }

        // Flame Chompers arm after ~0.7 s: only reliable on a target that is
        // already controlled (the cc-lock layer then drops them on the body).
        // Off by default; see Settings.
        if (settings.jinxE && E.ready && q.retryReady[2] &&
            q.hasSpellWindow && q.distance <= 900.0f) {
            auto action = Position(q, settings, 2, 925.0f, 0.8f, 0.0f,
                                   "Jinx E", 260);
            if (action.valid) {
                action.retryMs = 5000;
                return action;
            }
        }
        return {};
    }

    if (champion == "BRAND") {
        if (q.targetIsMinion) {
            // V lane-clear: Pillar of Flame covers the wave; Q stun and the
            // R bounce are kept for champions.
            if (q.hasSpellWindow && settings.brandW && W.ready) {
                auto action = Position(q, settings, 1, 900.0f, 0.9f, 0.0f,
                                       "Brand W LaneClear", 260);
                if (action.valid) {
                    action.retryMs = 1200;
                    return action;
                }
            }
            return {};
        }
        if (!q.hasSpellWindow) return {};

        // Pyroclasm: targeted burst. Best in clumps, which we cannot see, so
        // the HP gate keeps it as a finisher on an already-burned target.
        if (settings.brandR && R.ready && R.level > 0 && q.retryReady[3] &&
            q.distance <= 730.0f && q.targetHpPct > 0.0f &&
            q.targetHpPct <= settings.brandRHpPct) {
            auto action = Position(q, settings, 3, 750.0f, 0.0f, 0.0f,
                                   "Brand R", 300);
            if (action.valid) {
                action.retryMs = 5000;
                return action;
            }
        }

        // Standard trade order W -> Q -> E: the pillar applies Blaze, the
        // sear then stuns an ablaze target, the conflagration finishes.
        // The retry cadence approximates that sequencing.
        if (settings.brandW && W.ready && q.retryReady[1]) {
            auto action = Position(q, settings, 1, 900.0f, 0.9f, 0.0f,
                                   "Brand W", 280);
            if (action.valid) {
                action.maxLead = 280.0f;
                action.retryMs = 1200;
                return action;
            }
        }
        if (settings.brandQ && Q.ready) {
            auto action = Position(q, settings, 0, 1040.0f, 0.25f, 1600.0f,
                                   "Brand Q", 280);
            if (action.valid) {
                action.collisionRadius = 30.0f;
                action.collisionHits = 1;
                action.maxLead = 260.0f;
                action.requireStableMotion = true;
                return action;
            }
        }
        // Conflagration is targeted: no prediction, click the unit.
        if (settings.brandE && E.ready && q.distance <= 615.0f) {
            auto action = Position(q, settings, 2, 625.0f, 0.0f, 0.0f,
                                   "Brand E", 120);
            if (action.valid) {
                action.retryMs = 900;
                return action;
            }
        }
        return {};
    }

    if (champion == "VELKOZ") {
        if (q.targetIsMinion) {
            // V lane-clear: Void Rift line covers the wave. E knockup and the
            // Q split stay for champions.
            if (q.hasSpellWindow && settings.velkozW && W.ready) {
                auto action = Position(q, settings, 1, 1050.0f, 0.25f, 1700.0f,
                                       "Velkoz W LaneClear", 260);
                if (action.valid) {
                    action.retryMs = 900;
                    return action;
                }
            }
            return {};
        }
        if (!q.hasSpellWindow) return {};

        // Tectonic Disruption first: the knockup turns the follow-up W (and
        // anything else) into a cc-locked guaranteed hit.
        if (settings.velkozE && E.ready && q.retryReady[2] &&
            q.distance <= 800.0f) {
            auto action = Position(q, settings, 2, 800.0f, 0.85f, 0.0f,
                                   "Velkoz E", 280);
            if (action.valid) {
                action.maxLead = 250.0f;
                action.requireStableMotion = true;
                action.retryMs = 2000;
                return action;
            }
        }
        // Void Rift passes through everything - no collision test.
        if (settings.velkozW && W.ready) {
            auto action = Position(q, settings, 1, 1050.0f, 0.25f, 1700.0f,
                                   "Velkoz W", 280);
            if (action.valid) {
                action.maxLead = 280.0f;
                action.requireStableMotion = true;
                return action;
            }
        }
        // Q needs a manually timed split reactivation and R is a steered
        // channel that orbwalker movement would break; both stay manual.
        return {};
    }

    if (champion == "KARTHUS") {
        // Lay Waste is the entire loop for both farming and poke: a small
        // ground circle on a short cooldown. Isolated hits deal double
        // damage, which prediction naturally favours by leading the target
        // away from the wave.
        const char* label = q.targetIsMinion ? "Karthus Q LaneClear"
                                             : "Karthus Q";
        if (q.hasSpellWindow && settings.karthusQ && Q.ready &&
            q.retryReady[0]) {
            auto action = Position(q, settings, 0, 875.0f, 0.75f, 0.0f,
                                   label, 260);
            if (action.valid) {
                action.maxLead = 240.0f;
                action.retryMs = 700;
                return action;
            }
        }
        // W is a utility wall, E a mana toggle, R a global channel - manual.
        return {};
    }


    if (champion == "NILAH") {
        // Formless Blade is the whole weave: a short line that also empowers
        // her next attacks, so it is worth using on waves and champions alike.
        // Melee cadence: fire in the attack backswing rather than waiting for
        // the full spell window.
        const bool qWindow = q.hasSpellWindow ||
            (q.hasAttackTarget && q.aaGapMs >= 150);
        if (settings.nilahQ && Q.ready && q.retryReady[0] && qWindow &&
            q.distance <= 580.0f) {
            auto action = Position(q, settings, 0, 600.0f, 0.25f, 0.0f,
                                   q.targetIsMinion ? "Nilah Q LaneClear"
                                                    : "Nilah Q", 120);
            if (action.valid) {
                action.maxLead = 150.0f;
                action.retryMs = 1000;
                return action;
            }
        }
        if (q.targetIsMinion) return {};

        // Jubilant Veil: dodge basic attacks while brawling at low health.
        if (settings.nilahW && W.ready && q.retryReady[1] &&
            q.hasAttackTarget && q.distance <= 550.0f &&
            q.localHpPct <= settings.nilahWHpPct) {
            auto action = Self(q, settings, 1, "Nilah W", 45);
            if (action.valid) action.retryMs = 5000;
            return action;
        }
        // Apotheosis pulls around her: commit it in a real sustained fight,
        // never on cleanup the basic attacks already win.
        if (settings.nilahR && R.ready && R.level > 0 && q.retryReady[3] &&
            q.hasAttackTarget && q.distance <= 350.0f &&
            q.targetHp > q.totalAttackDamage * 2.0f) {
            auto action = Self(q, settings, 3, "Nilah R", 45);
            if (action.valid) action.retryMs = 5000;
            return action;
        }
        // E dashes onto a unit; landing safety is unverified - manual.
        return {};
    }

    if (champion == "SYNDRA") {
        // Dark Sphere is the loop for farm and poke: quick ground circle.
        if (q.hasSpellWindow && settings.syndraQ && Q.ready &&
            q.retryReady[0]) {
            auto action = Position(q, settings, 0, 800.0f, 0.6f, 0.0f,
                                   q.targetIsMinion ? "Syndra Q LaneClear"
                                                    : "Syndra Q", 260);
            if (action.valid) {
                action.maxLead = 220.0f;
                action.retryMs = 900;
                return action;
            }
        }
        if (q.targetIsMinion) return {};
        // Unleashed Power is targeted burst that scales with sphere count we
        // cannot read; the HP gate keeps it as a conservative finisher.
        if (settings.syndraR && R.ready && R.level > 0 && q.retryReady[3] &&
            q.hasSpellWindow && q.distance <= 675.0f &&
            q.targetHpPct > 0.0f && q.targetHpPct <= settings.syndraRHpPct) {
            auto action = Position(q, settings, 3, 675.0f, 0.0f, 0.0f,
                                   "Syndra R", 300);
            if (action.valid) {
                action.retryMs = 5000;
                return action;
            }
        }

        // Force of Will second stage: something is already held, throw it at
        // the champion. The game's own slot name is the source of truth here -
        // it reports SyndraWCast for as long as a sphere is carried, including
        // grabs the player made manually. An earlier attempt overrode this
        // name from a local flag and destroyed that information, which is why
        // the throw kept degrading into another grab. The executor's flag now
        // only covers the frame or two of lag right after our own grab.
        const std::string wName = Upper(W.name);
        const bool wHolding = wName.find("SYNDRAWCAST") != std::string::npos;
        if (settings.syndraW && W.ready && q.retryReady[1] && wHolding &&
            q.hasSpellWindow && q.distance <= 900.0f) {
            auto action = Position(q, settings, 1, 925.0f, 0.25f, 0.0f,
                                   "Syndra W Throw", 260);
            if (action.valid) {
                action.maxLead = 240.0f;
                action.retryMs = 900;
                return action;
            }
        }

        // Scatter the Weak without a tracked sphere is still a knockback and
        // damage. The old 450 gate almost never opened - Syndra fights near
        // her 800 Q range - so it is widened to the spell's real reach.
        if (settings.syndraE && E.ready && q.retryReady[2] &&
            q.hasSpellWindow && q.distance <= 600.0f) {
            auto action = Position(q, settings, 2, 650.0f, 0.1f, 0.0f,
                                   "Syndra E", 200);
            if (action.valid) {
                action.maxLead = 140.0f;
                action.retryMs = 2000;
                return action;
            }
        }

        // Force of Will first stage: grab the auxiliary unit (the executor
        // selects the nearest enemy minion) so the throw is loaded for the
        // next scan. Costs nothing offensively, so it runs last.
        if (settings.syndraW && W.ready && q.retryReady[1] && !wHolding &&
            q.hasSpellWindow && q.hasAuxTarget) {
            auto action = Auxiliary(q, settings, 1, 900.0f,
                                    "Syndra W Grab", 200);
            if (action.valid) {
                action.retryMs = 700;
                return action;
            }
        }
        return {};
    }

    if (champion == "ZIGGS") {
        // Bouncing Bomb: aimed at the first-bounce point; past that the
        // bounce physics are unmodelled, so the effective range stays 850.
        if (q.hasSpellWindow && settings.ziggsQ && Q.ready &&
            q.retryReady[0]) {
            auto action = Position(q, settings, 0, 850.0f, 0.75f, 0.0f,
                                   q.targetIsMinion ? "Ziggs Q LaneClear"
                                                    : "Ziggs Q", 260);
            if (action.valid) {
                action.maxLead = 240.0f;
                action.retryMs = 900;
                return action;
            }
        }
        if (q.targetIsMinion) return {};

        // Minefield: zoning slow between bomb cycles.
        if (q.hasSpellWindow && settings.ziggsE && E.ready &&
            q.retryReady[2] && q.distance <= 900.0f) {
            auto action = Position(q, settings, 2, 900.0f, 0.9f, 0.0f,
                                   "Ziggs E", 260);
            if (action.valid) {
                action.maxLead = 260.0f;
                action.retryMs = 4000;
                return action;
            }
        }
        // Mega Inferno Bomb: long flight (~1.6 s), so it is only thrown at a
        // low-HP target holding a consistent path; the navdest clamp keeps
        // the aim inside the point where the target will actually stop.
        if (q.hasSpellWindow && settings.ziggsR && R.ready && R.level > 0 &&
            q.retryReady[3] && !q.hasAttackTarget &&
            q.distance >= 900.0f && q.distance <= settings.ziggsRMaxRange &&
            q.targetHpPct > 0.0f && q.targetHpPct <= settings.ziggsRHpPct) {
            auto action = Position(q, settings, 3, settings.ziggsRMaxRange,
                                   1.6f, 0.0f, "Ziggs R", 800);
            if (action.valid) {
                action.maxLead = 400.0f;
                action.requireStableMotion = true;
                action.retryMs = 6000;
                return action;
            }
        }
        // W repositions himself or enemies - manual.
        return {};
    }

    if (champion == "TRISTANA") {
        // No spell is worth spending on a wave: E on minions is push the
        // player may not want, W is a jump. Pure auto attacks in V mode.
        if (q.targetIsMinion) return {};

        // Rapid Fire: attack-speed steroid once the trade is running.
        if (settings.tristanaQ && Q.ready && q.retryReady[0] &&
            q.hasAttackTarget && q.aaGapMs >= 55) {
            auto action = Self(q, settings, 0, "Tristana Q", 45);
            if (action.valid) action.retryMs = 5000;
            return action;
        }
        // Explosive Charge is targeted: stick it at trade start so the
        // following attacks stack and detonate it.
        if (settings.tristanaE && E.ready && q.retryReady[2] &&
            q.hasSpellWindow && q.distance <= 550.0f) {
            auto action = Position(q, settings, 2, 550.0f, 0.0f, 0.0f,
                                   "Tristana E", 120);
            if (action.valid) {
                action.retryMs = 4000;
                return action;
            }
        }
        // Buster Shot: targeted execute. The knockback can also peel, but
        // that needs intent we cannot read, so only the kill case is automated.
        if (settings.tristanaR && R.ready && R.level > 0 && q.retryReady[3] &&
            q.distance <= 550.0f && q.targetHpPct > 0.0f &&
            q.targetHpPct <= settings.tristanaRHpPct) {
            auto action = Position(q, settings, 3, 550.0f, 0.0f, 0.0f,
                                   "Tristana R", 200);
            if (action.valid) {
                action.retryMs = 5000;
                return action;
            }
        }
        // W is a jump; landing safety is unverified - manual.
        return {};
    }

    if (champion == "SIVIR") {
        // Boomerang Blade pierces out and back - no collision test. Speed
        // derived from the Evade DB missile entry.
        if (q.hasSpellWindow && settings.sivirQ && Q.ready &&
            q.retryReady[0]) {
            auto action = Position(q, settings, 0, 1200.0f, 0.25f, 1450.0f,
                                   q.targetIsMinion ? "Sivir Q LaneClear"
                                                    : "Sivir Q", 260);
            if (action.valid) {
                action.maxLead = 280.0f;
                action.requireStableMotion = true;
                action.retryMs = 1200;
                return action;
            }
        }
        if (q.targetIsMinion) return {};
        // Ricochet: attack steroid once the trade is running.
        if (settings.sivirW && W.ready && q.retryReady[1] &&
            q.hasAttackTarget && q.aaGapMs >= 55) {
            auto action = Self(q, settings, 1, "Sivir W", 45);
            if (action.valid) action.retryMs = 4000;
            return action;
        }
        // E spellshield needs incoming-threat context and R is a team
        // steroid; both stay manual.
        return {};
    }


    if (champion == "SERAPHINE") {
        if (q.targetIsMinion) {
            // V lane-clear: the note is cheap and echoes on the second cast.
            if (q.hasSpellWindow && settings.seraphineQ && Q.ready) {
                auto action = Position(q, settings, 0, 900.0f, 0.9f, 0.0f,
                                       "Seraphine Q LaneClear", 260);
                if (action.valid) {
                    action.retryMs = 900;
                    return action;
                }
            }
            return {};
        }
        if (!q.hasSpellWindow) return {};

        // Beat Drop first: it slows on hit and roots an already-slowed
        // target, so a landed E chains into cc-locked follow-ups. Passes
        // through units - no collision test. Speed from the Evade DB (1200).
        if (settings.seraphineE && E.ready && q.retryReady[2]) {
            auto action = Position(q, settings, 2, 1300.0f, 0.25f, 1200.0f,
                                   "Seraphine E", 280);
            if (action.valid) {
                action.maxLead = 280.0f;
                action.requireStableMotion = true;
                action.retryMs = 2000;
                return action;
            }
        }
        if (settings.seraphineQ && Q.ready) {
            auto action = Position(q, settings, 0, 900.0f, 0.9f, 0.0f,
                                   "Seraphine Q", 260);
            if (action.valid) {
                action.maxLead = 240.0f;
                action.retryMs = 900;
                return action;
            }
        }
        // W shields allies, R is a global engage line - both manual.
        return {};
    }

    if (champion == "SWAIN") {
        if (q.targetIsMinion) {
            // V lane-clear: the cone is the wave tool; Nevermove is kept.
            if (q.hasSpellWindow && settings.swainQ && Q.ready &&
                q.distance <= 700.0f) {
                auto action = Position(q, settings, 0, 750.0f, 0.1f, 0.0f,
                                       "Swain Q LaneClear", 200);
                if (action.valid) {
                    action.retryMs = 900;
                    return action;
                }
            }
            return {};
        }
        if (!q.hasSpellWindow) return {};

        // Nevermove: slow missile (935 from the DB) that roots on the return
        // pass, so a hit chains into a cc-locked Q. Strict gates because of
        // the low speed.
        if (settings.swainE && E.ready && q.retryReady[2]) {
            auto action = Position(q, settings, 2, 850.0f, 0.25f, 935.0f,
                                   "Swain E", 280);
            if (action.valid) {
                action.maxLead = 240.0f;
                action.requireStableMotion = true;
                action.retryMs = 2500;
                return action;
            }
        }
        // Death's Hand: near-instant cone, small lead only.
        if (settings.swainQ && Q.ready && q.distance <= 700.0f) {
            auto action = Position(q, settings, 0, 750.0f, 0.1f, 0.0f,
                                   "Swain Q", 200);
            if (action.valid) {
                action.maxLead = 120.0f;
                action.retryMs = 900;
                return action;
            }
        }
        // W needs vision judgment across the map and R is a drain state with
        // its own recast - both manual.
        return {};
    }

    if (champion == "TWITCH") {
        // No spell is spent on waves: E venom on minions is push the player
        // may not want and W is saved for champions.
        if (q.targetIsMinion) return {};

        // Contaminate: stack-based execute computed by the executor from the
        // target's DeadlyVenom stacks (Kalista Rend pattern). E hits every
        // afflicted enemy around Twitch, so this is a plain key press.
        if (settings.twitchE && E.ready && q.retryReady[2] &&
            q.twitchVenomStacks > 0 && q.twitchEKillable)
            return Self(q, settings, 2, "Twitch E Execute", 80);

        // Venom Cask: ground slow that also stacks venom for E.
        if (q.hasSpellWindow && settings.twitchW && W.ready &&
            q.retryReady[1]) {
            auto action = Position(q, settings, 1, 950.0f, 0.8f, 0.0f,
                                   "Twitch W", 260);
            if (action.valid) {
                action.maxLead = 260.0f;
                action.retryMs = 3000;
                return action;
            }
        }
        // Spray and Pray: commit in a real sustained fight, not on cleanup.
        if (settings.twitchR && R.ready && R.level > 0 && q.retryReady[3] &&
            q.hasAttackTarget && q.distance <= 850.0f &&
            q.targetHp > q.totalAttackDamage * 3.0f) {
            auto action = Self(q, settings, 3, "Twitch R", 45);
            if (action.valid) action.retryMs = 8000;
            return action;
        }
        // Q stealth is an engage/disengage decision - manual.
        return {};
    }

    if (champion == "KAISA") {
        if (q.targetIsMinion) {
            // V lane-clear: Icathian Rain sprays the wave around her.
            if (q.hasSpellWindow && settings.kaisaQ && Q.ready &&
                q.retryReady[0] && q.distance <= 500.0f) {
                auto action = Self(q, settings, 0, "KaiSa Q LaneClear", 45);
                if (action.valid) action.retryMs = 4000;
                return action;
            }
            return {};
        }

        // Void Seeker: long wind-up (0.4 s), never traded for a ready AA.
        // Stops on the first enemy hit, so minions block it.
        if (q.hasSpellWindow && settings.kaisaW && W.ready &&
            q.retryReady[1] &&
            (!q.hasAttackTarget || q.aaGapMs >= 650) &&
            q.distance <= 1800.0f) {
            auto action = Position(q, settings, 1, 1800.0f, 0.4f, 1750.0f,
                                   "KaiSa W", 420);
            if (action.valid) {
                action.collisionRadius = 50.0f;
                action.collisionHits = 1;
                action.maxLead = 340.0f;
                action.requireStableMotion = true;
                action.retryMs = 2500;
                return action;
            }
        }
        // Icathian Rain in the attack backswing while brawling.
        if (settings.kaisaQ && Q.ready && q.retryReady[0] &&
            q.hasAttackTarget && q.distance <= 550.0f && q.aaGapMs >= 55) {
            auto action = Self(q, settings, 0, "KaiSa Q", 45);
            if (action.valid) action.retryMs = 4000;
            return action;
        }
        // E is a movement steroid and R a long dash - both manual.
        return {};
    }


    if (champion == "YUNARA") {
        // Cultivation of Spirit: attack-speed steroid whose hits also spread
        // to nearby enemies, so it is the strongest button for both trading
        // and clearing. Self cast, so it fits in a short pre-AA gap and is
        // checked before the lane-clear split.
        if (settings.yunaraQ && Q.ready && q.retryReady[0] &&
            q.hasAttackTarget && q.aaGapMs >= 55 &&
            q.distance <= q.localAttackRange + 100.0f) {
            auto action = Self(q, settings, 0, "Yunara Q", 45);
            if (action.valid) action.retryMs = 5000;
            return action;
        }

        // Transcendent State replaces W with a laser and E with a dash. The
        // slot name is the only signal for that, so the W branch reads it and
        // switches to an instant beam profile instead of the bead's flight.
        const std::string wName = Upper(W.name);
        const bool transcendentW = !wName.empty() && wName != "YUNARAW";
        const float wSpeed = transcendentW ? 0.0f : 2150.0f;
        const float wLead = transcendentW ? 200.0f : 280.0f;

        if (q.targetIsMinion) {
            // Q above already covers the wave; W adds reach on the back line.
            if (q.hasSpellWindow && settings.yunaraW && W.ready) {
                auto action = Position(q, settings, 1, 1150.0f, 0.25f, wSpeed,
                                       "Yunara W LaneClear", 260);
                if (action.valid) {
                    action.retryMs = 900;
                    return action;
                }
            }
            return {};
        }

        // Transcend One's Self upgrades the whole kit, so commit it to a real
        // sustained fight rather than spending it on cleanup the basic
        // attacks already win - the same rule Vayne R and Twitch R use.
        if (settings.yunaraR && R.ready && R.level > 0 && q.retryReady[3] &&
            q.hasAttackTarget &&
            q.distance <= q.localAttackRange + 65.0f &&
            q.targetHp > q.totalAttackDamage * 2.5f) {
            auto action = Self(q, settings, 3, "Yunara R", 45);
            if (action.valid) action.retryMs = 8000;
            return action;
        }

        // Arc of Judgment / Arc of Ruin: the poke and slow. Bead speed is
        // derived from the Evade database entry (1150 range, 885 ms life).
        // No collision test - the ability is described as hitting enemies
        // rather than stopping on the first one; if it turns out to stop,
        // a collisionRadius of 30 is the single-line change needed.
        if (q.hasSpellWindow && settings.yunaraW && W.ready) {
            auto action = Position(q, settings, 1, 1150.0f, 0.25f, wSpeed,
                                   transcendentW ? "Yunara W Ruin"
                                                 : "Yunara W", 280);
            if (action.valid) {
                action.maxLead = wLead;
                action.requireStableMotion = true;
                return action;
            }
        }
        // E: base form is a self move-speed buff, Transcendent form is a
        // dash whose landing cannot be checked. Manual by default.
        return {};
    }

    return Generic(q, settings);
}

} // namespace ChampionCombo
