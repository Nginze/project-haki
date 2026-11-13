#include "Haki/Systems/TransformHierarchySystem.h"
#include "Haki/Scene/Components.h"
#include "Haki/Scene/Scene.h"
#include "Haki/AssetManager/AssetCatalog.h"
#include "Haki/Serialization/SceneSerializer.h"
#include <glm/gtc/quaternion.hpp>
#include <nlohmann/json.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>

int main() {
    using namespace Haki;
    int failures=0;
    auto check=[&](bool pass,const char *name){std::cout<<(pass?"PASS ":"FAIL ")<<name<<'\n';failures+=!pass;};
    entt::registry r;
    auto root=r.create();
    r.emplace<EntityIdComponent>(root,"player");
    auto &pose=r.emplace<TransformComponent>(root);
    pose.Translation={10,2,3};
    pose.Rotation=glm::angleAxis(glm::radians(90.f),glm::vec3(0,1,0));
    r.emplace<CharacterControllerComponent>(root);
    auto visual=r.create();
    r.emplace<EntityIdComponent>(visual,"visual");
    r.emplace<TransformComponent>(visual);
    auto &parent=r.emplace<ParentTransformComponent>(visual);
    parent.ParentId="player";
    parent.Local.Translation={1,0,0};
    parent.Local.Scale={2,2,2};
    auto accessory=r.create();
    r.emplace<EntityIdComponent>(accessory,"accessory");
    r.emplace<TransformComponent>(accessory);
    auto &nested=r.emplace<ParentTransformComponent>(accessory);
    nested.ParentId="visual";
    nested.Local.Translation={0,1,0};
    TransformHierarchySystem::Update(r);
    check(glm::distance(r.get<TransformComponent>(visual).Translation,glm::vec3(10,2,2))<0.001f,"child follows parent yaw and translation");
    check(glm::distance(r.get<TransformComponent>(accessory).Translation,glm::vec3(10,4,2))<0.001f,"nested child inherits visual scale");
    r.destroy(root);
    TransformHierarchySystem::Update(r);
    check(glm::distance(r.get<TransformComponent>(visual).Translation,glm::vec3(10,2,2))<0.001f,"missing runtime parent freezes last world pose");

    using Json=nlohmann::json;
    auto dir=std::filesystem::temp_directory_path()/("haki-hierarchy-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(dir);
    auto file=dir/"test.scene.json";
    Json document={{"type","haki.scene"},{"version",1},{"id","hierarchy"},{"entities",Json::array({
        {{"id","camera"},{"components",{{"transform",Json::object()},{"camera",{{"primary",true}}}}}},
        {{"id","root"},{"components",{{"transform",{{"translation",{4,0,0}}}}}}},
        {{"id","child"},{"components",{{"parent",{{"entity","root"},{"local",{{"translation",{1,0,0}}}}}}}}}
    })}};
    SceneSerializer serializer(std::make_shared<AssetCatalog>());
    auto write=[&]{std::ofstream(file)<<document.dump();};
    write();
    auto loaded=serializer.Deserialize(file);
    check(bool(loaded),"parent relation deserializes without graphics assets");
    if(loaded) {
        for(auto e:loaded.Value->GetRegistry().view<EntityIdComponent>())
            if(loaded.Value->GetRegistry().get<EntityIdComponent>(e).Id=="child")
                check(loaded.Value->GetRegistry().get<TransformComponent>(e).Translation.x==5,"deserialized local pose resolves");
    }
    document["entities"][2]["components"]["parent"]["entity"]="missing";
    write(); check(!serializer.Deserialize(file),"missing serialized parent rejected");
    document["entities"][2]["components"]["parent"]["entity"]="child";
    write(); check(!serializer.Deserialize(file),"serialized parent cycle rejected");
    std::filesystem::remove_all(dir);
    return failures?1:0;
}
