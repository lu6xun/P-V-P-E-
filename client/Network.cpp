#include "Network.h"
#include "Connection.h"
#include <mutex>
#include <thread>

namespace duel {
struct Network::Impl {
    asio::io_context io;
    asio::ip::tcp::resolver resolver{io};
    asio::ip::tcp::socket socket{io};
    asio::steady_timer timer{io};
    std::shared_ptr<Connection> connection;
    std::mutex mutex;
    std::vector<Json> events;
    std::thread thread;
    bool failed=false,connected=false;
    Config config;
    using Clock=std::chrono::steady_clock;
    Clock::time_point started=Clock::now(),lastReceived=started,lastPing=started;
    Impl(const std::string& host,int port,const Config& c) : config(c) {
        resolver.async_resolve(host,std::to_string(port),[this](asio::error_code ec,auto endpoints) {
            if(failed) return;
            if(ec) { fail("DNS: "+ec.message()); return; }
            asio::async_connect(socket,endpoints,[this](asio::error_code error,auto) {
                if(failed) return;
                if(error) { fail("Connect: "+error.message()); return; }
                connected=true; lastReceived=lastPing=Clock::now();
                connection=std::make_shared<Connection>(std::move(socket));
                connection->start([this](const Json& j) {
                    lastReceived=Clock::now();
                    if(j.at("type")=="welcome") {
                        if(requiredInt(j,"protocol",1,1)!=1) throw std::runtime_error("unsupported protocol");
                        config.heartbeatMs=requiredInt(j,"heartbeatMs",50,30000);
                        config.heartbeatTimeoutMs=requiredInt(j,"heartbeatTimeoutMs",200,120000);
                    }
                    if(j.at("type")!="pong") push(j);
                },[this](const std::string& reason) { fail(reason); });
                push({{"type","connected"}});
            });
        });
        tick(); thread=std::thread([this]{
            try { io.run(); }
            catch(const std::exception& e) { fail(std::string("Network error: ")+e.what()); }
        });
    }
    ~Impl() {
        io.stop(); if(thread.joinable()) thread.join();
        failed=true;
        if(connection) connection->close("Client closed");
    }
    void push(const Json& j) {
        bool overflow=false;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if(events.size()<256) events.push_back(j);
            else { events.clear(); events.push_back({{"type","network_error"},{"message","Receive queue overflow"}}); overflow=true; }
        }
        if(overflow) fail("Receive queue overflow");
    }
    void fail(const std::string& reason) {
        if(failed) return;
        failed=true; resolver.cancel(); timer.cancel();
        asio::error_code ignored; socket.close(ignored);
        if(connection) connection->close(reason);
        push({{"type","network_error"},{"message",reason}});
    }
    void tick() {
        timer.expires_after(std::chrono::milliseconds(50));
        timer.async_wait([this](asio::error_code ec) {
            if(ec || failed) return;
            const auto now=Clock::now();
            const auto elapsed=[](auto a,auto b){return std::chrono::duration_cast<std::chrono::milliseconds>(a-b).count();};
            if(!connected && elapsed(now,started)>=config.connectTimeoutMs) { fail("Connection timed out"); return; }
            if(connected) {
                if(elapsed(now,lastReceived)>=config.heartbeatTimeoutMs) { fail("Server heartbeat timed out"); return; }
                if(elapsed(now,lastPing)>=config.heartbeatMs) { lastPing=now; connection->send({{"type","ping"}}); }
            }
            tick();
        });
    }
};
Network::Network(const std::string& host,int port,const Config& c) : impl_(std::make_unique<Impl>(host,port,c)) {}
Network::~Network()=default;
void Network::send(Json j) { asio::post(impl_->io,[p=impl_.get(),j=std::move(j)]{if(p->connection && !p->failed) p->connection->send(j);}); }
std::vector<Json> Network::poll() {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    std::vector<Json> result; result.swap(impl_->events); return result;
}
}
