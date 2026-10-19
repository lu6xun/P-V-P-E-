#pragma once
#include "LocalMatch.h"
#include "Network.h"
#include <deque>
namespace duel {
enum class Screen { Menu, Connecting, RoomWaiting, LocalSelecting, OnlineSelecting,
    Submitted, Reveal, Settlement, Finished, ConnectionError };
class Game {
public:
    Game(Config config,std::uint32_t seed);
    void update();
    void draw();
    bool quit() const { return quit_; }
    // Optional screenshot smoke path uses the exact normal view and state handling.
    void preview(const std::string& scene);
private:
    void menu();
    void startLocal();
    void connect(bool create);
    void submit();
    void event(const Json& message);
    void beginRound(const Json& message,bool first);
    void reveal(RoundResult result,int revealMs,int displayMs);
    void applySettlement();
    void addLog();
    void drawMenu();
    void drawBattle();
    Config config_; Rules rules_; LocalMatch local_;
    Screen screen_=Screen::Menu;
    std::unique_ptr<Network> network_;
    State state_{};
    std::optional<Action> selected_;
    std::optional<RoundResult> result_;
    std::deque<std::string> logs_;
    std::string host_,port_,roomInput_,room_,message_;
    int activeInput_=0,seat_=0,round_=1;
    std::int64_t roundId_=0;
    bool online_=false,create_=false,quit_=false,opponentReady_=false,acked_=false;
    bool serverFinished_=false,rematchSent_=false;
    bool closingNetwork_=false;
    std::array<bool,2> rematch_{};
    double deadline_=0,phaseDeadline_=0;
    int displayMs_=1700;
};
}
