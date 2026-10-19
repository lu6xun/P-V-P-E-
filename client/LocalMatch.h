#pragma once
#include "Battle.h"
namespace duel {
class LocalMatch {
public:
    explicit LocalMatch(std::uint32_t seed) : rng_(seed) {}
    void reset(Rules rules) { rules_=rules; state_=initialState(rules); round_=0; beginRound(); }
    void beginRound() {
        ++round_;
        ai_=chooseAi(state_[1],state_[0],rules_,rng_);
        submitted_=false;
    }
    Resolution submit(Action player) {
        if(submitted_) return {std::nullopt,"Already submitted"};
        auto resolved=resolveRound(state_,{{player,ai_}},rules_);
        if(resolved) { state_=resolved.result->after; submitted_=true; }
        return resolved;
    }
    const State& state() const { return state_; }
    int round() const { return round_; }
private:
    Rules rules_; State state_{}; int round_=0; Action ai_=Action::Defend;
    bool submitted_=false; std::mt19937 rng_;
};
}
