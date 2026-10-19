#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <random>
#include <string>

namespace duel {
enum class Action { Attack, Defend, Charge, Ultimate };
enum class Outcome { Ongoing, Player0, Player1, Draw };
struct Rules {
    int initialHp = 5;
    int maxEnergy = 3;
    int ultimateCost = 3;
    int attackDamage = 1;
    int ultimateDamage = 3;
};
struct Fighter { int hp = 5; int energy = 0; };
using State = std::array<Fighter, 2>;
struct RoundResult {
    State before{}, after{};
    std::array<Action, 2> actions{};
    std::array<int, 2> damage{}, energyDelta{};
    std::array<bool, 2> blocked{};
    Outcome outcome = Outcome::Ongoing;
};
struct Resolution {
    std::optional<RoundResult> result;
    std::string error;
    explicit operator bool() const { return result.has_value(); }
};
std::string validateRules(const Rules& rules);
std::string validateAction(const Fighter& fighter, Action action, const Rules& rules);
State initialState(const Rules& rules);
const char* actionName(Action action);
std::optional<Action> parseAction(const std::string& name);
const char* outcomeName(Outcome outcome);
Resolution resolveRound(const State& state, const std::array<Action, 2>& actions, const Rules& rules);
// No hidden-action parameter exists. Call once at the START of a local round.
Action chooseAi(const Fighter& self, const Fighter& opponent, const Rules& rules, std::mt19937& rng);
}
