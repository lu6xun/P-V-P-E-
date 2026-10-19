#include "Network.h"
#include <chrono>
#include <iostream>
#include <thread>
using namespace duel;
int main(int argc,char** argv) {
    try {
        if(argc!=2) throw std::runtime_error("network_probe PORT");
        Config config; Network first("127.0.0.1",std::stoi(argv[1]),config), second("127.0.0.1",std::stoi(argv[1]),config);
        std::string code; bool secondReady=false,joined=false;
        std::array<bool,2> done{}; std::array<std::vector<Json>,2> results;
        const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(15);
        while(std::chrono::steady_clock::now()<end) {
            Network* clients[]={&first,&second};
            for(int i=0;i<2;++i) for(const auto& j:clients[i]->poll()) {
                const auto type=j.at("type").get<std::string>();
                if(type=="network_error" || type=="error") throw std::runtime_error(j.dump());
                if(type=="connected") { if(i==0) first.send({{"type","create_room"}}); else secondReady=true; }
                if(type=="room_created") code=j.at("room").get<std::string>();
                if(type=="match_start" || type=="round_start") clients[i]->send({{"type","submit"},{"roundId",j.at("roundId")},{"action","attack"}});
                if(type=="round_result") results[i].push_back(j);
                if(type=="match_finished") { if(j.at("outcome")!="draw") throw std::runtime_error("expected draw"); done[i]=true; }
            }
            if(secondReady && !code.empty() && !joined) { second.send({{"type","join_room"},{"room",code}}); joined=true; }
            if(done[0] && done[1]) {
                if(results[0].size()!=5 || results[0]!=results[1]) throw std::runtime_error("inconsistent results");
                std::cout<<"PASS actual C++ Network clients, 5 rounds, identical draw\n"; return 0;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5)); // Headless test driver, not the render loop.
        }
        throw std::runtime_error("probe deadline exceeded");
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
