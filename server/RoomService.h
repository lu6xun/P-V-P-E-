#pragma once
#include "Config.h"
#include <functional>
#include <map>

namespace duel {
using ClientId=std::uint64_t;
using Millis=std::int64_t;
enum class RoomPhase { Waiting, Selecting, Showing, Finished };
struct Room {
    std::string code;
    std::array<ClientId,2> players{};
    RoomPhase phase=RoomPhase::Waiting;
    State state{};
    std::array<std::optional<Action>,2> actions{};
    std::array<bool,2> rematch{};
    std::int64_t roundId=0;
    int round=0;
    Millis deadline=0;
    Outcome outcome=Outcome::Ongoing;
};
// No socket or wall clock: deterministic authority, fed by the server io thread.
class RoomService {
public:
    using Sender=std::function<void(ClientId,const Json&)>;
    RoomService(Config config, Sender send, std::uint32_t seed=std::random_device{}());
    void connect(ClientId client);
    void disconnect(ClientId client);
    void receive(ClientId client,const Json& message,Millis now);
    void tick(Millis now);
    std::size_t roomCount() const { return rooms_.size(); }
    static std::string formatCode(unsigned number);
private:
    std::string allocateCode();
    void error(ClientId client,const std::string& code,const std::string& message);
    void broadcast(const Room& room,const Json& message);
    void removeRoom(const std::string& code,ClientId leaving,const std::string& reason);
    void startMatch(Room& room,Millis now);
    void startRound(Room& room,Millis now,bool first);
    void submit(Room& room,int player,Action action,bool automatic,Millis now);
    void advance(Room& room,Millis now);
    Config config_;
    Sender send_;
    std::mt19937 rng_;
    std::map<ClientId,std::string> membership_;
    std::map<std::string,Room> rooms_;
    std::int64_t nextRoundId_=1;
};
}
