#include "Battle.h"
#include <algorithm>
#include <stdexcept>

namespace duel {
std::string validateRules(const Rules& r) {
    if (r.initialHp < 1 || r.initialHp > 20) return "initialHp must be 1..20";
    if (r.maxEnergy < 1 || r.maxEnergy > 10 || r.ultimateCost != r.maxEnergy)
        return "maxEnergy must be 1..10 and equal ultimateCost";
    if (r.attackDamage < 1 || r.attackDamage > 20 || r.ultimateDamage < 1 || r.ultimateDamage > 20)
        return "damage must be 1..20";
    return {};
}
std::string validateAction(const Fighter& f, Action a, const Rules& r) {
    if (auto error = validateRules(r); !error.empty()) return error;
    if (f.hp < 1 || f.hp > r.initialHp || f.energy < 0 || f.energy > r.maxEnergy)
        return "invalid or defeated fighter";
    switch (a) {
    case Action::Attack: case Action::Defend: return {};
    case Action::Charge: return f.energy == r.maxEnergy ? "Energy full" : "";
    case Action::Ultimate: return f.energy == r.ultimateCost ? "" : "Need full energy";
    }
    return "unknown action";
}
State initialState(const Rules& r) { return {{{r.initialHp, 0}, {r.initialHp, 0}}}; }
const char* actionName(Action a) {
    switch (a) {
    case Action::Attack: return "attack";
    case Action::Defend: return "defend";
    case Action::Charge: return "charge";
    case Action::Ultimate: return "ultimate";
    }
    return "invalid";
}
std::optional<Action> parseAction(const std::string& s) {
    for (auto a : {Action::Attack, Action::Defend, Action::Charge, Action::Ultimate})
        if (s == actionName(a)) return a;
    return std::nullopt;
}
const char* outcomeName(Outcome o) {
    switch (o) {
    case Outcome::Ongoing: return "ongoing";
    case Outcome::Player0: return "player0";
    case Outcome::Player1: return "player1";
    case Outcome::Draw: return "draw";
    }
    return "invalid";
}
Resolution resolveRound(const State& s, const std::array<Action, 2>& a, const Rules& r) {
    for (int i = 0; i < 2; ++i)
        if (auto error = validateAction(s[i], a[i], r); !error.empty()) return {std::nullopt, error};
    RoundResult out;
    out.before = out.after = s;
    out.actions = a;
    // Compute both sides from the immutable starting snapshot. No early death branch.
    for (int i = 0; i < 2; ++i) {
        const int other = 1 - i;
        out.blocked[i] = a[other] == Action::Attack && a[i] == Action::Defend;
        out.damage[i] = a[other] == Action::Ultimate ? r.ultimateDamage :
            (a[other] == Action::Attack && !out.blocked[i] ? r.attackDamage : 0);
        out.energyDelta[i] = a[i] == Action::Charge ? 1 : (a[i] == Action::Ultimate ? -r.ultimateCost : 0);
        out.after[i] = {std::max(0, s[i].hp - out.damage[i]), s[i].energy + out.energyDelta[i]};
    }
    if (!out.after[0].hp && !out.after[1].hp) out.outcome = Outcome::Draw;
    else if (!out.after[0].hp) out.outcome = Outcome::Player1;
    else if (!out.after[1].hp) out.outcome = Outcome::Player0;
    return {out, {}};
}
Action chooseAi(const Fighter& self, const Fighter& opponent, const Rules& r, std::mt19937& rng) {
    if (!validateAction(self, Action::Attack, r).empty() || !validateAction(opponent, Action::Attack, r).empty())
        throw std::invalid_argument("invalid AI public state");
    const int roll = std::uniform_int_distribution<int>(0, 99)(rng);
    if (self.energy == r.maxEnergy) return roll < 60 ? Action::Ultimate : (roll < 85 ? Action::Attack : Action::Defend);
    if (opponent.energy == r.maxEnergy) return roll < 50 ? Action::Attack : (roll < 90 ? Action::Charge : Action::Defend);
    return roll < 40 ? Action::Attack : (roll < 70 ? Action::Defend : Action::Charge);
}
}
