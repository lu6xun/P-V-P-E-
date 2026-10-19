#include "RoomService.h"
#include "Connection.h"
#include <chrono>
#include <csignal>
#include <iostream>

using namespace duel;
static Millis nowMs() { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

class Server {
public:
    Server(asio::io_context& io,Config config) : io_(io),config_(std::move(config)),acceptor_(io),timer_(io),
        rooms_(config_,[this](ClientId id,const Json& j) {
            // Posting avoids synchronous transport-close re-entry into RoomService.
            asio::post(io_,[this,id,j] { auto found=clients_.find(id); if(found!=clients_.end()) found->second.connection->send(j); });
        }) {
        asio::ip::tcp::endpoint ep(asio::ip::make_address(config_.listenAddress),static_cast<unsigned short>(config_.listenPort));
        acceptor_.open(ep.protocol()); acceptor_.set_option(asio::socket_base::reuse_address(true));
        acceptor_.bind(ep); acceptor_.listen();
        std::cout<<"READY "<<acceptor_.local_endpoint().port()<<std::endl;
        accept(); tick();
    }
private:
    struct Client { std::shared_ptr<Connection> connection; Millis lastSeen; Millis window; int messages=0; };
    void accept() {
        acceptor_.async_accept([this](asio::error_code ec,asio::ip::tcp::socket socket) {
            if(!ec && clients_.size()<256) {
                const auto id=nextId_++;
                auto connection=std::make_shared<Connection>(std::move(socket));
                clients_.emplace(id,Client{connection,nowMs(),nowMs(),0}); rooms_.connect(id);
                connection->start([this,id](const Json& j) {
                    auto& client=clients_.at(id); const auto now=nowMs();
                    if(now-client.window>=1000) { client.window=now; client.messages=0; }
                    if(++client.messages>120) { client.connection->close("Message rate exceeded"); return; }
                    client.lastSeen=now; rooms_.receive(id,j,now);
                },[this,id](const std::string&) { rooms_.disconnect(id); clients_.erase(id); });
                connection->send({{"type","welcome"},{"protocol",1},{"heartbeatMs",config_.heartbeatMs},{"heartbeatTimeoutMs",config_.heartbeatTimeoutMs}});
            }
            if(acceptor_.is_open()) accept();
        });
    }
    void tick() {
        timer_.expires_after(std::chrono::milliseconds(20));
        timer_.async_wait([this](asio::error_code ec) {
            if(ec) return;
            const auto now=nowMs(); std::vector<std::shared_ptr<Connection>> expired;
            for(const auto& [id,c]:clients_) if(now-c.lastSeen>=config_.heartbeatTimeoutMs) expired.push_back(c.connection);
            for(auto& c:expired) c->close("Heartbeat timeout");
            rooms_.tick(now); tick();
        });
    }
    asio::io_context& io_; Config config_; asio::ip::tcp::acceptor acceptor_; asio::steady_timer timer_;
    RoomService rooms_; std::map<ClientId,Client> clients_; ClientId nextId_=1;
};
int main(int argc,char** argv) {
    try {
        auto path=defaultConfigPath(argv[0]);
        for(int i=1;i<argc;++i) {
            const std::string arg=argv[i];
            if(arg=="--config" && i+1<argc) path=argv[++i];
            else if(arg=="--help") { std::cout<<"duel_server [--config path]\n"; return 0; }
            else throw std::runtime_error("Unknown argument: "+arg);
        }
        asio::io_context io; Server server(io,loadConfig(path));
        asio::signal_set signals(io,SIGINT,SIGTERM); signals.async_wait([&](auto,auto){io.stop();});
        io.run(); return 0;
    } catch(const std::exception& e) { std::cerr<<"Server error: "<<e.what()<<'\n'; return 1; }
}
