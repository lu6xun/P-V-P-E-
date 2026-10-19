#pragma once
#include "Protocol.h"
#include <filesystem>
namespace duel {
struct Config {
    Rules rules;
    int selectionMs=30000, revealMs=700, displayMs=1700, waitingRoomMs=300000;
    int heartbeatMs=3000, heartbeatTimeoutMs=15000, connectTimeoutMs=5000;
    std::string listenAddress="0.0.0.0", defaultHost="localhost";
    int listenPort=28765, defaultPort=28765;
};
Config loadConfig(const std::filesystem::path& path);
Json timingJson(const Config& config);
std::filesystem::path defaultConfigPath(const char* executable);
}
