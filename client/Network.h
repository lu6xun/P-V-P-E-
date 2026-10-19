#pragma once
#include "Config.h"
#include <memory>
#include <vector>
namespace duel {
// Pimpl keeps Windows socket headers out of raylib translation units.
class Network {
public:
    Network(const std::string& host,int port,const Config& config);
    ~Network();
    Network(const Network&)=delete;
    Network& operator=(const Network&)=delete;
    void send(Json message);
    std::vector<Json> poll();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
