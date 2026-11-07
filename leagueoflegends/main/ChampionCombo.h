#pragma once

#include <array>
#include <string>

namespace ChampionCombo {

enum class Shape { Unknown, Line, Circle, CircleStart, Cone };
enum class CastType { Position, Self };
enum class AimMode { TargetPrediction, AwayFromTarget, TipAim };

struct SpellState {
    int slot = 0;
    int key = 0;
    bool ready = false;
    int level = 0;
    std::string name;
    Shape shape = Shape::Unknown;
    float range = 0.0f;
    int delayMs = 0;
    int activeMs = 0;
};

struct Settings {
    bool genericEnabled = true;
    bool genericUltimate = false;
    bool kogQ = true, kogW = true, kogE = true, kogR = true;
    float kogRHpPct = 40.0f;
    bool ezrealQ = true, ezrealW = true, ezrealR = true;
    float ezrealRHpPct = 20.0f;
    float ezrealRMaxRange = 3000.0f;
    bool caitlynQ = true, caitlynW = true, caitlynE = false, caitlynR = true;
    float caitlynRHpPct = 15.0f;
    float caitlynRMaxRange = 3500.0f;
    bool asheQ = true, asheW = true, asheE = false, asheR = true;
    float asheRHpPct = 20.0f;
    float asheRMaxRange = 3500.0f;
    bool kalistaQ = true, kalistaE = false;
    float kalistaEHpPct = 12.0f;
    bool missFortuneQ = true, missFortuneW = true, missFortuneE = true;
    bool missFortuneR = false;
    float missFortuneRHpPct = 30.0f;
    bool dravenQ = true, dravenW = true, dravenE = true, dravenR = true;
    float dravenRHpPct = 25.0f;
    float dravenRMaxRange = 2500.0f;
    bool zoeQ = true, zoeE = true;
    bool zoeW = false, zoeR = false;
    bool ahriQ = true, ahriW = true, ahriE = true;
    // Spirit Rush changes position and requires wall/turret safety. Keep it
    // manual until the executor has a verified destination check.
    bool ahriR = false;
    bool cassiopeiaQ = true, cassiopeiaW = true, cassiopeiaE = true;
    bool cassiopeiaR = false;
    bool luxQ = true, luxW = false, luxE = true, luxR = true;
    float luxRHpPct = 20.0f;
    float luxRMaxRange = 3340.0f;
    bool zedQ = true, zedW = true, zedE = true;
    // Death Mark is gated by a conservative post-armor kill calculation.
    bool zedR = true;
    float zedRDamageBuffer = 35.0f;
    bool lucianQ = true, lucianW = true;
    // E requires a safe dash destination; R requires channel steering.
    bool lucianE = false, lucianR = false;
    bool yasuoQ = true, yasuoE = true, yasuoR = true;
    bool yasuoW = false;
    float yasuoDashMinImprovement = 120.0f;
    bool vayneQ = true, vayneE = true, vayneR = true;
    float vayneDefensiveEHpPct = 32.0f;
    // Aatrox: Q is a three-stage chain (Q1/Q2/Q3) whose sweet spot is the far
    // edge for Q1/Q2 and the center for Q3. W is a line + tether recast. E is
    // a 300-unit reposition dash used to close into Q range. R is a self buff.
    bool aatroxQ = true, aatroxW = true, aatroxE = true, aatroxR = true;
    float aatroxRHpPct = 40.0f;
    // Xerath R stays manual: the locus overlay changes the control scheme and
    // each recast needs its own aim; automating it mid-orbwalk is unsafe.
    bool xerathQ = true, xerathW = true, xerathE = true;
    // Morgana E needs ally/threat selection and R is a self channel that
    // orbwalker movement would break; both stay manual.
    bool morganaQ = true, morganaW = true;
    // Varus W active only empowers the next Q; leave it to the player.
    bool varusQ = true, varusE = true, varusR = true;
    // Jinx Q is a stance toggle (rockets/minigun state machine) - manual.
    // E traps arm after ~0.7 s and are only reliable on a controlled target,
    // so they default off until enabled deliberately.
    bool jinxW = true, jinxE = false, jinxR = true;
    float jinxRHpPct = 35.0f;
    bool brandQ = true, brandW = true, brandE = true, brandR = true;
    float brandRHpPct = 45.0f;
    // Vel'Koz Q splits at a manually-timed reactivation; R is a steered
    // channel - both stay manual.
    bool velkozW = true, velkozE = true;
    // Karthus W/E/R are utility/toggle/global-channel; Q is the whole loop.
    bool karthusQ = true;
    // Nilah E is a dash onto a unit; landing safety is unverified - manual.
    bool nilahQ = true, nilahW = true, nilahR = true;
    bool nilahE = false;
    float nilahWHpPct = 45.0f;
    // Syndra W is a two-stage grab/throw automated via the auxiliary-target
    // path (grabs the nearest enemy minion, throws at the champion). E has no
    // sphere tracking, so it is used as a close-range knockback peel only -
    // the sphere-stun lineup stays with the player.
    bool syndraQ = true, syndraW = true, syndraE = true, syndraR = true;
    float syndraRHpPct = 55.0f;
    // Ziggs W repositions himself or enemies - manual.
    bool ziggsQ = true, ziggsE = true, ziggsR = true;
    float ziggsRHpPct = 25.0f;
    float ziggsRMaxRange = 2500.0f;
    // Tristana W is a jump - manual.
    bool tristanaQ = true, tristanaE = true, tristanaR = true;
    float tristanaRHpPct = 25.0f;
    // Sivir E spellshield needs threat context, R is a team steroid - manual.
    bool sivirQ = true, sivirW = true;
    // Seraphine W shields allies and R is a global engage line - manual.
    bool seraphineQ = true, seraphineE = true;
    // Swain W needs vision judgment, R is a self drain state - manual.
    bool swainQ = true, swainE = true;
    // Twitch Q stealth is an engage decision - manual.
    bool twitchW = true, twitchE = true, twitchR = true;
    // Kai'Sa E is a movement steroid and R a dash - manual.
    bool kaisaQ = true, kaisaW = true;
    // Yunara: Q is a self attack steroid whose hits spread, W the only
    // skillshot, R the Transcendent State buff that upgrades the kit.
    // E is a self move-speed buff in the base form but a dash while
    // Transcendent, and that landing cannot be verified - manual.
    bool yunaraQ = true, yunaraW = true, yunaraR = true;
    bool yunaraE = false;
    int retryQMs = 900, retryWMs = 4000, retryEMs = 1500, retryRMs = 750;
};

struct Query {
    std::string champion;
    float distance = 0.0f;
    float targetHpPct = 100.0f;
    float localHpPct = 100.0f;
    float targetHp = 0.0f;
    float targetArmor = 0.0f;
    float totalAttackDamage = 0.0f;
    float bonusAttackDamage = 0.0f;
    float localAttackRange = 0.0f;
    int aaGapMs = 0;
    bool hasAttackTarget = false;
    bool hasSpellWindow = false;
    bool targetIsMinion = false;
    // The selected minion is inside the last-hit window, so the cast is meant
    // to secure that specific creep rather than to push the wave.
    bool targetIsSecureMinion = false;
    int kalistaRendStacks = 0;
    float kalistaRendDamage = 0.0f;
    float kalistaRendTargetHp = 0.0f;
    bool kalistaRendKillable = false;
    bool kalistaRendEstimated = false;
    // Twitch Contaminate execute, filled by the executor from the target's
    // DeadlyVenom stacks (same pattern as the Kalista Rend evaluation).
    int twitchVenomStacks = 0;
    float twitchEDamage = 0.0f;
    bool twitchEKillable = false;
    bool targetAirborne = false;
    bool yasuoEqPending = false;
    bool vayneCondemnWall = false;
    float vayneCondemnWallDistance = 0.0f;
    bool hasAuxTarget = false;
    float auxTargetDistance = 0.0f;
    std::array<SpellState, 4> spells{};
    std::array<bool, 4> retryReady{{true, true, true, true}};
};

struct Action {
    bool valid = false;
    int slot = -1;
    int key = 0;
    CastType castType = CastType::Position;
    AimMode aimMode = AimMode::TargetPrediction;
    float aimDistance = 0.0f;
    float range = 0.0f;
    float delaySeconds = 0.0f;
    float projectileSpeed = 0.0f;
    int retryMs = 0;
    int lockMs = 0;
    float estimatedDamage = 0.0f;
    bool useAuxTarget = false;
    // Upper bound on how far ahead of the target the aim point may be moved.
    // Zero leaves the analytic intercept uncapped, which is only correct for a
    // spell whose flight is short enough that a turn cannot invalidate it.
    float maxLead = 0.0f;
    // Require two consecutive motion samples agreeing on direction before
    // committing. Used for long-flight spells where a stale path is a certain
    // miss rather than a near miss.
    bool requireStableMotion = false;
    // Missile radius for the minion-collision test, and how many blockers must
    // be in the way before the cast is held. Zero radius disables the test.
    float collisionRadius = 0.0f;
    int collisionHits = 0;
    // Charged spells (Xerath/Varus Q): hold the key down this long before
    // releasing. Zero uses the normal quick tap. The executor thread blocks
    // for the whole hold, so profiles must keep this under ~1000 ms.
    int castHoldMs = 0;
    std::string label;
};

bool Supports(const std::string& champion);
bool IsSpecialProfile(const std::string& champion);
float ScanRange(const std::string& champion, const Settings& settings);
// Effective non-ultimate poke reach. Used by the mixed-mode target scan to
// decide when an enemy champion is close enough to outrank the wave.
float HarassRange(const std::string& champion);
bool AcceptTarget(const std::string& champion, float distance,
                  float hpPct, const Settings& settings);
float TargetScoreBonus(const std::string& champion, float distance);
Action Choose(const Query& query, const Settings& settings);

} // namespace ChampionCombo
