#include "Connection.h"
namespace duel {
void Connection::start(Receive receive, Closed closed) {
    receive_=std::move(receive); closed_=std::move(closed);
    asio::error_code ec; socket_.set_option(asio::ip::tcp::no_delay(true),ec);
    readHeader();
}
void Connection::readHeader() {
    auto self=shared_from_this();
    asio::async_read(socket_,asio::buffer(header_),[self](asio::error_code ec,std::size_t) {
        if(self->stopped_) return;
        if(ec) { self->close("Connection closed: " + ec.message()); return; }
        const auto length=bodyLength(self->header_);
        if(length==0 || length>MaxMessage) { self->close("Invalid frame length (limit 64 KiB)"); return; }
        self->body_.resize(length); self->readBody();
    });
}
void Connection::readBody() {
    auto self=shared_from_this();
    asio::async_read(socket_,asio::buffer(body_),[self](asio::error_code ec,std::size_t) {
        if(self->stopped_) return;
        if(ec) { self->close("Connection closed: " + ec.message()); return; }
        // Bound nesting before parsing to avoid hostile recursive JSON exhausting the stack.
        int depth=0; bool string=false,escaped=false;
        for(auto byte:self->body_) {
            const char c=static_cast<char>(byte);
            if(string) { if(escaped) escaped=false; else if(c=='\\') escaped=true; else if(c=='"') string=false; }
            else if(c=='"') string=true;
            else if(c=='{' || c=='[') { if(++depth>32) { self->close("JSON nesting exceeds 32"); return; } }
            else if(c=='}' || c==']') --depth;
        }
        const auto json=Json::parse(self->body_,nullptr,false);
        if(json.is_discarded() || !json.is_object()) { self->close("Invalid JSON object"); return; }
        try { self->receive_(json); }
        catch(const std::exception& e) { self->close(std::string("Protocol error: ")+e.what()); return; }
        if(!self->stopped_) self->readHeader();
    });
}
void Connection::send(const Json& message) {
    if(stopped_) return;
    auto bytes=std::make_shared<std::vector<std::uint8_t>>(frame(message));
    if(writes_.size()>=128 || queuedBytes_+bytes->size()>256*1024) { close("Send queue overflow"); return; }
    const bool idle=writes_.empty(); queuedBytes_+=bytes->size(); writes_.push_back(std::move(bytes));
    if(idle) write();
}
void Connection::write() {
    auto self=shared_from_this(); auto bytes=writes_.front();
    asio::async_write(socket_,asio::buffer(*bytes),[self,bytes](asio::error_code ec,std::size_t) {
        if(self->stopped_) return;
        if(ec) { self->close("Send failed: "+ec.message()); return; }
        self->queuedBytes_-=bytes->size(); self->writes_.pop_front();
        if(!self->writes_.empty()) self->write();
    });
}
void Connection::close(const std::string& reason) {
    if(stopped_) return;
    stopped_=true;
    asio::error_code ignored;
    socket_.shutdown(asio::ip::tcp::socket::shutdown_both,ignored); socket_.close(ignored);
    writes_.clear(); queuedBytes_=0;
    if(closed_) closed_(reason);
}
}
