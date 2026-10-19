#include "RoomService.h"
#include <iomanip>
#include <sstream>

namespace duel {
RoomService::RoomService(Config config,Sender send,std::uint32_t seed) : config_(std::move(config)),send_(std::move(send)),rng_(seed) {}
void RoomService::connect(ClientId id) { membership_.emplace(id,""); }
void RoomService::error(ClientId id,const std::string& code,const std::string& message) { send_(id,{{"type","error"},{"code",code},{"message",message}}); }
void RoomService::broadcast(const Room& r,const Json& j) { for(auto id:r.players) if(id) send_(id,j); }
std::string RoomService::formatCode(unsigned n) {
    std::ostringstream out; out<<std::setw(6)<<std::setfill('0')<<(n%1000000); return out.str();
}
std::string RoomService::allocateCode() {
    auto n=std::uniform_int_distribution<unsigned>(0,999999)(rng_);
    for(unsigned i=0;i<1000000;++i) { auto code=formatCode(n+i); if(!rooms_.count(code)) return code; }
    return {};
}
void RoomService::removeRoom(const std::string& code,ClientId leaving,const std::string& reason) {
    const auto found=rooms_.find(code); if(found==rooms_.end()) return;
    // Copy recipients and erase first: no dangling references on callback re-entry.
    const auto players=found->second.players; rooms_.erase(found);
    for(auto id:players) if(id && membership_.count(id)) {
        membership_[id].clear();
        send_(id,{{"type",id==leaving ? "room_left" : "opponent_left"},{"reason",reason}});
    }
}
void RoomService::disconnect(ClientId id) {
    auto found=membership_.find(id); if(found==membership_.end()) return;
    const auto code=found->second;
    membership_.erase(found);
    if(!code.empty()) removeRoom(code,id,"Opponent disconnected");
}
void RoomService::startMatch(Room& r,Millis now) {
    r.state=initialState(config_.rules); r.actions={}; r.rematch={}; r.round=0; r.deadline=0;
    r.outcome=Outcome::Ongoing; startRound(r,now,true);
}
void RoomService::startRound(Room& r,Millis now,bool first) {
    r.phase=RoomPhase::Selecting; r.actions={}; ++r.round; r.roundId=nextRoundId_++;
    r.deadline=now+config_.selectionMs;
    Json j={{"type",first ? "match_start" : "round_start"},{"room",r.code},{"round",r.round},{"roundId",r.roundId},
            {"state",stateJson(r.state)},{"remainingMs",config_.selectionMs},{"submitted",{false,false}}};
    if(first) { j["rules"]=rulesJson(config_.rules); j["timing"]=timingJson(config_); }
    broadcast(r,j);
}
void RoomService::submit(Room& r,int player,Action a,bool automatic,Millis now) {
    if(r.phase!=RoomPhase::Selecting || r.actions[player]) return;
    r.actions[player]=a;
    send_(r.players[player],{{"type","submit_ack"},{"roundId",r.roundId},{"automatic",automatic}});
    // NEVER include the selected action in an acknowledgement or ready notification.
    send_(r.players[1-player],{{"type","opponent_submitted"},{"roundId",r.roundId}});
    if(!r.actions[0] || !r.actions[1]) return;
    const auto resolved=resolveRound(r.state,{{*r.actions[0],*r.actions[1]}},config_.rules);
    if(!resolved) throw std::logic_error(resolved.error);
    const auto& result=*resolved.result;
    r.state=result.after; r.outcome=result.outcome; r.phase=RoomPhase::Showing;
    r.deadline=now+config_.revealMs+config_.displayMs;
    Json message=resultJson(result);
    message.update({{"type","round_result"},{"room",r.code},{"round",r.round},{"roundId",r.roundId},
        {"revealMs",config_.revealMs},{"displayMs",config_.displayMs}});
    broadcast(r,message);
}
void RoomService::advance(Room& r,Millis now) {
    if(now<r.deadline) return;
    if(r.phase==RoomPhase::Selecting) {
        for(int i=0;i<2;++i) if(!r.actions[i]) submit(r,i,Action::Defend,true,now);
    } else if(r.phase==RoomPhase::Showing) {
        if(r.outcome==Outcome::Ongoing) startRound(r,now,false);
        else { r.phase=RoomPhase::Finished; broadcast(r,{{"type","match_finished"},{"room",r.code},{"outcome",outcomeName(r.outcome)}}); }
    }
}
void RoomService::tick(Millis now) {
    std::vector<std::string> expired;
    for(auto& [code,r]:rooms_) {
        if(r.phase==RoomPhase::Waiting && now>=r.deadline) expired.push_back(code);
        else advance(r,now);
    }
    for(const auto& code:expired) removeRoom(code,0,"Waiting room expired");
}
void RoomService::receive(ClientId id,const Json& j,Millis now) {
    if(!membership_.count(id)) return;
    if(auto problem=validateRequest(j); !problem.empty()) { error(id,"invalid_message",problem); return; }
    const auto type=j.at("type").get<std::string>();
    if(type=="ping") { send_(id,{{"type","pong"}}); return; }
    auto& code=membership_.at(id);
    if(type=="create_room" || type=="join_room") {
        if(!code.empty()) { error(id,"already_in_room","Leave your current room first"); return; }
        if(type=="create_room") {
            if(rooms_.size()>=128) { error(id,"server_full","Server room limit reached"); return; }
            Room r; r.code=allocateCode(); r.players[0]=id; r.deadline=now+config_.waitingRoomMs;
            code=r.code; rooms_.emplace(code,r);
            send_(id,{{"type","room_created"},{"room",code},{"seat",0}});
            send_(id,{{"type","room_state"},{"room",code},{"players",1},{"phase","waiting"}});
        } else {
            const auto target=j.at("room").get<std::string>(); auto found=rooms_.find(target);
            if(found==rooms_.end()) { error(id,"room_not_found","Room does not exist or has expired"); return; }
            auto& r=found->second;
            if(r.phase!=RoomPhase::Waiting) { error(id,"room_started","Room already started (two seats occupied)"); return; }
            if(r.players[1]) { error(id,"room_full","Room is full"); return; }
            r.players[1]=id; code=target;
            send_(id,{{"type","room_joined"},{"room",code},{"seat",1}});
            broadcast(r,{{"type","room_state"},{"room",code},{"players",2},{"phase","starting"}});
            startMatch(r,now);
        }
        return;
    }
    if(code.empty()) { error(id,"not_in_room","Not in a room"); return; }
    const auto roomCode=code;
    auto& r=rooms_.at(code);
    const int seat=r.players[0]==id ? 0 : 1;
    if(type=="leave_room") { removeRoom(roomCode,id,"Opponent left the room"); return; }
    // Deadline checked even between timer ticks: late packets cannot beat timeout.
    advance(r,now);
    if(type=="submit") {
        if(j.at("roundId").get<std::int64_t>()!=r.roundId) { error(id,"wrong_round","Stale or future roundId"); return; }
        if(r.phase!=RoomPhase::Selecting) { error(id,"wrong_phase","Round is not accepting actions"); return; }
        if(r.actions[seat]) { error(id,"already_submitted","Action already locked"); return; }
        const auto a=*parseAction(j.at("action").get<std::string>());
        if(auto e=validateAction(r.state[seat],a,config_.rules); !e.empty()) { error(id,"illegal_action",e); return; }
        submit(r,seat,a,false,now);
    } else if(type=="rematch") {
        if(r.phase!=RoomPhase::Finished) { error(id,"wrong_phase","Rematch requires a finished game"); return; }
        if(!j.at("agree").get<bool>()) { removeRoom(roomCode,id,"Opponent declined rematch"); return; }
        if(r.rematch[seat]) { error(id,"already_requested","Rematch already requested"); return; }
        r.rematch[seat]=true;
        broadcast(r,{{"type","rematch_state"},{"accepted",r.rematch}});
        if(r.rematch[0] && r.rematch[1]) startMatch(r,now);
    }
}
}
