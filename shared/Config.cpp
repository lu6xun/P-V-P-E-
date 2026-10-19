#include "Config.h"
#include <fstream>
#include <stdexcept>
namespace duel {
Config loadConfig(const std::filesystem::path& path) {
    std::ifstream file(path);
    if(!file) throw std::runtime_error("Cannot open config: " + path.string());
    try {
        Json j=Json::parse(file); Config c;
        c.rules=readRules(j.at("rules"));
        const auto& t=j.at("timing");
        c.selectionMs=requiredInt(t,"selectionMs",100,300000);
        c.revealMs=requiredInt(t,"revealMs",50,10000);
        c.displayMs=requiredInt(t,"displayMs",50,10000);
        c.waitingRoomMs=requiredInt(t,"waitingRoomMs",100,3600000);
        c.heartbeatMs=requiredInt(t,"heartbeatMs",50,30000);
        c.heartbeatTimeoutMs=requiredInt(t,"heartbeatTimeoutMs",200,120000);
        c.connectTimeoutMs=requiredInt(t,"connectTimeoutMs",100,30000);
        if(c.heartbeatTimeoutMs<c.heartbeatMs*3) throw std::runtime_error("heartbeatTimeoutMs must be at least 3 * heartbeatMs");
        const auto& s=j.at("server"); const auto& client=j.at("client");
        c.listenPort=requiredInt(s,"port",0,65535); // 0: OS-assigned port for integration tests.
        c.defaultPort=requiredInt(client,"port",1,65535);
        c.listenAddress=s.at("address").get<std::string>();
        c.defaultHost=client.at("host").get<std::string>();
        for(const auto* value:{&c.listenAddress,&c.defaultHost})
            if(value->empty() || value->size()>253 || value->find_first_of(" \t\r\n")!=std::string::npos) throw std::runtime_error("invalid network address");
        return c;
    } catch(const std::exception& e) { throw std::runtime_error("Invalid config " + path.string() + ": " + e.what()); }
}
Json timingJson(const Config& c) { return {{"selectionMs",c.selectionMs},{"revealMs",c.revealMs},{"displayMs",c.displayMs}}; }
std::filesystem::path defaultConfigPath(const char* exe) {
    return std::filesystem::absolute(exe).parent_path()/"assets"/"config"/"game.json";
}
}
