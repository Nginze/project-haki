#include "Haki/Core/ProjectSettings.h"
#include <fstream>
#include <cmath>
#include <stdexcept>

namespace Haki {
namespace {
using Json = nlohmann::json;
Json Read(const std::filesystem::path &path) {
    std::ifstream file(path);
    if (!file) throw std::runtime_error("Cannot read project: " + path.string());
    Json doc; file >> doc;
    if (!doc.is_object()) throw std::runtime_error("Project must be an object");
    return doc;
}
void Range(bool valid, const char *name) { if (!valid) throw std::runtime_error(std::string("Invalid project setting: ")+name); }
void Validate(const ProjectSettings &p) {
    Range(std::isfinite(p.Rendering.RenderScale),"RenderScale");
    Range(std::isfinite(p.Rendering.AmbientSkyColor.x) && std::isfinite(p.Rendering.AmbientSkyColor.y) && std::isfinite(p.Rendering.AmbientSkyColor.z),"AmbientSkyColor");
    Range(std::isfinite(p.Rendering.AmbientGroundColor.x) && std::isfinite(p.Rendering.AmbientGroundColor.y) && std::isfinite(p.Rendering.AmbientGroundColor.z),"AmbientGroundColor");
    Range(std::isfinite(p.Rendering.AmbientIntensity),"AmbientIntensity");
    Range(std::isfinite(p.Rendering.SsaoRadius),"SsaoRadius");
    Range(std::isfinite(p.Rendering.SsaoBias),"SsaoBias");
    Range(std::isfinite(p.Rendering.SsaoPower),"SsaoPower");
    Range(std::isfinite(p.Rendering.ShadowDistance),"ShadowDistance");
    Range(std::isfinite(p.Rendering.CascadeSplitLambda),"CascadeSplitLambda");
    Range(std::isfinite(p.Rendering.BloomIntensity),"BloomIntensity");
    Range(std::isfinite(p.Rendering.BloomThreshold),"BloomThreshold");
    Range(std::isfinite(p.Rendering.BloomSoftKnee),"BloomSoftKnee");
    Range(std::isfinite(p.Rendering.BloomDiffusion),"BloomDiffusion");
    Range(std::isfinite(p.Rendering.Exposure),"Exposure");
    Range(std::isfinite(p.Rendering.FxaaContrastThreshold),"FxaaContrastThreshold");
    Range(std::isfinite(p.Rendering.FxaaRelativeThreshold),"FxaaRelativeThreshold");
    Range(std::isfinite(p.Rendering.FxaaSubpixelBlending),"FxaaSubpixelBlending");
    Range(!p.Title.empty(),"window.title");
    Range(p.Width>=64 && p.Width<=16384 && p.Height>=64 && p.Height<=16384,"window dimensions (64..16384)");
    Range(p.Rendering.RenderScale>=0.25f && p.Rendering.RenderScale<=2,"renderScale (0.25..2)");
    Range(p.Rendering.RenderHeight>=0 && p.Rendering.RenderHeight<=8192,"renderHeight (0..8192)");
    Range(p.Rendering.ShadowMapResolution>=256 && p.Rendering.ShadowMapResolution<=8192,"shadowMapResolution");
    Range(p.Rendering.SsaoSampleCount>=1 && p.Rendering.SsaoSampleCount<=64,"ssaoSampleCount");
}
}
ProjectSettings ProjectSettings::Load(const std::filesystem::path &path, ProjectSettings p) {
    const auto doc=Read(path);
    if (doc.contains("window")) {
        const auto &w=doc.at("window"); Range(w.is_object(),"window");
        if(w.contains("width")) Range(w.at("width").is_number_integer(),"window.width");
        if(w.contains("height")) Range(w.at("height").is_number_integer(),"window.height");
        p.Title=w.value("title",p.Title); p.Width=w.value("width",p.Width); p.Height=w.value("height",p.Height);
        p.Fullscreen=w.value("fullscreen",p.Fullscreen); p.Maximized=w.value("maximized",p.Maximized);
        p.Vsync=w.value("vsync",p.Vsync);
    }
    if (doc.contains("editor")) {
        Range(doc.at("editor").is_object(),"editor");
        p.Autosave=doc.at("editor").value("autosaveProjectSettings",p.Autosave);
    }
    if (doc.contains("rendering")) {
        const auto &r=doc.at("rendering"); Range(r.is_object(),"rendering");
        if(r.contains("ssaoSampleCount")) Range(r.at("ssaoSampleCount").is_number_integer(),"ssaoSampleCount");
        if(r.contains("shadowMapResolution")) Range(r.at("shadowMapResolution").is_number_integer(),"shadowMapResolution");
        if(r.contains("renderHeight")) Range(r.at("renderHeight").is_number_integer(),"renderHeight");
        p.Rendering.FrustumCullingEnabled=r.value("frustumCullingEnabled",p.Rendering.FrustumCullingEnabled);
        p.Rendering.ShowBounds=r.value("showBounds",p.Rendering.ShowBounds);
        p.Rendering.GpuTimingsEnabled=r.value("gpuTimingsEnabled",p.Rendering.GpuTimingsEnabled);
        p.Rendering.Wireframe=r.value("wireframe",p.Rendering.Wireframe);
        p.Rendering.RenderScale=r.value("renderScale",p.Rendering.RenderScale);
        Range(std::isfinite(p.Rendering.RenderScale),"renderScale");
        p.Rendering.RenderHeight=r.value("renderHeight",p.Rendering.RenderHeight);
        p.Rendering.DepthPrepassEnabled=r.value("depthPrepassEnabled",p.Rendering.DepthPrepassEnabled);
        p.Rendering.SkyboxEnabled=r.value("skyboxEnabled",p.Rendering.SkyboxEnabled);
        p.Rendering.IndirectDiffuseEnabled=r.value("indirectDiffuseEnabled",p.Rendering.IndirectDiffuseEnabled);
        p.Rendering.IndirectSpecularEnabled=r.value("indirectSpecularEnabled",p.Rendering.IndirectSpecularEnabled);
        p.Rendering.AmbientIntensity=r.value("ambientIntensity",p.Rendering.AmbientIntensity);
        Range(std::isfinite(p.Rendering.AmbientIntensity),"ambientIntensity");
        p.Rendering.SsaoEnabled=r.value("ssaoEnabled",p.Rendering.SsaoEnabled);
        p.Rendering.SsaoBlurEnabled=r.value("ssaoBlurEnabled",p.Rendering.SsaoBlurEnabled);
        p.Rendering.SsaoHalfResolution=r.value("ssaoHalfResolution",p.Rendering.SsaoHalfResolution);
        p.Rendering.SsaoSampleCount=r.value("ssaoSampleCount",p.Rendering.SsaoSampleCount);
        p.Rendering.SsaoRadius=r.value("ssaoRadius",p.Rendering.SsaoRadius);
        Range(std::isfinite(p.Rendering.SsaoRadius),"ssaoRadius");
        p.Rendering.SsaoBias=r.value("ssaoBias",p.Rendering.SsaoBias);
        Range(std::isfinite(p.Rendering.SsaoBias),"ssaoBias");
        p.Rendering.SsaoPower=r.value("ssaoPower",p.Rendering.SsaoPower);
        Range(std::isfinite(p.Rendering.SsaoPower),"ssaoPower");
        p.Rendering.ShadowsEnabled=r.value("shadowsEnabled",p.Rendering.ShadowsEnabled);
        p.Rendering.ShadowMapResolution=r.value("shadowMapResolution",p.Rendering.ShadowMapResolution);
        if(r.contains("shadowFilter")) { auto v=r.at("shadowFilter").get<std::string>(); Range(v=="pcf" || v=="pcss","shadowFilter");
            p.Rendering.ShadowFilter=v=="pcss"?ShadowFilterMode::PCSS:ShadowFilterMode::PCF; }
        p.Rendering.ShadowDistance=r.value("shadowDistance",p.Rendering.ShadowDistance);
        Range(std::isfinite(p.Rendering.ShadowDistance),"shadowDistance");
        p.Rendering.CascadeSplitLambda=r.value("cascadeSplitLambda",p.Rendering.CascadeSplitLambda);
        Range(std::isfinite(p.Rendering.CascadeSplitLambda),"cascadeSplitLambda");
        p.Rendering.ShowCascadeZones=r.value("showCascadeZones",p.Rendering.ShowCascadeZones);
        p.Rendering.BloomEnabled=r.value("bloomEnabled",p.Rendering.BloomEnabled);
        p.Rendering.BloomIntensity=r.value("bloomIntensity",p.Rendering.BloomIntensity);
        Range(std::isfinite(p.Rendering.BloomIntensity),"bloomIntensity");
        p.Rendering.BloomThreshold=r.value("bloomThreshold",p.Rendering.BloomThreshold);
        Range(std::isfinite(p.Rendering.BloomThreshold),"bloomThreshold");
        p.Rendering.BloomSoftKnee=r.value("bloomSoftKnee",p.Rendering.BloomSoftKnee);
        Range(std::isfinite(p.Rendering.BloomSoftKnee),"bloomSoftKnee");
        p.Rendering.BloomDiffusion=r.value("bloomDiffusion",p.Rendering.BloomDiffusion);
        Range(std::isfinite(p.Rendering.BloomDiffusion),"bloomDiffusion");
        p.Rendering.Exposure=r.value("exposure",p.Rendering.Exposure);
        Range(std::isfinite(p.Rendering.Exposure),"exposure");
        p.Rendering.FxaaEnabled=r.value("fxaaEnabled",p.Rendering.FxaaEnabled);
        p.Rendering.FxaaContrastThreshold=r.value("fxaaContrastThreshold",p.Rendering.FxaaContrastThreshold);
        Range(std::isfinite(p.Rendering.FxaaContrastThreshold),"fxaaContrastThreshold");
        p.Rendering.FxaaRelativeThreshold=r.value("fxaaRelativeThreshold",p.Rendering.FxaaRelativeThreshold);
        Range(std::isfinite(p.Rendering.FxaaRelativeThreshold),"fxaaRelativeThreshold");
        p.Rendering.FxaaSubpixelBlending=r.value("fxaaSubpixelBlending",p.Rendering.FxaaSubpixelBlending);
        Range(std::isfinite(p.Rendering.FxaaSubpixelBlending),"fxaaSubpixelBlending");
        if(r.contains("ambientSkyColor")) { auto v=r.at("ambientSkyColor"); Range(v.is_array() && v.size()==3,"ambientSkyColor");
            for(int i=0;i<3;++i) { p.Rendering.AmbientSkyColor[i]=v.at(i).get<float>(); Range(std::isfinite(p.Rendering.AmbientSkyColor[i]),"ambientSkyColor"); } }
        if(r.contains("ambientGroundColor")) { auto v=r.at("ambientGroundColor"); Range(v.is_array() && v.size()==3,"ambientGroundColor");
            for(int i=0;i<3;++i) { p.Rendering.AmbientGroundColor[i]=v.at(i).get<float>(); Range(std::isfinite(p.Rendering.AmbientGroundColor[i]),"ambientGroundColor"); } }
    }
    Validate(p); return p;
}
nlohmann::json ProjectSettings::Sections() const {
    Json r=Json::object();
    r["frustumCullingEnabled"]=Rendering.FrustumCullingEnabled;
    r["showBounds"]=Rendering.ShowBounds;
    r["gpuTimingsEnabled"]=Rendering.GpuTimingsEnabled;
    r["wireframe"]=Rendering.Wireframe;
    r["renderScale"]=Rendering.RenderScale;
    r["renderHeight"]=Rendering.RenderHeight;
    r["depthPrepassEnabled"]=Rendering.DepthPrepassEnabled;
    r["skyboxEnabled"]=Rendering.SkyboxEnabled;
    r["indirectDiffuseEnabled"]=Rendering.IndirectDiffuseEnabled;
    r["indirectSpecularEnabled"]=Rendering.IndirectSpecularEnabled;
    r["ambientIntensity"]=Rendering.AmbientIntensity;
    r["ssaoEnabled"]=Rendering.SsaoEnabled;
    r["ssaoBlurEnabled"]=Rendering.SsaoBlurEnabled;
    r["ssaoHalfResolution"]=Rendering.SsaoHalfResolution;
    r["ssaoSampleCount"]=Rendering.SsaoSampleCount;
    r["ssaoRadius"]=Rendering.SsaoRadius;
    r["ssaoBias"]=Rendering.SsaoBias;
    r["ssaoPower"]=Rendering.SsaoPower;
    r["shadowsEnabled"]=Rendering.ShadowsEnabled;
    r["shadowMapResolution"]=Rendering.ShadowMapResolution;
    r["shadowFilter"]=Rendering.ShadowFilter==ShadowFilterMode::PCSS?"pcss":"pcf";
    r["shadowDistance"]=Rendering.ShadowDistance;
    r["cascadeSplitLambda"]=Rendering.CascadeSplitLambda;
    r["showCascadeZones"]=Rendering.ShowCascadeZones;
    r["bloomEnabled"]=Rendering.BloomEnabled;
    r["bloomIntensity"]=Rendering.BloomIntensity;
    r["bloomThreshold"]=Rendering.BloomThreshold;
    r["bloomSoftKnee"]=Rendering.BloomSoftKnee;
    r["bloomDiffusion"]=Rendering.BloomDiffusion;
    r["exposure"]=Rendering.Exposure;
    r["fxaaEnabled"]=Rendering.FxaaEnabled;
    r["fxaaContrastThreshold"]=Rendering.FxaaContrastThreshold;
    r["fxaaRelativeThreshold"]=Rendering.FxaaRelativeThreshold;
    r["fxaaSubpixelBlending"]=Rendering.FxaaSubpixelBlending;
    r["ambientSkyColor"]={Rendering.AmbientSkyColor.x,Rendering.AmbientSkyColor.y,Rendering.AmbientSkyColor.z};
    r["ambientGroundColor"]={Rendering.AmbientGroundColor.x,Rendering.AmbientGroundColor.y,Rendering.AmbientGroundColor.z};
    return {{"window",{{"title",Title},{"width",Width},{"height",Height},{"fullscreen",Fullscreen},{"maximized",Maximized},{"vsync",Vsync}}},
            {"rendering",r},{"editor",{{"autosaveProjectSettings",Autosave}}}};
}
bool ProjectSettings::Save(const std::filesystem::path &path, std::string &error) const {
    const auto temporary=path.string()+".settings.tmp";
    try {
        Validate(*this);
        auto doc=Read(path); // Preserve the latest scene registrations and unrelated fields.
        const auto sections=Sections();
        for (const auto &[name,values]:sections.items()) {
            if (!doc.contains(name)) doc[name]=Json::object();
            if (!doc[name].is_object()) throw std::runtime_error("Project section is not an object: "+name);
            doc[name].update(values);
        }
        { std::ofstream file(temporary); if(!file) throw std::runtime_error("Cannot write project settings");
          file << doc.dump(2) << '\n'; file.flush(); if(!file) throw std::runtime_error("Cannot flush project settings"); }
        std::filesystem::rename(temporary,path);
        return true;
    } catch(const std::exception &e) { error=e.what(); std::error_code ec; std::filesystem::remove(temporary,ec); return false; }
}
}
