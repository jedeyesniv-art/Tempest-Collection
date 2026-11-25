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
    std::filesystem::path userdata;
    std::optional<uint64_t> restartAt;
    bool fromLauncher=false,novsync=false;
    int menuTest=0;   // --menu (1) / --menu=all (2): the menu open from the start (tests; --winshot counts its presents)
    std::vector<std::pair<uint64_t,int>> stateTests;
    std::vector<std::pair<uint64_t,std::string>> screenshots;
    int width=1440,height=1080;bool windowSize=false;
};
inline CollectionConfig collection;
inline void load_collection_settings(){
    std::filesystem::create_directories(collection.userdata);
    if(collection.fromLauncher)runlog::Start(collection.userdata,"t3k");
    settings::SetDefaultText(R"(# Tempest 3000 native settings.
fullscreen = 1
vsync = 1
game_speed = 21
menu_speed = 30
warp_speed = 60
warp_ramp = 0.5
volume = 1
native_view = 1
native_interpolate = 1
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
control_device = gamepad
controller = auto
cheat_inf_lives = 0
)");
    settings::Load((collection.userdata/"t3k_settings.txt").string());
    volume::Init(collection.userdata,"t3k");
    t3k_menu::Install(collection.fromLauncher);
}
}
