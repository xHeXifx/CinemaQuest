#pragma once

#include "custom-types/shared/macros.hpp"
#include "UnityEngine/AudioSource.hpp"
#include "UnityEngine/MeshRenderer.hpp"
#include "UnityEngine/MonoBehaviour.hpp"
#include "UnityEngine/Video/VideoPlayer.hpp"
#include "VideoConfig.hpp"

namespace Cinema {
enum class PlayerMode {
    Gameplay,
    Preview,
};
}

DECLARE_CLASS_CODEGEN(Cinema, CinemaPlayer, UnityEngine::MonoBehaviour) {
    DECLARE_INSTANCE_FIELD(UnityW<UnityEngine::Video::VideoPlayer>, player);
    DECLARE_INSTANCE_FIELD(UnityW<UnityEngine::AudioSource>, audioSource);
    DECLARE_INSTANCE_FIELD(UnityW<UnityEngine::MeshRenderer>, screenRenderer);

    DECLARE_INSTANCE_METHOD(void, Update);

    DECLARE_DEFAULT_CTOR();
    DECLARE_SIMPLE_DTOR();

   public:
    void Init(UnityEngine::AudioSource* source, VideoConfig config, float songSpeed, PlayerMode mode);
    void SetAudioSource(UnityEngine::AudioSource* source);
    void SetOffset(int offsetMs);
    void SetActive(bool active);

   private:
    void UpdatePlayback();
    void CreateScreen();
    void SetScreenVisible(bool visible);
    void Resync(double referenceTime);
    double GetReferenceTime();
    double GetDisplayedTime();

    std::optional<VideoConfig> _config;
    float _songSpeed = 1.0f;
    PlayerMode _mode = PlayerMode::Gameplay;
    bool _active = true;
    bool _forceResync = false;
    bool _seekPending = false;
    int _failedResyncs = 0;
    bool _placementApplied = false;
    float _lastSeekRealtime = -100.0f;
    float _lastAudioTime = 0.0f;
    float _initRealtime = 0.0f;
    int _framesSinceInit = 0;
    bool _environmentModified = false;
    bool _screenAllowed = true;
};
