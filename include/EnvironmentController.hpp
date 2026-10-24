#pragma once

#include <optional>
#include <string>

#include "GlobalNamespace/BeatmapKey.hpp"
#include "GlobalNamespace/BeatmapLevel.hpp"
#include "GlobalNamespace/EnvironmentsListModel.hpp"
#include "GlobalNamespace/OverrideEnvironmentSettings.hpp"
#include "UnityEngine/Vector3.hpp"
#include "VideoConfig.hpp"

namespace Cinema::EnvironmentController {

std::string GetEnvironmentName();


GlobalNamespace::OverrideEnvironmentSettings* GetOverrideEnvironmentSettings(VideoConfig const& config, GlobalNamespace::BeatmapLevel* level,
                                                                            GlobalNamespace::BeatmapKey& key,
                                                                            GlobalNamespace::EnvironmentsListModel* environmentsListModel,
                                                                            GlobalNamespace::OverrideEnvironmentSettings* overrideSettings);


bool ModifyGameScene(VideoConfig const& config);

struct ScreenPlacement {
    UnityEngine::Vector3 position;
    UnityEngine::Vector3 rotation;
    float height;
};

ScreenPlacement GetDefaultPlacement();

}
