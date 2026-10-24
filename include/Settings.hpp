#pragma once

#include "UnityEngine/Vector3.hpp"

namespace Cinema {

struct Settings {
    bool pluginEnabled = true;
    bool overrideEnvironment = true;
    bool forceDisableEnvironmentOverrides = false;
    bool uploadServerEnabled = false;

    UnityEngine::Vector3 screenOffset = {0, 0, 0};
    float screenSizeScale = 1.0f;

    static Settings& Get();
    void Load();
    void Save();
};

void RegisterSettingsMenu();

}
