#pragma once

#include "CinemaPlayer.hpp"
#include "GlobalNamespace/BeatmapLevel.hpp"
#include "GlobalNamespace/SongPreviewPlayer.hpp"
#include "GlobalNamespace/StandardLevelDetailViewController.hpp"
#include "TMPro/TextMeshProUGUI.hpp"
#include "UnityEngine/AudioClip.hpp"
#include "UnityEngine/MonoBehaviour.hpp"
#include "UnityEngine/UI/Button.hpp"
#include "bsml/shared/BSML/Components/Settings/SliderSetting.hpp"
#include "custom-types/shared/macros.hpp"
#include "VideoConfig.hpp"

DECLARE_CLASS_CODEGEN(Cinema, OffsetTuner, UnityEngine::MonoBehaviour) {
    DECLARE_INSTANCE_FIELD(UnityW<GlobalNamespace::StandardLevelDetailViewController>, levelDetail);
    DECLARE_INSTANCE_FIELD(UnityW<GlobalNamespace::SongPreviewPlayer>, songPreviewPlayer);
    DECLARE_INSTANCE_FIELD(UnityW<Cinema::CinemaPlayer>, previewPlayer);
    DECLARE_INSTANCE_FIELD(UnityW<UnityEngine::AudioSource>, snippetAudioSource);
    DECLARE_INSTANCE_FIELD(UnityW<UnityEngine::AudioClip>, snippetClip);
    DECLARE_INSTANCE_FIELD(GlobalNamespace::BeatmapLevel*, level);
    DECLARE_INSTANCE_FIELD(UnityW<TMPro::TextMeshProUGUI>, statusText);
    DECLARE_INSTANCE_FIELD(UnityW<BSML::SliderSetting>, startSlider);
    DECLARE_INSTANCE_FIELD(UnityW<UnityEngine::UI::Button>, playButton);

    DECLARE_INSTANCE_METHOD(void, Update);
    DECLARE_INSTANCE_METHOD(void, OnDisable);

    DECLARE_DEFAULT_CTOR();
    DECLARE_SIMPLE_DTOR();

   public:
    void BuildUI();

   private:
    void FindMenuObjects();
    void OnLevelChanged();
    void Play();
    void StartSnippet();
    void UpdateSnippet();
    UnityEngine::AudioSource* FindSnippetSource();
    void Stop(bool returnToPreview);
    void DestroyPreviewPlayer();
    void ChangeOffset(int delta);
    void Save();
    void Revert();
    void UpdateTexts();

    std::optional<VideoConfig> _config;
    int _offset = 0;
    int _savedOffset = 0;
    float _snippetStart = 0;
    float _snippetLength = 3;
    float _snippetStartTime = 0;
    float _snippetStartRealtime = 0;
    float _snippetEnd = 0;
    bool _playing = false;
    float _nextMenuSearch = 0;
};

namespace Cinema {
void RegisterOffsetTunerTab();
}
