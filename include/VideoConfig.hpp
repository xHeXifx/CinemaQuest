#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "GlobalNamespace/BeatmapLevel.hpp"
#include "UnityEngine/Vector3.hpp"

namespace Cinema {

struct EnvironmentModification {
    std::string name;
    std::optional<std::string> parentName;
    std::optional<std::string> cloneFrom;
    std::optional<bool> active;
    std::optional<UnityEngine::Vector3> position;
    std::optional<UnityEngine::Vector3> rotation;
    std::optional<UnityEngine::Vector3> scale;
};

struct VideoConfig {
    std::optional<std::string> videoID;
    std::optional<std::string> title;
    std::optional<std::string> author;
    std::optional<std::string> videoFile;
    int duration = 0;
    int offset = 0;
    float playbackSpeed = 1.0f;
    bool loop = false;
    std::optional<float> endVideoAt;
    std::optional<UnityEngine::Vector3> screenPosition;
    std::optional<UnityEngine::Vector3> screenRotation;
    std::optional<float> screenHeight;
    std::optional<std::string> environmentName;
    bool disableDefaultModifications = false;
    std::vector<EnvironmentModification> environment;

    std::filesystem::path levelDir;
    bool bundledConfig = false;
    std::optional<std::filesystem::path> videoPath;

    float GetOffsetInSec() const { return offset / 1000.0f; }
    bool IsPlayable() const { return videoPath.has_value(); }
    bool EnvironmentModified() const { return !environment.empty() || screenPosition || screenHeight; }

    std::string VideoFileName() const;

    static std::optional<VideoConfig> LoadForLevel(std::filesystem::path const& levelDir, std::string_view levelID);
    static bool SaveBundledConfig(std::filesystem::path const& levelDir, std::string_view levelID);
    struct UserConfigFields {
        std::string videoID;
        std::string title;
        std::string author;
        int offset = 0;
        bool videoChanged = false;
    };
    static bool SaveUserConfig(std::filesystem::path const& levelDir, std::string_view levelID, UserConfigFields const& fields);
    static bool SaveOffset(std::filesystem::path const& levelDir, std::string_view levelID, int offset);
    static void PreloadBundledConfigs();
};

std::optional<VideoConfig> LoadConfigForLevel(GlobalNamespace::BeatmapLevel* level);

}
