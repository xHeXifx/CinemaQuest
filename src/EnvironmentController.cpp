#include "EnvironmentController.hpp"

#include <algorithm>
#include <chrono>
#include <functional>
#include <regex>
#include <vector>

#include "GlobalNamespace/BeatmapCharacteristicSO.hpp"
#include "GlobalNamespace/EnvironmentInfoSO.hpp"
#include "GlobalNamespace/EnvironmentName.hpp"
#include "GlobalNamespace/MovementBeatmapEventEffect.hpp"
#include "UnityEngine/GameObject.hpp"
#include "UnityEngine/Resources.hpp"
#include "UnityEngine/SceneManagement/Scene.hpp"
#include "UnityEngine/SceneManagement/SceneManager.hpp"
#include "UnityEngine/Transform.hpp"
#include "Settings.hpp"
#include "main.hpp"

using namespace UnityEngine;
using namespace UnityEngine::SceneManagement;

namespace Cinema::EnvironmentController {

static constexpr auto HIDE_CINEMA_SCREEN_OBJECT_NAME = "HideCinemaScreen";


static constexpr std::array ENVIRONMENT_WHITELIST = {
    "BigMirrorEnvironment",    "OriginsEnvironment",      "BTSEnvironment",        "KDAEnvironment",      "RocketEnvironment",
    "DragonsEnvironment",      "Dragons2Environment",     "LinkinParkEnvironment", "KaleidoscopeEnvironment", "GlassDesertEnvironment",
    "MonstercatEnvironment",   "CrabRaveEnvironment",     "SkrillexEnvironment",   "WeaveEnvironment",    "PyroEnvironment",
    "EDMEnvironment",          "LizzoEnvironment",
};

static bool IsWhitelisted(std::string_view name) {
    return std::find(ENVIRONMENT_WHITELIST.begin(), ENVIRONMENT_WHITELIST.end(), name) != ENVIRONMENT_WHITELIST.end();
}

std::string GetEnvironmentName() {
    std::string environmentName = "MainMenu";
    auto sceneCount = SceneManager::get_sceneCount();
    for (int i = 0; i < sceneCount; i++) {
        std::string sceneName = SceneManager::GetSceneAt(i).name;
        if (sceneName.ends_with("Environment")) {
            environmentName = sceneName;
        }
    }
    return environmentName;
}

static GlobalNamespace::EnvironmentInfoSO* GetEnvironmentInfoFor(std::string_view serializedName) {
    for (auto info : Resources::FindObjectsOfTypeAll<GlobalNamespace::EnvironmentInfoSO*>()) {
        if (std::string(info->serializedName) == serializedName) {
            return info;
        }
    }
    return nullptr;
}

static GlobalNamespace::OverrideEnvironmentSettings* GetOverrideEnvironmentSettingsFor(std::string_view serializedName) {
    auto info = GetEnvironmentInfoFor(serializedName);
    if (!info) {
        PaperLogger.error("Could not find environment info for {}", serializedName);
        return nullptr;
    }
    auto settings = GlobalNamespace::OverrideEnvironmentSettings::New_ctor();
    settings->overrideEnvironments = true;
    settings->SetEnvironmentInfoForType(info->environmentType, info);
    return settings;
}

GlobalNamespace::OverrideEnvironmentSettings* GetOverrideEnvironmentSettings(VideoConfig const& config, GlobalNamespace::BeatmapLevel* level,
                                                                            GlobalNamespace::BeatmapKey& key,
                                                                            GlobalNamespace::EnvironmentsListModel* environmentsListModel,
                                                                            GlobalNamespace::OverrideEnvironmentSettings* overrideSettings) {
    if (config.environmentName) {
        if (auto settings = GetOverrideEnvironmentSettingsFor(*config.environmentName)) {
            PaperLogger.info("Overriding environment to {} as configured", *config.environmentName);
            return settings;
        }
    }

    if (config.EnvironmentModified()) {
        PaperLogger.info("Environment is modified, disabling environment override");
        return nullptr;
    }

    if (!Settings::Get().overrideEnvironment) {
        PaperLogger.info("Cinema's environment override disallowed by user");
        return overrideSettings;
    }

    auto environmentName = level->GetEnvironmentName(key.beatmapCharacteristic.ptr(), key.difficulty);
    auto mapEnvironmentInfo = environmentsListModel->GetEnvironmentInfoBySerializedNameSafe(environmentName._environmentName);
    if (!mapEnvironmentInfo) {
        return overrideSettings;
    }

    if (overrideSettings && overrideSettings->overrideEnvironments) {
        auto overrideInfo = overrideSettings->GetOverrideEnvironmentInfoForType(mapEnvironmentInfo->environmentType);
        if (overrideInfo && IsWhitelisted(std::string(overrideInfo->serializedName))) {
            PaperLogger.info("Environment override by user is in whitelist, allowing override");
            return overrideSettings;
        }
    }

    if (IsWhitelisted(std::string(mapEnvironmentInfo->serializedName))) {
        PaperLogger.info("Environment chosen by mapper is in whitelist");
        return nullptr;
    }

    if (auto bigMirror = GetOverrideEnvironmentSettingsFor("BigMirrorEnvironment")) {
        PaperLogger.info("Overwriting environment to Big Mirror");
        return bigMirror;
    }
    return overrideSettings;
}


struct EnvironmentObject {
    GameObject* gameObject;
    std::string name;
    std::string parentName;
    bool activeInHierarchy;

    Transform* transform() const { return gameObject->transform.ptr(); }
    void SetActive(bool active) const { gameObject->SetActive(active); }
};

using Predicate = std::function<bool(EnvironmentObject const&)>;

class SceneObjects {
   public:
    explicit SceneObjects(std::string const& environmentName) {
        auto activeScene = SceneManager::GetActiveScene().handle;
        auto environmentScene = SceneManager::GetSceneByName(environmentName).handle;

        auto pcInitScene = SceneManager::GetSceneByName("PCInit").handle;

        auto gameObjects = Resources::FindObjectsOfTypeAll<GameObject*>();
        objects.reserve(gameObjects.size());
        for (auto go : gameObjects) {
            auto scene = go->scene.handle;

            if (scene != activeScene && scene != environmentScene && scene != pcInitScene) {
                continue;
            }
            auto parent = go->transform->parent;
            objects.push_back({go, go->name, parent ? std::string(parent->name) : "", go->activeInHierarchy});
        }
    }

    std::vector<EnvironmentObject const*> Where(Predicate const& predicate) const {
        std::vector<EnvironmentObject const*> result;
        for (auto const& obj : objects) {
            if (predicate(obj)) {
                result.push_back(&obj);
            }
        }
        return result;
    }

    EnvironmentObject const* Last(Predicate const& predicate) const {
        for (auto itr = objects.rbegin(); itr != objects.rend(); ++itr) {
            if (predicate(*itr)) {
                return &*itr;
            }
        }
        return nullptr;
    }


    std::vector<EnvironmentObject const*> ActiveContaining(std::string_view part) const {
        return Where([part](auto const& x) { return x.name.find(part) != std::string::npos && x.activeInHierarchy; });
    }
    EnvironmentObject const* LastActiveNamed(std::string_view name) const {
        return Last([name](auto const& x) { return x.name == name && x.activeInHierarchy; });
    }

    size_t Count() const { return objects.size(); }

   private:
    std::vector<EnvironmentObject> objects;
};

static void SetPosition(EnvironmentObject const* obj, Vector3 position) {
    if (obj) {
        obj->transform()->position = position;
    }
}

static void SetScale(EnvironmentObject const* obj, Vector3 scale) {
    if (obj) {
        obj->transform()->localScale = scale;
    }
}

static void Deactivate(EnvironmentObject const* obj) {
    if (obj) {
        obj->SetActive(false);
    }
}


static void SpreadX(std::vector<EnvironmentObject const*> const& objects, float newX) {
    for (auto obj : objects) {
        auto pos = obj->transform()->position;
        obj->transform()->position = Vector3(pos.x < 0 ? -newX : newX, pos.y, pos.z);
    }
}

static void SpreadChildrenX(std::vector<EnvironmentObject const*> const& objects, float newX) {
    for (auto obj : objects) {
        auto t = obj->transform();
        for (int i = 0; i < t->childCount; i++) {
            auto child = t->GetChild(i);
            auto pos = child->position;
            child->position = Vector3(pos.x < 0 ? -newX : newX, pos.y, pos.z);
        }
    }
}


static std::optional<int> GetIndex(std::string name, std::string const& baseName, std::regex const& pattern) {
    if (name == baseName) {
        name += " (0)";
    }
    std::smatch match;
    if (!std::regex_search(name, match, pattern)) {
        PaperLogger.debug("Could not find index of: {}", name);
        return std::nullopt;
    }
    return std::stoi(match[1].str());
}

static void DefaultSceneModifications(SceneObjects const& objects, std::string const& environmentName) {

    Deactivate(objects.Last([](auto const& x) { return (x.name == "FrontLights" || x.name == "FrontLight") && x.activeInHierarchy; }));


    Deactivate(objects.Last([](auto const& x) { return x.name == "DirectionalLight" && x.parentName == "CoreLighting"; }));

    if (environmentName == "NiceEnvironment" || environmentName == "BigMirrorEnvironment") {
        static std::regex const laserPattern("^DoubleColorLaser \\(([0-9])\\)$");
        for (auto laser : objects.ActiveContaining("DoubleColorLaser")) {
            auto i = GetIndex(laser->name, "DoubleColorLaser", laserPattern);
            if (!i) {
                continue;
            }
            float sign = *i % 2 == 0 ? -1 : 1;
            auto pos = laser->transform()->position;
            laser->transform()->position = Vector3(pos.x + 18.0f * sign, pos.y, pos.z - 28.5f);
        }

        SpreadChildrenX(objects.ActiveContaining("RotatingLasersPair"), 20);
    } else if (environmentName == "BTSEnvironment") {
        Deactivate(objects.LastActiveNamed("MagicDoorSprite"));

        static std::regex const pillarPattern("PillarPair \\(([0-9])\\)$");
        std::vector<Vector3> movementEffectStartPositions;
        for (auto pillarPair : objects.ActiveContaining("PillarPair")) {
            auto pillarPairName = pillarPair->name;
            if (pillarPairName == "PillarPair" || pillarPairName == "SmallPillarPair") {
                pillarPairName += " (0)";
            }
            std::smatch match;
            if (!std::regex_search(pillarPairName, match, pillarPattern)) {
                PaperLogger.debug("Could not find index of: {}", pillarPairName);
                continue;
            }
            auto i = std::stoi(match[1].str());

            auto children = objects.Where([&](auto const& x) {
                return x.name.find("Pillar") != std::string::npos && x.parentName == pillarPair->name && x.activeInHierarchy;
            });
            for (auto child : children) {
                auto childPos = child->transform()->position;
                float sign = child->name == "PillarL" ? -1 : 1;
                auto newX = (16.0f + i * 2.3f) * sign;
                child->transform()->position = Vector3(newX, childPos.y, childPos.z);
                if (pillarPairName.find("SmallPillarPair") != std::string::npos) {
                    movementEffectStartPositions.emplace_back(newX, 0, 0);
                }
            }

            auto pairPos = pillarPair->transform()->position;
            pillarPair->transform()->position = Vector3(pairPos.x, pairPos.y - 2.0f, pairPos.z);
        }

        GlobalNamespace::MovementBeatmapEventEffect* movementEffect = nullptr;
        for (auto effect : Resources::FindObjectsOfTypeAll<GlobalNamespace::MovementBeatmapEventEffect*>()) {
            if (effect->name == "PillarsMovementEffect") {
                movementEffect = effect;
            }
        }
        if (movementEffect) {
            std::reverse(movementEffectStartPositions.begin(), movementEffectStartPositions.end());
            movementEffect->_startLocalPositions = ArrayW<Vector3>(movementEffectStartPositions);
        } else {
            PaperLogger.warn("BTS movement effect not found");
        }
    } else if (environmentName == "OriginsEnvironment") {
        SpreadX(objects.Where([](auto const& x) { return x.name == "Spectrogram" && x.activeInHierarchy; }), 12);
    } else if (environmentName == "KDAEnvironment") {
        auto construction = objects.Last([](auto const& x) { return x.name == "Construction" && x.parentName != "PlayersPlace" && x.activeInHierarchy; });

        SetScale(construction, Vector3(1, 2, 1));

        for (auto tentacle : objects.ActiveContaining("Tentacle")) {
            auto pos = tentacle->transform()->position;
            auto rot = tentacle->transform()->eulerAngles;
            float sign = pos.x < 0 ? -1 : 1;
            tentacle->transform()->position = Vector3(15 * sign, pos.y, pos.z);
            tentacle->transform()->eulerAngles = Vector3(rot.x, -135 * sign, rot.z);
        }

        SpreadX(objects.Where([](auto const& x) {
            return x.name.find("Laser") != std::string::npos && x.name.find("RotatingLasersPair") == std::string::npos && x.activeInHierarchy;
        }),
                10);

        for (auto glowLine : objects.ActiveContaining("GlowTopLine")) {
            auto pos = glowLine->transform()->position;
            glowLine->transform()->position = Vector3(pos.x, 20.0f, pos.z);
        }


        SpreadChildrenX(objects.ActiveContaining("RotatingLasersPair"), 18);
    } else if (environmentName == "RocketEnvironment") {
        SpreadX(objects.ActiveContaining("RocketCar"), 16);
        SetScale(objects.LastActiveNamed("RocketArena"), Vector3(2.38f, 2, 1));

        if (auto arenaLight = objects.LastActiveNamed("RocketArenaLight")) {
            arenaLight->transform()->position = Vector3(0, 5.8f, 42.4f);
            arenaLight->transform()->localScale = Vector3(2.38f, 1, 1);
            arenaLight->transform()->eulerAngles = Vector3(90, 180, 0);
        }

        if (auto gateLight = objects.LastActiveNamed("RocketGateLight")) {
            gateLight->transform()->position = Vector3(0, -3, 64);
            gateLight->transform()->localScale = Vector3(2.6f, 1, 4.5f);
        }
    } else if (environmentName == "DragonsEnvironment") {
        SpreadX(objects.Where([](auto const& x) { return x.name == "Spectrogram" && x.activeInHierarchy; }), 16);

        SpreadChildrenX(objects.ActiveContaining("RotatingLasersPair"), 20);

        for (auto topConstruction : objects.ActiveContaining("TopConstruction")) {
            auto pos = topConstruction->transform()->position;
            topConstruction->transform()->position = Vector3(pos.x, 27.0f, pos.z);
        }

        if (auto hallConstruction = objects.LastActiveNamed("HallConstruction")) {
            auto pos = hallConstruction->transform()->position;
            hallConstruction->transform()->position = Vector3(pos.x, 22.0f, pos.z);
        }

        for (auto ring : objects.ActiveContaining("PanelsTrackLaneRing")) {
            ring->SetActive(false);
        }
    } else if (environmentName == "Dragons2Environment") {
        Deactivate(objects.Last([](auto const& x) { return x.name == "Left" && x.parentName == "CoreLighting"; }));
        Deactivate(objects.Last([](auto const& x) { return x.name == "Right" && x.parentName == "CoreLighting"; }));

        for (auto spectrogram : objects.Where([](auto const& x) { return x.name == "Spectrogram" && x.activeInHierarchy; })) {
            auto pos = spectrogram->transform()->position;
            float newX = 18;
            float newYRotation = 10;
            if (pos.x < 0) {
                newX *= -1;
                newYRotation = 180 - newYRotation;
            }
            spectrogram->transform()->position = Vector3(newX, pos.y, pos.z);
            spectrogram->transform()->eulerAngles = Vector3(0, newYRotation, 0);
        }

        if (auto hallConstruction = objects.LastActiveNamed("HallConstruction")) {
            auto pos = hallConstruction->transform()->position;
            hallConstruction->transform()->position = Vector3(pos.x, 17.2f, pos.z);
            hallConstruction->transform()->localScale = Vector3(1.0f, 0.7f, 1.0f);
        }

        for (auto ring : objects.ActiveContaining("PanelsTrackLaneRing")) {
            ring->SetActive(false);
        }
    } else if (environmentName == "LinkinParkEnvironment") {
        Deactivate(objects.Last([](auto const& x) { return x.name == "Logo" && x.parentName == "Environment" && x.activeInHierarchy; }));

        Vector3 const environmentScale(4.0f, 3.0f, 3.0f);
        Vector3 const invertedScale(1 / environmentScale.x, 1 / environmentScale.y, 1 / environmentScale.z);

        SetScale(objects.LastActiveNamed("Environment"), environmentScale);

        if (auto trackConstruction = objects.LastActiveNamed("TrackConstruction")) {
            trackConstruction->transform()->position = Vector3(0.9f, 0.0f, 106.5f);
            trackConstruction->transform()->localScale = invertedScale;
        }
        if (auto trackMirror = objects.LastActiveNamed("TrackMirror")) {
            trackMirror->transform()->position = Vector3(0.3f, 0.0f, 6.55f);
            trackMirror->transform()->localScale = invertedScale;
        }
        if (auto trackShadow = objects.LastActiveNamed("TrackShadow")) {
            trackShadow->transform()->position = Vector3(0.0f, -0.3f, 126.1f);
            trackShadow->transform()->localScale = invertedScale;
        }
        SetScale(objects.LastActiveNamed("PlayersPlace"), invertedScale);
        SetScale(objects.LastActiveNamed("PlayersPlaceShadow"), invertedScale);
        SetScale(objects.LastActiveNamed("NarrowGameHUD"), invertedScale);
    } else if (environmentName == "KaleidoscopeEnvironment") {
        Deactivate(objects.Last([](auto const& x) { return x.name == "Construction" && x.parentName != "PlayersPlace" && x.activeInHierarchy; }));
        Deactivate(objects.LastActiveNamed("TrackMirror"));

        constexpr float coneOffset = 2.5f;
        auto shiftCones = [&](std::string_view coneName, float offset) {
            for (auto cone : objects.Where([&](auto const& x) { return x.name == coneName && x.parentName == "ConeRing(Clone)" && x.activeInHierarchy; })) {
                auto localPos = cone->transform()->localPosition;
                cone->transform()->localPosition = Vector3(localPos.x, localPos.y + offset, localPos.z);
            }
        };
        shiftCones("Cone", coneOffset);
        shiftCones("Cone (1)", -coneOffset);
    } else if (environmentName == "GlassDesertEnvironment") {

        PaperLogger.warn("GlassDesertEnvironment screen rotation is not supported yet");
    } else if (environmentName == "InterscopeEnvironment") {

        Deactivate(objects.LastActiveNamed("Plane (1)"));
        Deactivate(objects.LastActiveNamed("Plane (4)"));
        for (auto light : objects.ActiveContaining("NeonTop")) {
            light->SetActive(false);
        }
    } else if (environmentName == "CrabRaveEnvironment" || environmentName == "MonstercatEnvironment") {
        for (auto ring : objects.Where([](auto const& x) { return x.name == "SmallTrackLaneRing(Clone)" && x.activeInHierarchy; })) {
            ring->transform()->localScale = Vector3(3.0f, 3.0f, 1.0f);
        }

        SetPosition(objects.LastActiveNamed("GlowLineL"), Vector3(-10.0f, -1.5f, 9.5f));
        SetPosition(objects.LastActiveNamed("GlowLineR"), Vector3(10.0f, -1.5f, 9.5f));
        SetPosition(objects.LastActiveNamed("GlowLineL (1)"), Vector3(-12.0f, 1.5f, -20.0f));
        SetPosition(objects.LastActiveNamed("GlowLineR (1)"), Vector3(12.0f, 1.5f, -20.0f));

        for (auto logo : objects.ActiveContaining("MonstercatLogo")) {
            logo->SetActive(false);
        }

        for (auto glowTopLine : objects.ActiveContaining("GlowTopLine")) {
            glowTopLine->transform()->localScale = Vector3(2, 2, 2);
        }
        SetPosition(objects.LastActiveNamed("GlowTopLine (5)"), Vector3(0.0f, 12.0f, 0.0f));
        SetPosition(objects.LastActiveNamed("GlowTopLine (6)"), Vector3(-3.0f, 12.0f, 0.0f));
        SetPosition(objects.LastActiveNamed("GlowTopLine (7)"), Vector3(3.0f, 12.0f, 0.0f));
        SetPosition(objects.LastActiveNamed("GlowTopLine (8)"), Vector3(-6.0f, 12.0f, 0.0f));
        SetPosition(objects.LastActiveNamed("GlowTopLine (9)"), Vector3(6.0f, 12.0f, 0.0f));
        SetPosition(objects.LastActiveNamed("GlowTopLine (10)"), Vector3(-9.0f, 12.0f, 1.0f));
        SetPosition(objects.LastActiveNamed("GlowTopLine (11)"), Vector3(9.0f, 12.0f, 1.0f));

        for (auto rotatingLaser : objects.ActiveContaining("RotatingLasersPair")) {
            rotatingLaser->transform()->eulerAngles = Vector3(-15, 0, 180);
        }

        if (auto farBuildings = objects.LastActiveNamed("FarBuildings")) {
            farBuildings->transform()->localScale = Vector3(1.1f, 1.0f, 1.0f);
            farBuildings->transform()->position = Vector3(0.0f, -5.0f, -30.0f);
        }

        if (auto construction = objects.Last([](auto const& x) { return x.name == "Construction" && x.parentName != "PlayersPlace" && x.activeInHierarchy; })) {
            construction->transform()->position = Vector3(0.0f, -1.0f, 2.0f);
            construction->transform()->localScale = Vector3(1.5f, 1, 1);
        }

        if (auto vConstruction = objects.LastActiveNamed("VConstruction")) {
            vConstruction->transform()->position = Vector3(0.0f, 2.0f, -1.0f);
            vConstruction->transform()->localScale = Vector3(1.0f, 1, 0.7f);
        }

        if (auto trackMirror = objects.LastActiveNamed("TrackMirror")) {
            trackMirror->transform()->position = Vector3(0.0f, -1.0f, 9.75f);
            trackMirror->transform()->localScale = Vector3(1.8f, 1, 1);
        }


        struct LaserPlacement {
            char const* name;
            float x, z, rotZ;
        };
        static constexpr LaserPlacement lasers[] = {
            {"Laser (4)", 12.4f, 9.9f, -30},  {"Laser (5)", -12.4f, 9.9f, 30},  {"Laser (7)", 12.4f, 9.7f, -30},
            {"Laser (8)", -12.4f, 9.7f, 30},  {"Laser (9)", 12.4f, 9.5f, -30},  {"Laser (10)", -12.4f, 9.5f, 30},
            {"Laser (11)", 12.4f, 9.3f, -30}, {"Laser (12)", -12.4f, 9.3f, 30},
        };
        for (auto const& placement : lasers) {
            if (auto laser = objects.LastActiveNamed(placement.name)) {
                laser->transform()->position = Vector3(placement.x, 10.0f, placement.z);
                laser->transform()->eulerAngles = Vector3(0.0f, 0, placement.rotZ);
            }
        }
    } else if (environmentName == "SkrillexEnvironment") {
        SetPosition(objects.LastActiveNamed("SkrillexLogo"), Vector3(-0.23f, 15.5f, 60.0f));
        SetPosition(objects.LastActiveNamed("SkrillexLogo (1)"), Vector3(-0.23f, -15.5f, 60.0f));
    } else if (environmentName == "PyroEnvironment") {
        Deactivate(objects.LastActiveNamed("PyroLogo"));


        if (auto left = objects.LastActiveNamed("LightGroupLeft")) {
            left->transform()->position = Vector3(-11.1f, 0.99f, 59.0f);
            left->transform()->localScale = Vector3(0.7f, 0.7f, 0.7f);
        }
        if (auto right = objects.LastActiveNamed("LightGroupRight")) {
            right->transform()->position = Vector3(11.1f, 0.99f, 59.0f);
            right->transform()->localScale = Vector3(0.7f, 0.7f, 0.7f);
        }

        Deactivate(objects.LastActiveNamed("Video"));
        for (auto screen : objects.ActiveContaining("ScreenSetup")) {
            screen->SetActive(false);
        }

        SetPosition(objects.LastActiveNamed("LightBoxesScaffoldingLeft"), Vector3(-37.65f, -2.92f, 50.54f));
        SetPosition(objects.LastActiveNamed("LightBoxesScaffoldingRight"), Vector3(37.65f, -2.92f, 50.54f));
        SetPosition(objects.LastActiveNamed("LightGroupCloserLeft"), Vector3(-35.13f, -0.02f, 66.74f));
        SetPosition(objects.LastActiveNamed("LightGroupCloserRight"), Vector3(35.13f, -0.02f, 66.74f));
        SetPosition(objects.LastActiveNamed("Stairs"), Vector3(0.0f, -1.0f, 51.31f));
        SetPosition(objects.LastActiveNamed("CrowdFlipbookGroup"), Vector3(-4.83f, -0.40f, -1.80f));


    } else if (environmentName == "LizzoEnvironment") {
        if (auto rainbow = objects.LastActiveNamed("Rainbow")) {
            rainbow->transform()->position = Vector3(0.0f, 2.1f, 65.51f);
            rainbow->transform()->localScale = Vector3(2.0f, 2.0f, 2.0f);
        }
        if (auto rainbowLights = objects.LastActiveNamed("BehindRainbowSpotlights")) {
            rainbowLights->transform()->position = Vector3(0.0f, 2.1f, 62.24f);
            rainbowLights->transform()->localScale = Vector3(2.0f, 2.0f, 2.0f);
        }
    } else if (environmentName == "WeaveEnvironment") {
        SetPosition(objects.LastActiveNamed("LightGroup14"), Vector3(-3.85f, 1.50f, 20.90f));
        SetPosition(objects.LastActiveNamed("LightGroup15"), Vector3(3.85f, 1.50f, 20.90f));
    } else if (environmentName == "EDMEnvironment") {
        SetPosition(objects.LastActiveNamed("Spectrograms"), Vector3(0.0f, -2.0f, 1.2f));
    }
}


static EnvironmentModification TranslateNameForBackwardsCompatibility(EnvironmentModification modification, std::string const& environmentName) {
    if (environmentName == "BigMirrorEnvironment") {
        static std::unordered_map<std::string, std::string> const renames = {
            {"GlowLineL", "NeonTubeDirectionalL"},
            {"GlowLineL2", "NeonTubeDirectionalFL"},
            {"GlowLineR", "NeonTubeDirectionalR"},
            {"GlowLineR2", "NeonTubeDirectionalFR"},
        };
        if (auto itr = renames.find(modification.name); itr != renames.end()) {
            modification.name = itr->second;
        }
        if (modification.parentName == "Buildings") {
            modification.parentName = "Environment";
        }
    } else if (environmentName == "NiceEnvironment") {
        if (modification.name == "TrackLaneRing(Clone)") {
            modification.name = "SmallTrackLaneRing(Clone)";
        }
    }
    return modification;
}

static void VideoConfigSceneModifications(SceneObjects const& objects, VideoConfig const& config, std::string const& environmentName) {
    for (auto const& original : config.environment) {
        if (original.cloneFrom) {

            PaperLogger.warn("Cloning objects is not supported yet, skipping {} (cloneFrom {})", original.name, *original.cloneFrom);
            continue;
        }

        auto modification = TranslateNameForBackwardsCompatibility(original, environmentName);
        auto selected = objects.Where([&](auto const& x) { return x.name == modification.name && (!modification.parentName || x.parentName == *modification.parentName); });
        if (selected.empty()) {
            PaperLogger.error("Failed to find object: name={}, parentName={}", modification.name, modification.parentName.value_or("null"));
            continue;
        }

        for (auto obj : selected) {

            if (environmentName == "PyroEnvironment" && obj->name.starts_with("LightGroup") && modification.position &&
                std::abs(modification.position->y - 1.99f) < 0.1f) {
                continue;
            }
            if (modification.active) {
                obj->SetActive(*modification.active);
            }
            if (modification.position) {
                obj->transform()->position = *modification.position;
            }
            if (modification.rotation) {
                obj->transform()->eulerAngles = *modification.rotation;
            }
            if (modification.scale) {
                obj->transform()->localScale = *modification.scale;
            }
        }
    }
}

bool ModifyGameScene(VideoConfig const& config) {
    auto start = std::chrono::steady_clock::now();
    auto environmentName = GetEnvironmentName();
    SceneObjects objects(environmentName);
    PaperLogger.info("Modifying environment {} ({} objects)", environmentName, objects.Count());


    bool screenVisible = !objects.Last([](auto const& x) { return x.name == HIDE_CINEMA_SCREEN_OBJECT_NAME; });
    if (!screenVisible) {
        PaperLogger.info("Hiding video screen due to custom platform");
    }

    if (!config.disableDefaultModifications) {
        DefaultSceneModifications(objects, environmentName);
    }
    VideoConfigSceneModifications(objects, config, environmentName);

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
    PaperLogger.info("Modified environment in {} ms", elapsed.count());
    return screenVisible;
}

ScreenPlacement GetDefaultPlacement() {

    static std::unordered_map<std::string, ScreenPlacement> const placements = {
        {"LinkinParkEnvironment", {{0.0f, 6.2f, 52.7f}, {0, 0, 0}, 16.0f}},
        {"BTSEnvironment", {{0, 12.4f, 80.0f}, {-7, 0, 0}, 25.0f}},
        {"OriginsEnvironment", {{0, 12.4f, 66.7f}, {-7, 0, 0}, 25.0f}},
        {"KaleidoscopeEnvironment", {{0.0f, -0.5f, 35.0f}, {0, 0, 0}, 12.0f}},
        {"InterscopeEnvironment", {{0.0f, 6.3f, 37.0f}, {0, 0, 0}, 12.5f}},
        {"CrabRaveEnvironment", {{0.0f, 5.46f, 40.0f}, {-5.0f, 0.0f, 0.0f}, 13.0f}},
        {"MonstercatEnvironment", {{0.0f, 5.46f, 40.0f}, {-5.0f, 0.0f, 0.0f}, 13.0f}},
        {"SkrillexEnvironment", {{0.0f, 1.5f, 40.0f}, {0, 0, 0}, 12.0f}},
        {"WeaveEnvironment", {{0.0f, 1.5f, 21.0f}, {0, 0, 0}, 4.3f}},
        {"PyroEnvironment", {{0.0f, 12.0f, 60.0f}, {0, 0, 0}, 24.0f}},
        {"EDMEnvironment", {{0.0f, 1.5f, 25.0f}, {0, 0, 0}, 8.0f}},
        {"LizzoEnvironment", {{0.0f, 8.0f, 63.0f}, {0, 0, 0}, 16.0f}},
        {"Dragons2Environment", {{0.0f, 5.8f, 67.0f}, {0, 0, 0}, 33.0f}},
    };
    if (auto itr = placements.find(GetEnvironmentName()); itr != placements.end()) {
        return itr->second;
    }

    return {{0, 12.4f, 67.8f}, {-7, 0, 0}, 25.0f};
}

}
