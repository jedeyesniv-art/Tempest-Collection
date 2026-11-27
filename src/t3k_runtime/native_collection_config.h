#pragma once
#include "../settings.h"
#include "../t3k_menu.h"
#include "../volume.h"
#include "../runlog.h"
#include "../pads.h"
#include "../controls.h"
#include <filesystem>
#include <optional>
namespace tempest_native {
struct NativeRestart{};
struct CollectionConfig {
#ifdef TEMPEST_T3K_TIMING_RESEARCH
    static constexpr bool timingResearch=true;
#else
    static constexpr bool timingResearch=false;
#endif
    std::filesystem::path userdata;
    std::string timingTrace;
    std::optional<uint64_t> restartAt;
    bool fromLauncher=false,novsync=false;
    int menuTest=0;   // --menu (1) / --menu=all (2): the menu open from the start (tests; --winshot counts its presents)
    std::vector<std::pair<uint64_t,int>> stateTests;
    std::vector<std::pair<uint64_t,std::string>> screenshots;
    int width=1440,height=1080;bool windowSize=false;
    unsigned logicHz=timingResearch?120:24;
    unsigned sceneHz=0;
    uint64_t fineCheckLimit=0;
    unsigned presentHz=0;
};
inline CollectionConfig collection;
inline void load_collection_settings(){
    std::filesystem::create_directories(collection.userdata);
    if(collection.fromLauncher)runlog::Start(collection.userdata,"t3k");
    settings::SetDefaultText(R"(# Tempest 3000 native settings.
fullscreen = 1
vsync = 1
game_speed = 20
menu_speed = 30
native_menu_speed = 24
native_high_framerate = 1
warp_speed = 60
warp_ramp = 0.5
volume = 1
native_view = 1
native_precise_web = 1
sharp_vectors = 1
sharp_core = 0.35
sharp_glow = 0.4
sharp_max_size = 100
widescreen = 1
resolution = 0
crt = 0
show_rates = 0
state_slot = 1
keep_high_scores = 1
controller = auto
t3k_stick_curve = 2.5
t3k_input_gain = 0.9
cheat_inf_lives = 0
)");
    settings::Load((collection.userdata/"t3k_settings.txt").string());
    // Retire the old modality key once, preserving users who selected mouse.
    if(settings::Str("control_device","gamepad")=="mouse"){
        settings::Save("controller","mouse");settings::Save("control_device","gamepad");
    }
    volume::Init(collection.userdata,"t3k");
    t3k_menu::Install(collection.fromLauncher,collection.logicHz==120&&collection.sceneHz!=0);
}
}
