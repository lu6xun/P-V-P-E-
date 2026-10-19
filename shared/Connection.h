#pragma once
#include "Protocol.h"
#include <asio.hpp>
#include <deque>
#include <functional>
#include <memory>

namespace duel {
// All methods and callbacks run on ONE io_context thread. async_read assembles
// the exact header/body lengths; the single writer drains a bounded FIFO.
class Connection : public std::enable_shared_from_this<Connection> {
public:
    using Receive=std::function<void(const Json&)>;
    using Closed=std::function<void(const std::string&)>;
    explicit Connection(asio::ip::tcp::socket socket) : socket_(std::move(socket)) {}
    void start(Receive receive, Closed closed);
    void send(const Json& message);
    void close(const std::string& reason);
private:
    void readHeader();
    void readBody();
    void write();
    asio::ip::tcp::socket socket_;
    std::array<std::uint8_t,4> header_{};
    std::vector<std::uint8_t> body_;
    std::deque<std::shared_ptr<std::vector<std::uint8_t>>> writes_;
    std::size_t queuedBytes_=0;
    Receive receive_;
    Closed closed_;
    bool stopped_=false;
};
}
