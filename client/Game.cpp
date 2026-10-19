#include "Game.h"
#include <raylib.h>
#include <algorithm>
#include <cmath>
#include <sstream>

namespace duel {
namespace {
constexpr Color Bg{12,18,31,255}, Panel{22,32,49,255}, Border{43,60,80,255};
constexpr Color Text{231,240,249,255}, Muted{144,164,186,255}, Cyan{73,222,205,255}, Purple{183,151,255,255};
constexpr Color Red{255,111,127,255};
void label(const std::string& s,int x,int y,int size,Color c=Text) { DrawText(s.c_str(),x,y,size,c); }
void center(const std::string& s,int x,int y,int size,Color c=Text) { label(s,x-MeasureText(s.c_str(),size)/2,y,size,c); }
bool button(Rectangle r,const std::string& title,bool enabled=true,bool chosen=false,Color accent=Cyan) {
    const bool hover=CheckCollisionPointRec(GetMousePosition(),r);
    DrawRectangleRounded(r,0.15f,8,chosen ? Color{38,77,83,255} : Panel);
    DrawRectangleRoundedLinesEx(r,0.15f,8,chosen || (hover&&enabled) ? 2.0f : 1.0f,enabled&&(chosen||hover) ? accent : Border);
    center(title,static_cast<int>(r.x+r.width/2),static_cast<int>(r.y+r.height/2-9),18,enabled ? Text : Muted);
    return enabled && hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}
void field(Rectangle r,const std::string& title,const std::string& value,bool active) {
    label(title,static_cast<int>(r.x),static_cast<int>(r.y-24),16,Muted);
    DrawRectangleRounded(r,0.12f,6,Panel);
    DrawRectangleRoundedLinesEx(r,0.12f,6,1,active ? Cyan : Border);
    BeginScissorMode(static_cast<int>(r.x+8),static_cast<int>(r.y),static_cast<int>(r.width-16),static_cast<int>(r.height));
    const std::string visible=value+(active && static_cast<int>(GetTime()*2)%2==0 ? "_" : "");
    const int width=MeasureText(visible.c_str(),20);
    const int offset=active ? std::min(0,static_cast<int>(r.width)-24-width) : 0;
    label(visible,static_cast<int>(r.x)+12+offset,static_cast<int>(r.y)+14,20);
    EndScissorMode();
}
void wrapped(const std::string& text,int x,int y,int width,int size,Color color=Muted) {
    std::istringstream words(text); std::string word,line;
    while(words>>word) {
        if(!line.empty() && MeasureText((line+" "+word).c_str(),size)>width) { label(line,x,y,size,color); y+=size+6; line.clear(); }
        line+=(line.empty()?"":" ")+word;
    }
    if(!line.empty()) label(line,x,y,size,color);
}
const char* title(Action a) {
    switch(a) { case Action::Attack:return "ATTACK"; case Action::Defend:return "DEFEND"; case Action::Charge:return "CHARGE"; case Action::Ultimate:return "ULTIMATE"; }
    return "?";
}
std::string signedNumber(int n) { return (n>=0 ? "+" : "")+std::to_string(n); }
bool inputAllowed(int fieldIndex,int c) {
    if(fieldIndex!=0) return c>='0' && c<='9';
    return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='.'||c=='-'||c==':';
}
std::size_t inputLimit(int fieldIndex) { return fieldIndex==0 ? 253u : (fieldIndex==1 ? 5u : 6u); }
}
Game::Game(Config c,std::uint32_t seed) : config_(std::move(c)),rules_(config_.rules),local_(seed),
    host_(config_.defaultHost),port_(std::to_string(config_.defaultPort)) { state_=initialState(rules_); }
void Game::menu() {
    if(network_) {
        if(screen_==Screen::Connecting || screen_==Screen::ConnectionError) network_.reset();
        else { network_->send({{"type","leave_room"}}); closingNetwork_=true; }
    }
    screen_=Screen::Menu; online_=false; selected_.reset(); result_.reset();
    room_.clear(); message_.clear(); logs_.clear(); rules_=config_.rules;
}
void Game::startLocal() {
    online_=false; rules_=config_.rules; local_.reset(rules_); state_=local_.state(); seat_=0;
    round_=1; selected_.reset(); result_.reset(); logs_.clear(); message_.clear(); screen_=Screen::LocalSelecting;
    deadline_=GetTime()+config_.selectionMs/1000.0;
}
void Game::connect(bool create) {
    if(host_.empty() || host_.size()>253 || host_.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-:[]")!=std::string::npos) { message_="Enter a valid host name or IP address"; return; }
    int port=0; try { port=std::stoi(port_); } catch(...) {}
    if(port<1 || port>65535) { message_="Port must be 1..65535"; return; }
    if(!create && !roomCodeValid(roomInput_)) { message_="Room code must contain exactly 6 digits"; return; }
    online_=true; create_=create; closingNetwork_=false; message_.clear(); screen_=Screen::Connecting;
    // Asio DNS, connect, reads, writes and heartbeats all run off the render thread.
    network_=std::make_unique<Network>(host_,port,config_);
}
void Game::submit() {
    if(!selected_) return;
    if(screen_==Screen::LocalSelecting) {
        const auto r=local_.submit(*selected_);
        if(r) reveal(*r.result,config_.revealMs,config_.displayMs); else message_=r.error;
    } else if(screen_==Screen::OnlineSelecting) {
        network_->send({{"type","submit"},{"roundId",roundId_},{"action",actionName(*selected_)}});
        screen_=Screen::Submitted; acked_=false;
    }
}
void Game::beginRound(const Json& j,bool first) {
    if(first) { rules_=readRules(j.at("rules")); logs_.clear(); rematchSent_=false; rematch_={}; }
    state_=readState(j.at("state"),rules_); round_=requiredInt(j,"round",1,2147483647);
    roundId_=j.at("roundId").get<std::int64_t>();
    if(roundId_<1 || roundId_>MaxRoundId) throw std::runtime_error("Invalid server roundId");
    room_=j.at("room").get<std::string>();
    deadline_=GetTime()+requiredInt(j,"remainingMs",0,300000)/1000.0;
    selected_.reset(); result_.reset(); opponentReady_=false; acked_=false; serverFinished_=false;
    message_.clear(); screen_=Screen::OnlineSelecting;
}
void Game::reveal(RoundResult r,int revealMs,int displayMs) {
    result_=std::move(r); state_=result_->before; screen_=Screen::Reveal;
    phaseDeadline_=GetTime()+revealMs/1000.0; displayMs_=displayMs; message_.clear();
}
void Game::applySettlement() { state_=result_->after; screen_=Screen::Settlement; phaseDeadline_=GetTime()+displayMs_/1000.0; addLog(); }
void Game::addLog() {
    const int other=1-seat_; const auto& r=*result_;
    std::ostringstream s; s<<"R"<<round_<<"  "<<actionName(r.actions[seat_])<<" / "<<actionName(r.actions[other])
        <<"   HP -"<<r.damage[seat_]<<" / -"<<r.damage[other]<<"   E "<<signedNumber(r.energyDelta[seat_])<<" / "<<signedNumber(r.energyDelta[other])
        <<"   BLOCK "<<(r.blocked[seat_] ? "Y" : "N")<<" / "<<(r.blocked[other] ? "Y" : "N");
    logs_.push_front(s.str()); if(logs_.size()>3) logs_.pop_back();
}
void Game::event(const Json& j) {
    const auto t=j.at("type").get<std::string>();
    if(t=="connected") network_->send(create_ ? Json{{"type","create_room"}} : Json{{"type","join_room"},{"room",roomInput_}});
    else if(t=="room_created" || t=="room_joined") {
        room_=j.at("room").get<std::string>(); seat_=requiredInt(j,"seat",0,1); screen_=Screen::RoomWaiting;
    } else if(t=="match_start") beginRound(j,true);
    else if(t=="round_start") {
        // Server phase wins over animation time, including after a stalled window.
        if(screen_==Screen::Reveal) applySettlement();
        beginRound(j,false);
    } else if(t=="submit_ack" && j.at("roundId")==roundId_) { acked_=true; if(screen_==Screen::OnlineSelecting) screen_=Screen::Submitted; }
    else if(t=="opponent_submitted" && j.at("roundId")==roundId_) opponentReady_=true;
    else if(t=="round_result") {
        if(j.at("roundId")!=roundId_ || j.at("room")!=room_) throw std::runtime_error("Unexpected result round");
        reveal(readResult(j,rules_),requiredInt(j,"revealMs",50,10000),requiredInt(j,"displayMs",50,10000));
    } else if(t=="match_finished") serverFinished_=true;
    else if(t=="rematch_state") rematch_=j.at("accepted").get<std::array<bool,2>>();
    else if(t=="opponent_left" || t=="room_left") { message_=j.at("reason").get<std::string>(); screen_=Screen::ConnectionError; }
    else if(t=="network_error") { message_=j.at("message").get<std::string>(); screen_=Screen::ConnectionError; }
    else if(t=="error") {
        message_=j.at("message").get<std::string>();
        if(screen_==Screen::Connecting || screen_==Screen::RoomWaiting) screen_=Screen::ConnectionError;
        else if(screen_==Screen::Submitted && !acked_ && j.at("code")=="illegal_action") screen_=Screen::OnlineSelecting;
    }
}
void Game::update() {
    if(network_) for(const auto& j:network_->poll()) {
        if(closingNetwork_) {
            if(j.at("type")=="room_left" || j.at("type")=="error" || j.at("type")=="network_error") network_.reset();
            continue;
        }
        try { event(j); } catch(const std::exception& e) { message_=std::string("Invalid server message: ")+e.what(); screen_=Screen::ConnectionError; }
    }
    const auto now=GetTime();
    if(screen_==Screen::Reveal && now>=phaseDeadline_) applySettlement();
    else if(screen_==Screen::Settlement && now>=phaseDeadline_) {
        if(result_->outcome!=Outcome::Ongoing) screen_=Screen::Finished;
        else if(!online_) {
            local_.beginRound(); round_=local_.round(); selected_.reset(); result_.reset(); screen_=Screen::LocalSelecting;
            deadline_=now+config_.selectionMs/1000.0;
        }
    }
    if(screen_==Screen::LocalSelecting && now>=deadline_) { selected_=Action::Defend; submit(); }
    // Drain press EVENTS so a fast press+release within one frame is not lost.
    // This also preserves 1,2,Enter ordering when several keys arrive together.
    bool control=IsKeyDown(KEY_LEFT_CONTROL)||IsKeyDown(KEY_RIGHT_CONTROL);
    for(int key=GetKeyPressed();key;key=GetKeyPressed()) {
        if(key==KEY_LEFT_CONTROL || key==KEY_RIGHT_CONTROL) control=true;
        if(key==KEY_ESCAPE) { if(screen_==Screen::Menu) quit_=true; else menu(); return; }
        if(screen_==Screen::Menu) {
            if(key==KEY_TAB) activeInput_=(activeInput_+1)%3;
            auto& value=activeInput_==0 ? host_ : (activeInput_==1 ? port_ : roomInput_);
            if(key==KEY_BACKSPACE && !value.empty()) value.pop_back();
            if(key==KEY_V && control) {
                if(const char* clipboard=GetClipboardText()) {
                    value.clear();
                    for(const char* p=clipboard;*p && value.size()<inputLimit(activeInput_);++p)
                        if(inputAllowed(activeInput_,static_cast<unsigned char>(*p))) value+=*p;
                }
            }
        } else if(screen_==Screen::LocalSelecting || screen_==Screen::OnlineSelecting) {
            if(key>=KEY_ONE && key<=KEY_FOUR) {
                const auto a=static_cast<Action>(key-KEY_ONE);
                if(validateAction(state_[seat_],a,rules_).empty()) selected_=a;
            }
            if(key==KEY_ENTER) submit();
        }
    }
    if(screen_==Screen::Menu) {
        auto& value=activeInput_==0 ? host_ : (activeInput_==1 ? port_ : roomInput_);
        const auto limit=inputLimit(activeInput_);
        for(int c=GetCharPressed();c;c=GetCharPressed()) {
            if(inputAllowed(activeInput_,c) && value.size()<limit && !control) value+=static_cast<char>(c);
        }
        if(IsKeyPressedRepeat(KEY_BACKSPACE)&&!value.empty()) value.pop_back();
    }
}
void Game::drawMenu() {
    label("SIMULTANEOUS",56,55,20,Cyan); label("DUEL",52,84,74);
    label("Choose in secret. Resolve together.",56,173,19,Muted);
    DrawLine(56,214,904,214,Border);
    DrawCircleLines(755,111,64,Border); DrawPoly({755,111},6,44,30,Cyan); DrawPoly({755,111},6,24,30,Bg);
    const Rectangle host{56,269,360,48},port{434,269,120,48},code{578,269,326,48};
    const Rectangle fields[]={host,port,code};
    for(int i=0;i<3;++i) if(IsMouseButtonPressed(MOUSE_BUTTON_LEFT)&&CheckCollisionPointRec(GetMousePosition(),fields[i])) activeInput_=i;
    field(host,"SERVER ADDRESS",host_,activeInput_==0); field(port,"PORT",port_,activeInput_==1); field(code,"6-DIGIT ROOM CODE",roomInput_,activeInput_==2);
    if(button({56,350,410,60},"Play vs AI")) startLocal();
    if(button({494,350,410,60},"Create Room")) connect(true);
    if(button({56,430,410,60},"Join Room")) connect(false);
    if(button({494,430,410,60},"Exit")) quit_=true;
    wrapped(message_.empty() ? "1-4 to choose  |  Enter to confirm  |  Esc to return\n" : message_,56,523,848,18,message_.empty() ? Muted : Red);
    label(std::to_string(config_.rules.initialHp)+" HEALTH   /   "+std::to_string(config_.rules.maxEnergy)+" ENERGY   /   ONE SIMULTANEOUS ROUND",56,593,16,Muted);
}
void Game::drawBattle() {
    const int other=1-seat_;
    label(online_ ? "ONLINE DUEL  /  ROOM "+room_ : "PRACTICE  /  VS AI",28,23,18,Cyan);
    center("ROUND "+std::to_string(round_),480,23,20);
    if(button({817,12,115,38},"Menu")) { menu(); return; }
    const bool selecting=screen_==Screen::LocalSelecting || screen_==Screen::OnlineSelecting;
    const bool revealed=result_.has_value() && (screen_==Screen::Reveal || screen_==Screen::Settlement || screen_==Screen::Finished);
    const int remaining=static_cast<int>(std::ceil(std::max(0.0,deadline_-GetTime())));
    for(int side=0;side<2;++side) {
        const int player=side==0 ? seat_ : other; const int x=side==0 ? 28 : 508; const Color accent=side==0 ? Cyan : Purple;
        DrawRectangleRounded({static_cast<float>(x),70,424,220},0.08f,8,Panel);
        label(side==0 ? "YOU" : (online_ ? "OPPONENT" : "AI"),x+20,87,18,accent);
        label("HP "+std::to_string(state_[player].hp)+" / "+std::to_string(rules_.initialHp),x+265,87,18);
        DrawRectangle(x+20,119,384,9,Border);
        DrawRectangle(x+20,119,384*state_[player].hp/rules_.initialHp,9,accent);
        const Vector2 p{static_cast<float>(x+70),185};
        DrawPoly(p,side==0 ? 4 : 6,35,side==0 ? 45.0f : 30.0f,accent);
        DrawPoly(p,side==0 ? 4 : 6,24,side==0 ? 45.0f : 30.0f,Panel);
        DrawCircle(static_cast<int>(p.x)-9,static_cast<int>(p.y)-3,3,Text); DrawCircle(static_cast<int>(p.x)+9,static_cast<int>(p.y)-3,3,Text);
        label("ENERGY",x+128,151,14,Muted);
        const int cell=std::min(44,240/rules_.maxEnergy);
        for(int i=0;i<rules_.maxEnergy;++i) DrawRectangleRounded({static_cast<float>(x+128+i*(cell+3)),175,static_cast<float>(cell),24},0.12f,4,state_[player].energy>i ? accent : Border);
        std::string status=side==0 ? (selected_ ? title(*selected_) : "Choosing...") : "Hidden";
        if(!revealed && side==1 && online_ && opponentReady_) status="Submitted / hidden";
        if(!revealed && side==0 && screen_==Screen::Submitted) status=acked_ ? "Locked / waiting" : "Sending...";
        if(revealed) status=title(result_->actions[player]);
        label(status,x+20,237,20,revealed ? accent : Muted);
        if(revealed && screen_!=Screen::Reveal) {
            std::string effect="HP -"+std::to_string(result_->damage[player])+"  E "+signedNumber(result_->energyDelta[player]);
            if(result_->blocked[player]) effect+="  BLOCK";
            label(effect,x+180,242,16,result_->damage[player] ? Red : accent);
        }
    }
    center("VS",480,175,20,Muted);
    std::string status;
    if(selecting) status="Choose your action  /  "+std::to_string(remaining)+"s";
    else if(screen_==Screen::Submitted) status=acked_ ? "Action locked. Waiting for opponent..." : "Waiting for server acknowledgement...";
    else if(screen_==Screen::Reveal) status="Both actions revealed";
    else if(screen_==Screen::Settlement) status="Round resolved";
    else if(screen_==Screen::Finished) {
        const auto o=result_->outcome;
        const bool won=(seat_==0 && o==Outcome::Player0)||(seat_==1 && o==Outcome::Player1);
        status=o==Outcome::Draw ? "DRAW / simultaneous knockout" : (won ? "VICTORY" : "DEFEAT");
    }
    center(status,480,308,23,screen_==Screen::Finished ? Cyan : Text);
    if(screen_==Screen::Finished) {
        if(online_) {
            if(button({28,359,440,54},rematchSent_ ? "Rematch requested" : "Play again",serverFinished_&&!rematchSent_)) {
                network_->send({{"type","rematch"},{"agree",true}}); rematchSent_=true;
            }
            if(button({492,359,440,54},"Decline / return to menu")) { network_->send({{"type","rematch"},{"agree",false}}); menu(); return; }
            center(rematch_[other] ? "Opponent wants a rematch" : (rematchSent_ ? "Waiting for opponent approval" : "Both players must agree to restart"),480,433,17,Muted);
        } else {
            if(button({28,359,440,54},"Play again")) startLocal();
            if(button({492,359,440,54},"Main menu")) { menu(); return; }
        }
    } else {
        const std::string hints[]={std::to_string(rules_.attackDamage)+" damage / blocked","Blocks basic attack","+1 energy",std::to_string(rules_.ultimateDamage)+" damage / pierces"};
        for(int i=0;i<4;++i) {
            const auto a=static_cast<Action>(i); const auto reason=validateAction(state_[seat_],a,rules_);
            const int x=28+i*183;
            if(button({static_cast<float>(x),357,171,58},std::to_string(i+1)+"  "+title(a),selecting&&reason.empty(),selected_==a)) selected_=a;
            center(reason.empty() ? hints[i] : reason,x+85,427,14,reason.empty() ? Muted : Red);
        }
        if(button({760,357,172,58},"CONFIRM",selecting&&selected_.has_value())) submit();
        center("Enter",846,427,14,Muted);
    }
    DrawLine(28,464,932,464,Border); label("ROUND HISTORY   /   YOU : OPPONENT",28,482,14,Muted);
    int y=510; for(const auto& line:logs_) { label(line,28,y,16); y+=25; }
    if(logs_.empty()) label("Actions stay secret until both players submit.",28,518,17,Muted);
    if(!message_.empty()) wrapped(message_,28,595,904,14,Red);
    else label("Attack is free. Charge persists. Ultimate pierces defense. Timeout = Defend.",28,603,14,Muted);
}
void Game::draw() {
    ClearBackground(Bg);
    if(screen_==Screen::Menu) { drawMenu(); return; }
    if(screen_==Screen::Connecting || screen_==Screen::RoomWaiting || screen_==Screen::ConnectionError) {
        center(screen_==Screen::RoomWaiting ? "ROOM READY" : (screen_==Screen::Connecting ? "CONNECTING" : "CONNECTION ENDED"),480,155,34,Cyan);
        if(screen_==Screen::RoomWaiting) {
            center(room_,480,236,68); center("Share this code with a player on the same server.",480,335,19,Muted);
            center("Waiting for player two...",480,371,20);
            if(button({270,426,420,52},"Copy room code")) { SetClipboardText(room_.c_str()); message_="Room code copied"; }
            center(message_,480,495,17,Muted);
        } else if(screen_==Screen::ConnectionError) wrapped(message_,150,262,660,22,Red);
        else center(host_+":"+port_,480,270,22,Muted);
        if(button({270,543,420,52},"Return to menu")) menu();
    } else drawBattle();
}
void Game::preview(const std::string& scene) {
    if(scene=="menu") return;
    if(scene=="room") { screen_=Screen::RoomWaiting; online_=true; room_="004291"; return; }
    startLocal();
    if(scene=="battle") { selected_=Action::Charge; return; }
    auto previewState=initialState(rules_); previewState[0].energy=rules_.maxEnergy;
    const auto resolved=resolveRound(previewState,{{Action::Ultimate,Action::Defend}},rules_);
    reveal(*resolved.result,700,1700); applySettlement();
}
}
