#include "main.hpp"

#include "CinemaPlayer.hpp"
#include "EnvironmentController.hpp"
#include "OffsetTuner.hpp"
#include "Settings.hpp"
#include "UploadServer.hpp"
#include "GlobalNamespace/AudioTimeSyncController.hpp"
#include "GlobalNamespace/GameplayCoreInstaller.hpp"
#include "GlobalNamespace/GameplayCoreSceneSetupData.hpp"
#include "GlobalNamespace/GameplayModifiers.hpp"
#include "GlobalNamespace/PracticeSettings.hpp"
#include "GlobalNamespace/StandardLevelScenesTransitionSetupDataSO.hpp"
#include "UnityEngine/GameObject.hpp"
#include "UnityEngine/SceneManagement/SceneManager.hpp"
#include "bsml/shared/BSML.hpp"
#include "custom-types/shared/register.hpp"
#include "scotland2/shared/modloader.h"
#include "songcore/shared/SongCore.hpp"
#include "songcore/shared/SongLoader/CustomBeatmapLevel.hpp"

static modloader::ModInfo modInfo{MOD_ID, VERSION, 0};




static constexpr auto CAPABILITY = "Cinema";




Configuration &getConfig() {
  static Configuration config(modInfo);
  return config;
}


using StandardLevelInit = void (GlobalNamespace::StandardLevelScenesTransitionSetupDataSO::*)(
    StringW, ByRef<GlobalNamespace::BeatmapKey>, GlobalNamespace::BeatmapLevel *, GlobalNamespace::OverrideEnvironmentSettings *,
    GlobalNamespace::ColorScheme *, bool, GlobalNamespace::ColorScheme *, GlobalNamespace::GameplayModifiers *,
    GlobalNamespace::PlayerSpecificSettings *, GlobalNamespace::PracticeSettings *, GlobalNamespace::EnvironmentsListModel *,
    GlobalNamespace::AudioClipAsyncLoader *, GlobalNamespace::BeatmapDataLoader *, GlobalNamespace::SettingsManager *, StringW,
    GlobalNamespace::BeatmapLevelsModel *, GlobalNamespace::BeatmapLevelsEntitlementModel *, bool, bool,
    System::Nullable_1<GlobalNamespace::RecordingToolManager_SetupData>);

MAKE_HOOK_MATCH(StandardLevelScenesTransitionSetupDataSO_Init,
                static_cast<StandardLevelInit>(&GlobalNamespace::StandardLevelScenesTransitionSetupDataSO::Init), void,
                GlobalNamespace::StandardLevelScenesTransitionSetupDataSO *self, StringW gameMode, ByRef<GlobalNamespace::BeatmapKey> beatmapKey,
                GlobalNamespace::BeatmapLevel *beatmapLevel, GlobalNamespace::OverrideEnvironmentSettings *overrideEnvironmentSettings,
                GlobalNamespace::ColorScheme *playerOverrideColorScheme, bool playerOverrideLightshowColors,
                GlobalNamespace::ColorScheme *beatmapOverrideColorScheme, GlobalNamespace::GameplayModifiers *gameplayModifiers,
                GlobalNamespace::PlayerSpecificSettings *playerSpecificSettings, GlobalNamespace::PracticeSettings *practiceSettings,
                GlobalNamespace::EnvironmentsListModel *environmentsListModel, GlobalNamespace::AudioClipAsyncLoader *audioClipAsyncLoader,
                GlobalNamespace::BeatmapDataLoader *beatmapDataLoader, GlobalNamespace::SettingsManager *settingsManager, StringW backButtonText,
                GlobalNamespace::BeatmapLevelsModel *beatmapLevelsModel, GlobalNamespace::BeatmapLevelsEntitlementModel *beatmapLevelsEntitlementModel,
                bool useTestNoteCutSoundEffects, bool startPaused, System::Nullable_1<GlobalNamespace::RecordingToolManager_SetupData> recordingToolData) {
  auto const &settings = Cinema::Settings::Get();

  try {
    if (!settings.pluginEnabled || settings.forceDisableEnvironmentOverrides) {
      PaperLogger.info("Cinema disabled: {}, environment override force disabled: {}", !settings.pluginEnabled,
                       settings.forceDisableEnvironmentOverrides);
    } else if (auto config = Cinema::LoadConfigForLevel(beatmapLevel); config && config->IsPlayable()) {
      overrideEnvironmentSettings = Cinema::EnvironmentController::GetOverrideEnvironmentSettings(
          *config, beatmapLevel, beatmapKey.heldRef, environmentsListModel, overrideEnvironmentSettings);
    }
  } catch (std::exception const &e) {
    PaperLogger.warn("Failed to apply environment override: {}", e.what());
  }

  StandardLevelScenesTransitionSetupDataSO_Init(self, gameMode, beatmapKey, beatmapLevel, overrideEnvironmentSettings, playerOverrideColorScheme,
                                                playerOverrideLightshowColors, beatmapOverrideColorScheme, gameplayModifiers, playerSpecificSettings,
                                                practiceSettings, environmentsListModel, audioClipAsyncLoader, beatmapDataLoader, settingsManager,
                                                backButtonText, beatmapLevelsModel, beatmapLevelsEntitlementModel, useTestNoteCutSoundEffects,
                                                startPaused, recordingToolData);
}


struct PendingVideo {
  Cinema::VideoConfig config;
  float songSpeed;
};
static std::optional<PendingVideo> pendingVideo;

MAKE_HOOK_MATCH(GameplayCoreInstaller_InstallBindings, &GlobalNamespace::GameplayCoreInstaller::InstallBindings, void,
                GlobalNamespace::GameplayCoreInstaller *self) {
  GameplayCoreInstaller_InstallBindings(self);

  pendingVideo.reset();
  if (!Cinema::Settings::Get().pluginEnabled) {
    return;
  }
  auto setupData = self->_sceneSetupData;
  if (!setupData) {
    return;
  }

  auto config = Cinema::LoadConfigForLevel(setupData->beatmapLevel);
  if (!config || !config->IsPlayable()) {
    return;
  }

  float songSpeed = setupData->gameplayModifiers ? setupData->gameplayModifiers->songSpeedMul : 1.0f;
  if (setupData->practiceSettings) {
    songSpeed = setupData->practiceSettings->songSpeedMul;
  }
  pendingVideo = PendingVideo{std::move(*config), songSpeed};
}

MAKE_HOOK_MATCH(AudioTimeSyncController_Start, &GlobalNamespace::AudioTimeSyncController::Start, void,
                GlobalNamespace::AudioTimeSyncController *self) {
  AudioTimeSyncController_Start(self);

  if (!pendingVideo) {
    return;
  }

  auto go = UnityEngine::GameObject::New_ctor("CinemaPlayer");

  UnityEngine::SceneManagement::SceneManager::MoveGameObjectToScene(go, self->gameObject->scene);
  go->AddComponent<Cinema::CinemaPlayer *>()->Init(self->_audioSource, std::move(pendingVideo->config), pendingVideo->songSpeed,
                                                   Cinema::PlayerMode::Gameplay);
  pendingVideo.reset();
}


static void OnSongsLoaded(std::span<SongCore::SongLoader::CustomBeatmapLevel *const> levels) {
  std::vector<Cinema::UploadServer::LevelInfo> infos;
  infos.reserve(levels.size());
  for (auto level : levels) {
    std::string mapper = level->allMappers.size() > 0 ? std::string(level->allMappers[0]) : "";
    infos.push_back({std::string(level->levelID), std::string(level->songName), std::string(level->songAuthorName), mapper,
                     std::string(level->customLevelPath)});
  }
  Cinema::UploadServer::SetLevels(std::move(infos));
}


MOD_EXTERN_FUNC void setup(CModInfo *info) noexcept {
  *info = modInfo.to_c();

  getConfig().Load();
  Cinema::Settings::Get().Load();


  Paper::Logger::RegisterFileContextId(PaperLogger.tag);

  PaperLogger.info("Completed setup!");
}


MOD_EXTERN_FUNC void late_load() noexcept {
  il2cpp_functions::Init();
  custom_types::Register::AutoRegister();
  BSML::Init();
  Cinema::RegisterSettingsMenu();
  Cinema::RegisterOffsetTunerTab();
  Cinema::VideoConfig::PreloadBundledConfigs();
  SongCore::API::Loading::GetSongsLoadedEvent() += OnSongsLoaded;
  if (Cinema::Settings::Get().uploadServerEnabled) {
    Cinema::UploadServer::Start();
  }
  SongCore::API::Capabilities::RegisterCapability(CAPABILITY);

  PaperLogger.info("Installing hooks...");
  INSTALL_HOOK(PaperLogger, StandardLevelScenesTransitionSetupDataSO_Init);
  INSTALL_HOOK(PaperLogger, GameplayCoreInstaller_InstallBindings);
  INSTALL_HOOK(PaperLogger, AudioTimeSyncController_Start);
  PaperLogger.info("Installed all hooks!");
}
