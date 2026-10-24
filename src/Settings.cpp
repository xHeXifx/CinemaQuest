#include "Settings.hpp"

#include "bsml/shared/BSML.hpp"
#include "bsml/shared/BSML-Lite.hpp"
#include "HMUI/ViewController.hpp"
#include "UnityEngine/GameObject.hpp"
#include "UnityEngine/Transform.hpp"
#include "UploadServer.hpp"
#include "main.hpp"

namespace Cinema {

Settings& Settings::Get() {
    static Settings settings;
    return settings;
}

static bool GetBool(rapidjson::Value const& obj, char const* key, bool fallback) {
    auto itr = obj.FindMember(key);
    return itr != obj.MemberEnd() && itr->value.IsBool() ? itr->value.GetBool() : fallback;
}

static float GetFloat(rapidjson::Value const& obj, char const* key, float fallback) {
    auto itr = obj.FindMember(key);
    return itr != obj.MemberEnd() && itr->value.IsNumber() ? itr->value.GetFloat() : fallback;
}

void Settings::Load() {
    auto& doc = getConfig().config;
    if (!doc.IsObject()) {
        return;
    }
    Settings defaults;
    pluginEnabled = GetBool(doc, "PluginEnabled", defaults.pluginEnabled);
    overrideEnvironment = GetBool(doc, "OverrideEnvironment", defaults.overrideEnvironment);
    forceDisableEnvironmentOverrides = GetBool(doc, "ForceDisableEnvironmentOverrides", defaults.forceDisableEnvironmentOverrides);
    uploadServerEnabled = GetBool(doc, "UploadServerEnabled", defaults.uploadServerEnabled);
    screenOffset.x = GetFloat(doc, "ScreenOffsetX", 0);
    screenOffset.y = GetFloat(doc, "ScreenOffsetY", 0);
    screenOffset.z = GetFloat(doc, "ScreenOffsetZ", 0);
    screenSizeScale = GetFloat(doc, "ScreenSizeScale", defaults.screenSizeScale);
}

void Settings::Save() {
    auto& doc = getConfig().config;
    doc.SetObject();
    auto& allocator = doc.GetAllocator();
    doc.AddMember("PluginEnabled", pluginEnabled, allocator);
    doc.AddMember("OverrideEnvironment", overrideEnvironment, allocator);
    doc.AddMember("ForceDisableEnvironmentOverrides", forceDisableEnvironmentOverrides, allocator);
    doc.AddMember("UploadServerEnabled", uploadServerEnabled, allocator);
    doc.AddMember("ScreenOffsetX", screenOffset.x, allocator);
    doc.AddMember("ScreenOffsetY", screenOffset.y, allocator);
    doc.AddMember("ScreenOffsetZ", screenOffset.z, allocator);
    doc.AddMember("ScreenSizeScale", screenSizeScale, allocator);
    getConfig().Write();
}

static void SettingsDidActivate(HMUI::ViewController* self, bool firstActivation, bool, bool) {
    if (!firstActivation) {
        return;
    }

    auto& settings = Settings::Get();
    auto container = BSML::Lite::CreateScrollableSettingsContainer(self->transform);
    auto parent = container->transform;

    BSML::Lite::CreateToggle(parent, "Enable Cinema", settings.pluginEnabled, [](bool value) {
        Settings::Get().pluginEnabled = value;
        Settings::Get().Save();
    });
    BSML::Lite::CreateToggle(parent, "Override environment", settings.overrideEnvironment, [](bool value) {
        Settings::Get().overrideEnvironment = value;
        Settings::Get().Save();
    });
    BSML::Lite::CreateToggle(parent, "Disable all environment overrides", settings.forceDisableEnvironmentOverrides, [](bool value) {
        Settings::Get().forceDisableEnvironmentOverrides = value;
        Settings::Get().Save();
    });

    BSML::Lite::CreateText(parent, "Video upload page", TMPro::FontStyles::Italic, 3.5f);
    auto urlText = BSML::Lite::CreateText(parent, "", TMPro::FontStyles::Normal, 3.5f);
    auto updateUrlText = [urlText]() {
        if (!UploadServer::IsRunning()) {
            urlText->text = "Off. Turn on to upload videos from a PC or phone on the same Wi-Fi.";
        } else if (auto url = UploadServer::GetUrl()) {
            urlText->text = fmt::format("Open <color=#9fa8ff>{}</color> on a PC or phone on the same Wi-Fi", *url);
        } else {
            urlText->text = "On, but the Quest has no network connection";
        }
    };
    BSML::Lite::CreateToggle(parent, "Enable video upload page", settings.uploadServerEnabled, [updateUrlText](bool value) {
        Settings::Get().uploadServerEnabled = value;
        Settings::Get().Save();
        if (value) {
            UploadServer::Start();
        } else {
            UploadServer::Stop();
        }
        updateUrlText();
    });
    updateUrlText();

    BSML::Lite::CreateText(parent, "Screen adjustments (applied on top of the map's placement)", TMPro::FontStyles::Italic, 3.5f);

    auto makeOffsetSlider = [&](StringW label, float& field, float range) {
        return BSML::Lite::CreateSliderSetting(parent, label, 0.5f, field, -range, range, 0.2f, {0, 0}, [&field](float value) {
            field = value;
            Settings::Get().Save();
        });
    };
    auto xSlider = makeOffsetSlider("Left / right (m)", settings.screenOffset.x, 50);
    auto ySlider = makeOffsetSlider("Down / up (m)", settings.screenOffset.y, 30);
    auto zSlider = makeOffsetSlider("Closer / farther (m)", settings.screenOffset.z, 50);
    auto sizeSlider = BSML::Lite::CreateSliderSetting(parent, "Screen size", 0.05f, settings.screenSizeScale, 0.25f, 2.0f, 0.2f, {0, 0}, [](float value) {
        Settings::Get().screenSizeScale = value;
        Settings::Get().Save();
    });

    BSML::Lite::CreateUIButton(parent, "Reset screen adjustments", [xSlider, ySlider, zSlider, sizeSlider]() {
        auto& settings = Settings::Get();
        settings.screenOffset = {0, 0, 0};
        settings.screenSizeScale = 1.0f;
        settings.Save();
        xSlider->set_Value(0);
        ySlider->set_Value(0);
        zSlider->set_Value(0);
        sizeSlider->set_Value(1.0f);
    });
}

void RegisterSettingsMenu() {
    BSML::Register::RegisterSettingsMenu("Cinema", SettingsDidActivate, false);
}

}
