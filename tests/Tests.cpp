#include "RoomService.h"
#include "LocalMatch.h"
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>

using namespace duel;
#define CHECK(x) do { if(!(x)) throw std::runtime_error(std::string(__FILE__)+":"+std::to_string(__LINE__)+" check failed: " #x); } while(false)
struct Harness {
    Config c;
    std::map<ClientId,std::vector<Json>> sent;
    RoomService service;
    explicit Harness(Config config={}) : c(std::move(config)),service(c,[this](auto id,const auto& j){sent[id].push_back(j);},7) { for(ClientId i=1;i<=4;++i) service.connect(i); }
    Json last(ClientId id,const std::string& type) {
        auto& list=sent[id];
        for(auto it=list.rbegin();it!=list.rend();++it) if(it->at("type")==type) return *it;
        throw std::runtime_error("missing message: "+type);
    }
    std::size_t count(ClientId id,const std::string& type) {
        std::size_t n=0; for(const auto& j:sent[id]) if(j.at("type")==type) ++n; return n;
    }
    void request(ClientId id,const Json& j,Millis now=0) { service.receive(id,j,now); }
    std::string create(ClientId id=1) { request(id,{{"type","create_room"}}); return last(id,"room_created")["room"]; }
    std::int64_t start() { auto code=create(); request(2,{{"type","join_room"},{"room",code}}); return last(1,"match_start")["roundId"]; }
    void act(ClientId id,std::int64_t round,Action a,Millis now=0) { request(id,{{"type","submit"},{"roundId",round},{"action",actionName(a)}},now); }
};
void combinations() {
    // Rows are receiver action, columns are attacker action: A D C U.
    const int incoming[4][4]={{1,0,0,3},{0,0,0,3},{1,0,0,3},{1,0,0,3}};
    for(int a=0;a<4;++a) for(int b=0;b<4;++b) {
        State state{{{5,a==2 ? 2 : 3},{5,b==2 ? 2 : 3}}};
        const auto r=resolveRound(state,{{static_cast<Action>(a),static_cast<Action>(b)}},{});
        CHECK(r); CHECK(r.result->damage[0]==incoming[a][b]); CHECK(r.result->damage[1]==incoming[b][a]);
        CHECK(r.result->after[0].hp==5-incoming[a][b]); CHECK(r.result->after[1].hp==5-incoming[b][a]);
        CHECK(r.result->after[0].energy==(a==3 ? 0 : 3)); CHECK(r.result->after[1].energy==(b==3 ? 0 : 3));
        CHECK(r.result->blocked[0]==(a==1 && b==0)); CHECK(r.result->blocked[1]==(b==1 && a==0));
    }
}
void rules() {
    Rules rule; const State full{{{5,3},{5,0}}};
    CHECK(!resolveRound(full,{{Action::Charge,Action::Attack}},rule));
    CHECK(full[0].hp==5 && full[0].energy==3);
    CHECK(!resolveRound(initialState(rule),{{Action::Ultimate,Action::Attack}},rule));
    auto charged=resolveRound({{{5,2},{5,0}}},{{Action::Charge,Action::Attack}},rule);
    CHECK(charged && charged.result->after[0].energy==3 && charged.result->after[0].hp==4);
    auto ult=resolveRound(charged.result->after,{{Action::Ultimate,Action::Defend}},rule);
    CHECK(ult && ult.result->after[0].energy==0 && ult.result->damage[1]==3);
    auto keep=resolveRound(full,{{Action::Attack,Action::Defend}},rule); CHECK(keep.result->after[0].energy==3);
    auto draw=resolveRound({{{1,0},{1,0}}},{{Action::Attack,Action::Attack}},rule);
    CHECK(draw.result->outcome==Outcome::Draw);
    auto both=resolveRound({{{2,3},{2,3}}},{{Action::Ultimate,Action::Ultimate}},rule);
    CHECK(both.result->outcome==Outcome::Draw && both.result->after[0].energy==0 && both.result->after[1].energy==0);
    auto dead=resolveRound({{{1,3},{3,0}}},{{Action::Ultimate,Action::Attack}},rule); CHECK(dead.result->outcome==Outcome::Draw);
    CHECK(!resolveRound({{{0,0},{5,0}}},{{Action::Attack,Action::Attack}},rule));
    CHECK(!resolveRound(full,{{static_cast<Action>(123),Action::Defend}},rule));
    rule.ultimateCost=2; CHECK(!resolveRound(full,{{Action::Attack,Action::Attack}},rule));
}
void ai() {
    Rules r; std::mt19937 rng(22),same(22);
    for(int energy=0;energy<=3;++energy) for(int enemy=0;enemy<=3;++enemy) for(int n=0;n<500;++n) {
        auto a=chooseAi({5,energy},{5,enemy},r,rng);
        CHECK(validateAction({5,energy},a,r).empty()); CHECK(a==chooseAi({5,energy},{5,enemy},r,same));
    }
    for(unsigned seed=0;seed<100;++seed) {
        LocalMatch left(seed),right(seed); left.reset(r); right.reset(r);
        const auto l=left.submit(Action::Attack), rr=right.submit(Action::Charge);
        CHECK(l.result->actions[1]==rr.result->actions[1]); CHECK(!left.submit(Action::Defend));
    }
    std::array<int,4> counts{};
    for(int i=0;i<10000;++i) ++counts[static_cast<int>(chooseAi({5,3},{5,0},r,rng))];
    CHECK(counts[3]>5700 && counts[3]<6300 && counts[2]==0);
}
void rooms() {
    CHECK(RoomService::formatCode(7)=="000007"); CHECK(roomCodeValid("000000"));
    CHECK(!roomCodeValid("12345") && !roomCodeValid("12x456"));
    Harness h; std::set<std::string> codes;
    for(ClientId id=10;id<100;++id) { h.service.connect(id); auto code=h.create(id); CHECK(codes.insert(code).second); CHECK(code.size()==6); }
    h.request(1,{{"type","join_room"},{"room","999999"}}); CHECK(h.last(1,"error")["code"]=="room_not_found");
    auto code=h.create(); h.request(1,{{"type","join_room"},{"room",code}}); CHECK(h.last(1,"error")["code"]=="already_in_room");
    h.request(2,{{"type","join_room"},{"room",code}}); h.request(3,{{"type","join_room"},{"room",code}});
    CHECK(h.last(3,"error")["code"]=="room_started");
    h.request(2,{{"type","join_room"},{"room",code}}); CHECK(h.last(2,"error")["code"]=="already_in_room");
    CHECK(h.count(1,"match_start")==1);
}
void authority() {
    Harness h; const auto id=h.start();
    h.act(1,id+1,Action::Attack); CHECK(h.last(1,"error")["code"]=="wrong_round");
    h.act(1,id,Action::Ultimate); CHECK(h.last(1,"error")["code"]=="illegal_action");
    h.request(1,{{"type","submit"},{"roundId",id},{"action","attack"},{"playerId",2}});
    CHECK(h.last(1,"error")["code"]=="invalid_message");
    h.sent[2].clear(); h.act(1,id,Action::Charge);
    CHECK(h.sent[2].size()==1); CHECK(h.sent[2][0]==Json({{"type","opponent_submitted"},{"roundId",id}}));
    CHECK(!h.last(1,"submit_ack").contains("action"));
    h.act(1,id,Action::Attack); CHECK(h.last(1,"error")["code"]=="already_submitted");
    h.act(2,id,Action::Attack); CHECK(h.count(1,"round_result")==1);
    auto result=h.last(1,"round_result"); CHECK(result==h.last(2,"round_result"));
    CHECK(result["state"][0]["hp"]==4 && result["state"][0]["energy"]==1);
    h.act(2,id,Action::Attack); CHECK(h.last(2,"error")["code"]=="wrong_phase");
    h.service.tick(2399); CHECK(h.count(1,"round_start")==0);
    h.service.tick(2400); CHECK(h.last(1,"round_start")["roundId"]!=id);
    h.act(1,id,Action::Attack,2401); CHECK(h.last(1,"error")["code"]=="wrong_round");
    CHECK(h.count(1,"round_result")==1);
}
void timeouts() {
    Harness h; const auto id=h.start(); h.act(1,id,Action::Attack,29999); h.service.tick(30000);
    auto r=h.last(1,"round_result"); CHECK(r["actions"]==Json({"attack","defend"})); CHECK(r["damage"]==Json({0,0}));
    CHECK(h.last(2,"submit_ack")["automatic"]==true);
    h.act(2,id,Action::Charge,30000); h.service.tick(30001); CHECK(h.count(1,"round_result")==1);
    Harness late; auto lateId=late.start(); late.act(1,lateId,Action::Attack,30000);
    CHECK(late.last(1,"round_result")["actions"]==Json({"defend","defend"})); CHECK(late.count(1,"round_result")==1);
    Harness order; auto round=order.start(); order.service.tick(30000); order.act(1,round,Action::Attack,30000);
    CHECK(order.last(1,"round_result")==late.last(1,"round_result"));
}
void rematch() {
    Harness h; auto id=h.start(); Millis now=0;
    // Finish with full energy on both sides, proving rematch resets nonzero values.
    for(int i=0;i<8;++i) {
        const auto action=i<3 ? Action::Charge : Action::Attack;
        h.act(1,id,action,now); h.act(2,id,action,now); now+=2400; h.service.tick(now);
        if(i<7) id=h.last(1,"round_start")["roundId"];
    }
    CHECK(h.last(1,"match_finished")["outcome"]=="draw");
    CHECK(h.last(1,"round_result")["state"][0]["energy"]==3);
    h.request(1,{{"type","rematch"},{"agree",true}},now);
    CHECK(h.count(1,"match_start")==1);
    h.request(2,{{"type","rematch"},{"agree",true}},now);
    const auto start=h.last(1,"match_start"); CHECK(start["round"]==1 && start["roundId"]>id);
    CHECK(start["state"]==stateJson(initialState({}))); CHECK(start["submitted"]==Json({false,false}));
    CHECK(start["remainingMs"]==30000); CHECK(h.count(1,"match_start")==2);
    auto newId=start["roundId"].get<std::int64_t>(); h.act(1,newId,Action::Charge,now);
    CHECK(h.last(1,"submit_ack")["automatic"]==false);
    h.service.tick(now+29999); CHECK(h.count(1,"round_result")==8);
    Config quick; quick.rules.initialHp=1;
    Harness decline(quick); auto finalId=decline.start();
    decline.act(1,finalId,Action::Attack); decline.act(2,finalId,Action::Attack); decline.service.tick(2400);
    decline.request(1,{{"type","rematch"},{"agree",false}},2400);
    CHECK(decline.service.roomCount()==0);
    CHECK(decline.last(2,"opponent_left")["reason"]=="Opponent declined rematch");
}
void protocol() {
    for(const auto& j:std::vector<Json>{Json{},Json::array(),Json{{"type",1}},Json{{"type","join_room"},{"room",123456}},
        Json{{"type","submit"},{"roundId",1.1},{"action","attack"}},Json{{"type","submit"},{"roundId",0},{"action","attack"}},
        Json{{"type","submit"},{"roundId",18446744073709551615ULL},{"action","attack"}},Json{{"type","rematch"},{"agree","yes"}},
        Json{{"type","submit"},{"roundId",1},{"action","hack"}},Json{{"type","ping"},{"hp",999}}}) CHECK(!validateRequest(j).empty());
    CHECK(validateRequest({{"type","submit"},{"roundId",1},{"action","attack"}}).empty());
    auto data=frame({{"type","ping"}}); std::array<std::uint8_t,4> header{data[0],data[1],data[2],data[3]};
    CHECK(bodyLength(header)==data.size()-4);
    bool rejected=false; try { frame({{"payload",std::string(MaxMessage,'x')}}); } catch(...) { rejected=true; } CHECK(rejected);
    auto r=resolveRound(initialState({}),{{Action::Charge,Action::Attack}},{});
    CHECK(resultJson(readResult(resultJson(*r.result),{}))==resultJson(*r.result));
}
void cleanup() {
    Harness h; h.start(); h.service.disconnect(1); CHECK(h.service.roomCount()==0);
    CHECK(h.last(2,"opponent_left")["reason"]=="Opponent disconnected");
    h.create(2); h.service.tick(299999); CHECK(h.service.roomCount()==1);
    h.service.tick(300000); CHECK(h.service.roomCount()==0);
    h.create(2); h.request(2,{{"type","leave_room"}}); CHECK(h.service.roomCount()==0);
    h.service.disconnect(2); h.service.disconnect(2); CHECK(h.service.roomCount()==0);
}
void configTest() {
    auto config=loadConfig("assets/config/game.json"); CHECK(config.selectionMs==30000);
    const auto file=std::filesystem::temp_directory_path()/"duel-config-unit-test.json";
    { std::ofstream f(file); f<<"{ broken"; }
    bool rejected=false; try { loadConfig(file); } catch(...) { rejected=true; } CHECK(rejected);
    std::filesystem::remove(file); rejected=false;
    try { loadConfig(file); } catch(...) { rejected=true; } CHECK(rejected);
    Json j; { std::ifstream f("assets/config/game.json"); f>>j; }
    j["rules"]["ultimateCost"]=2; { std::ofstream f(file); f<<j; }
    rejected=false; try { loadConfig(file); } catch(...) { rejected=true; } CHECK(rejected); std::filesystem::remove(file);
}
int main(int argc,char** argv) {
    try {
        std::filesystem::current_path(std::filesystem::absolute(argv[0]).parent_path());
        const std::map<std::string,void(*)()> cases{{"combinations",combinations},{"rules",rules},{"ai",ai},{"rooms",rooms},
            {"authority",authority},{"timeouts",timeouts},{"rematch",rematch},{"protocol",protocol},{"cleanup",cleanup},{"config",configTest}};
        if(argc!=2 || !cases.count(argv[1])) throw std::runtime_error("Choose a test case");
        cases.at(argv[1])(); std::cout<<"PASS "<<argv[1]<<'\n'; return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
