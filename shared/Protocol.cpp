#include "Protocol.h"
#include <algorithm>
#include <set>
#include <stdexcept>

namespace duel {
int requiredInt(const Json& j, const char* key, int low, int high) {
    if (!j.contains(key) || !j.at(key).is_number_integer() || j.at(key) < low || j.at(key) > high)
        throw std::runtime_error(std::string(key) + " missing or outside allowed integer range");
    return j.at(key).get<int>();
}
bool roomCodeValid(const std::string& s) {
    return s.size() == 6 && std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; });
}
std::string validateRequest(const Json& j) {
    if (!j.is_object() || !j.contains("type") || !j["type"].is_string() || j["type"].get_ref<const std::string&>().size() > 32)
        return "type must be a short string";
    const auto type = j["type"].get<std::string>();
    std::set<std::string> keys{"type"};
    if (type == "join_room") {
        keys.insert("room");
        if (!j.contains("room") || !j["room"].is_string() || !roomCodeValid(j["room"].get<std::string>())) return "room must be six digits";
    } else if (type == "submit") {
        keys.insert("roundId"); keys.insert("action");
        if (!j.contains("roundId") || !j["roundId"].is_number_integer() || j["roundId"] < 1 || j["roundId"] > MaxRoundId)
            return "roundId outside allowed integer range";
        if (!j.contains("action") || !j["action"].is_string() || j["action"].get_ref<const std::string&>().size() > 16 || !parseAction(j["action"].get<std::string>()))
            return "unknown action";
    } else if (type == "rematch") {
        keys.insert("agree");
        if (!j.contains("agree") || !j["agree"].is_boolean()) return "agree must be boolean";
    } else if (type != "create_room" && type != "leave_room" && type != "ping") return "unknown message type";
    if (j.size() != keys.size()) return "unexpected fields (identity and results are server-owned)";
    for (auto it = j.begin(); it != j.end(); ++it) if (!keys.count(it.key())) return "unexpected field";
    return {};
}
Json rulesJson(const Rules& r) {
    return {{"initialHp", r.initialHp}, {"maxEnergy", r.maxEnergy}, {"ultimateCost", r.ultimateCost},
            {"attackDamage", r.attackDamage}, {"ultimateDamage", r.ultimateDamage}};
}
Rules readRules(const Json& j) {
    Rules r{requiredInt(j,"initialHp",1,20), requiredInt(j,"maxEnergy",1,10), requiredInt(j,"ultimateCost",1,10),
            requiredInt(j,"attackDamage",1,20), requiredInt(j,"ultimateDamage",1,20)};
    if (auto e = validateRules(r); !e.empty()) throw std::runtime_error(e);
    return r;
}
Json stateJson(const State& s) { return Json::array({{{"hp",s[0].hp},{"energy",s[0].energy}},{{"hp",s[1].hp},{"energy",s[1].energy}}}); }
State readState(const Json& j, const Rules& r) {
    if (!j.is_array() || j.size()!=2) throw std::runtime_error("state must have two fighters");
    State s;
    for(int i=0;i<2;++i) s[i]={requiredInt(j[i],"hp",0,r.initialHp),requiredInt(j[i],"energy",0,r.maxEnergy)};
    return s;
}
Json resultJson(const RoundResult& r) {
    return {{"before",stateJson(r.before)}, {"state",stateJson(r.after)},
        {"actions",{actionName(r.actions[0]),actionName(r.actions[1])}}, {"damage",r.damage},
        {"energyDelta",r.energyDelta}, {"blocked",r.blocked}, {"outcome",outcomeName(r.outcome)}};
}
RoundResult readResult(const Json& j, const Rules& rules) {
    RoundResult r; r.before=readState(j.at("before"),rules); r.after=readState(j.at("state"),rules);
    const auto& a=j.at("actions");
    if (!a.is_array() || a.size()!=2) throw std::runtime_error("bad actions");
    for(int i=0;i<2;++i) {
        auto parsed=parseAction(a[i].get<std::string>());
        if(!parsed) throw std::runtime_error("bad action");
        r.actions[i]=*parsed;
    }
    r.damage=j.at("damage").get<std::array<int,2>>();
    r.energyDelta=j.at("energyDelta").get<std::array<int,2>>();
    r.blocked=j.at("blocked").get<std::array<bool,2>>();
    const auto outcome=j.at("outcome").get<std::string>();
    if(outcome=="ongoing") r.outcome=Outcome::Ongoing;
    else if(outcome=="draw") r.outcome=Outcome::Draw;
    else if(outcome=="player0") r.outcome=Outcome::Player0;
    else if(outcome=="player1") r.outcome=Outcome::Player1;
    else throw std::runtime_error("bad outcome");
    return r;
}
std::vector<std::uint8_t> frame(const Json& j) {
    const auto body=j.dump();
    if(body.empty() || body.size()>MaxMessage) throw std::runtime_error("message exceeds 64 KiB");
    const auto n=static_cast<std::uint32_t>(body.size());
    std::vector<std::uint8_t> bytes{static_cast<std::uint8_t>(n>>24),static_cast<std::uint8_t>(n>>16),static_cast<std::uint8_t>(n>>8),static_cast<std::uint8_t>(n)};
    bytes.insert(bytes.end(),body.begin(),body.end()); return bytes;
}
std::uint32_t bodyLength(const std::array<std::uint8_t,4>& h) {
    return (std::uint32_t(h[0])<<24)|(std::uint32_t(h[1])<<16)|(std::uint32_t(h[2])<<8)|h[3];
}
}
