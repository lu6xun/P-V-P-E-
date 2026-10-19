#pragma once
#include "Battle.h"
#include <nlohmann/json.hpp>
#include <array>
#include <vector>

namespace duel {
using Json = nlohmann::json;
constexpr std::size_t MaxMessage = 64 * 1024;
constexpr std::int64_t MaxRoundId = 9007199254740991LL;
bool roomCodeValid(const std::string& code);
std::string validateRequest(const Json& message);
Json rulesJson(const Rules& rules);
Rules readRules(const Json& json);
Json stateJson(const State& state);
State readState(const Json& json, const Rules& rules);
Json resultJson(const RoundResult& result);
RoundResult readResult(const Json& json, const Rules& rules);
std::vector<std::uint8_t> frame(const Json& json);
std::uint32_t bodyLength(const std::array<std::uint8_t, 4>& header);
int requiredInt(const Json& json, const char* key, int low, int high);
}
