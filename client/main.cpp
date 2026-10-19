#include "Game.h"
#include <raylib.h>
#include <rlgl.h>
#include <iostream>

int main(int argc,char** argv) {
    try {
        auto path=duel::defaultConfigPath(argv[0]); auto seed=std::random_device{}();
        std::string screenshot,scene="menu";
        for(int i=1;i<argc;++i) {
            const std::string arg=argv[i];
            if(arg=="--config" && i+1<argc) path=argv[++i];
            else if(arg=="--seed" && i+1<argc) seed=static_cast<std::uint32_t>(std::stoul(argv[++i]));
            else if(arg=="--screenshot" && i+1<argc) screenshot=argv[++i];
            else if(arg=="--scene" && i+1<argc) scene=argv[++i];
            else if(arg=="--help") { std::cout<<"duel_client [--config path] [--seed integer] [--screenshot path.png --scene menu|battle|result|room]\n"; return 0; }
            else throw std::runtime_error("Unknown argument: "+arg);
        }
        duel::Game game(duel::loadConfig(path),seed);
        SetConfigFlags(FLAG_MSAA_4X_HINT); InitWindow(960,640,"Simultaneous Duel");
        if(!IsWindowReady()) throw std::runtime_error("Cannot create OpenGL window");
        SetExitKey(KEY_NULL); SetTargetFPS(60);
        if(!screenshot.empty()) game.preview(scene);
        int frames=0;
        while(!WindowShouldClose() && !game.quit()) {
            if(screenshot.empty()) game.update();
            BeginDrawing(); game.draw(); EndDrawing();
            if(!screenshot.empty() && ++frames==4) {
                // Read the actual framebuffer size: raylib 5.5 TakeScreenshot
                // multiplies DPI again on Windows and strips output directories.
                Image capture{rlReadScreenPixels(GetRenderWidth(),GetRenderHeight()),GetRenderWidth(),GetRenderHeight(),1,PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
                const bool exported=ExportImage(capture,screenshot.c_str());
                UnloadImage(capture);
                if(!exported) throw std::runtime_error("Cannot save screenshot: "+screenshot);
                break;
            }
        }
        CloseWindow(); return 0;
    } catch(const std::exception& e) { std::cerr<<"Client error: "<<e.what()<<'\n'; return 1; }
}
