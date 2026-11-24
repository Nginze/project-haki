#include "Haki/Core/ProjectSettings.h"
#include <chrono>
#include <fstream>
#include <iostream>

int main() {
    using namespace Haki;
    using Json=nlohmann::json;
    int failures=0;
    auto check=[&](bool pass,const char *name) {std::cout<<(pass?"PASS ":"FAIL ")<<name<<'\n'; failures+=!pass;};
    const auto root=std::filesystem::temp_directory_path()/("haki-project-settings-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);
    const auto path=root/"Project.json";
    Json doc={{"type","haki.scene-manifest"},{"version",1},{"entry","arena"},{"scenes",Json::array()},
        {"window",{{"title","My Game"},{"width",1600},{"height",900},{"vsync",false}}},
        {"rendering",{{"renderScale",0.5},{"renderHeight",720},{"shadowFilter","pcss"},{"exposure",1.5},{"tonemapper","neutral"}}},
        {"editor",{{"autosaveProjectSettings",false}}}};
    auto write=[&] {std::ofstream(path)<<doc.dump();};write();
    auto p=ProjectSettings::Load(path,ProjectSettings{});
    check(p.Title=="My Game" && p.Width==1600 && p.Height==900 && !p.Vsync,"window properties load");
    check(p.Rendering.RenderScale==0.5f && p.Rendering.RenderHeight==720 && p.Rendering.ShadowFilter==ShadowFilterMode::PCSS
          && p.Rendering.Tonemap==TonemapMode::Neutral,
          "renderer properties load");
    check(!p.Autosave && p.Rendering.ShadowsEnabled,"missing options keep defaults");
    check(ProjectSettings{}.Rendering.Tonemap==TonemapMode::ACES,"ACES stays the default tonemapper");
    // Simulate a tool changing registrations while the editor holds its loaded settings.
    doc["entry"]="new-scene"; doc["scenes"].push_back({{"id","new-scene"},{"source","Scenes/new.scene.json"}});
    doc["rendering"]["futureOption"]="preserved"; write();
    p.Width=1920;p.Rendering.Exposure=2;
    std::string error;
    check(p.Save(path,error),"project settings save atomically");
    Json saved;std::ifstream(path)>>saved;
    check(saved["entry"]=="new-scene" && saved["scenes"]==doc["scenes"] && saved["rendering"]["futureOption"]=="preserved",
          "saving settings preserves latest registrations and unknown properties");
    auto reloaded=ProjectSettings::Load(path,ProjectSettings{});
    check(reloaded.Width==1920 && reloaded.Rendering.Exposure==2,"saved settings restore on restart");
    check(reloaded.Sections()==p.Sections(),"every rendering and window property round-trips");
    p.Width=-1;
    check(!p.Save(path,error),"invalid dimensions rejected without replacing project");
    Json unchanged;std::ifstream(path)>>unchanged;check(unchanged==saved,"failed save preserves valid project");
    doc["rendering"]["renderScale"]=-1;write();
    bool rejected=false;try {ProjectSettings::Load(path,ProjectSettings{});}catch(const std::exception&){rejected=true;}
    check(rejected,"invalid configured render scale rejected");
    std::filesystem::remove_all(root);
    return failures?1:0;
}
