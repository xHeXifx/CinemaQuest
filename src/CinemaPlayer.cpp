#include "CinemaPlayer.hpp"

#include <cmath>

#include "UnityEngine/AudioSource.hpp"
#include "UnityEngine/Collider.hpp"
#include "UnityEngine/GameObject.hpp"
#include "UnityEngine/LayerMask.hpp"
#include "UnityEngine/Material.hpp"
#include "UnityEngine/PrimitiveType.hpp"
#include "UnityEngine/Shader.hpp"
#include "UnityEngine/Texture.hpp"
#include "UnityEngine/Time.hpp"
#include "UnityEngine/Transform.hpp"
#include "UnityEngine/Video/VideoAudioOutputMode.hpp"
#include "UnityEngine/Video/VideoRenderMode.hpp"
#include "UnityEngine/Video/VideoSource.hpp"
#include "EnvironmentController.hpp"
#include "Settings.hpp"
#include "main.hpp"

DEFINE_TYPE(Cinema, CinemaPlayer);

using namespace UnityEngine;

namespace Cinema {


static constexpr double START_LEAD = 0.0667;
static constexpr double MAX_DESYNC = 0.3;

static constexpr float SEEK_COOLDOWN = 1.0f;

static constexpr double MAX_VISIBLE_DESYNC = 1.0;

static constexpr int ENVIRONMENT_MODIFICATION_FRAMES = 3;
static constexpr float ENVIRONMENT_MODIFICATION_DELAY = 0.1f;


static Vector3 const MENU_POSITION(0, 4.0f, 16.0f);
static constexpr float MENU_HEIGHT = 8.0f;

static constexpr double PREVIEW_MAX_DESYNC = 0.1;
static constexpr float PREVIEW_SEEK_COOLDOWN = 0.5f;

void CinemaPlayer::Init(AudioSource* source, VideoConfig config, float songSpeed, PlayerMode mode) {
    audioSource = source;
    _mode = mode;
    _songSpeed = songSpeed;
    _config = std::move(config);
    _initRealtime = Time::get_realtimeSinceStartup();

    CreateScreen();

    player = gameObject->AddComponent<Video::VideoPlayer*>();
    player->playOnAwake = false;
    player->waitForFirstFrame = true;
    player->isLooping = _config->loop;
    player->source = Video::VideoSource::Url;
    player->renderMode = Video::VideoRenderMode::MaterialOverride;
    player->targetMaterialRenderer = screenRenderer.ptr();
    player->targetMaterialProperty = "_MainTex";

    player->audioOutputMode = Video::VideoAudioOutputMode::None;
    player->playbackSpeed = _songSpeed * _config->playbackSpeed;
    player->url = "file://" + _config->videoPath->string();
    player->Prepare();

    PaperLogger.info("Preparing video {} (song speed {})", _config->videoPath->string(), _songSpeed);
}

void CinemaPlayer::CreateScreen() {
    auto screen = GameObject::CreatePrimitive(PrimitiveType::Quad);
    screen->name = "CinemaScreen";
    if (auto collider = screen->GetComponent<Collider*>()) {
        Object::Destroy(collider);
    }
    screen->layer = LayerMask::NameToLayer("Environment");
    screen->transform->SetParent(transform, false);

    auto shader = Shader::Find("Unlit/Texture");
    if (!shader) {
        PaperLogger.warn("Unlit/Texture shader not found, falling back to UI/Default");
        shader = Shader::Find("UI/Default");
    }
    screenRenderer = screen->GetComponent<MeshRenderer*>();
    screenRenderer->material = Material::New_ctor(shader);

    if (_mode == PlayerMode::Preview) {
        screen->transform->position = MENU_POSITION;
        screen->transform->localScale = Vector3(MENU_HEIGHT * (16.0f / 9.0f), MENU_HEIGHT, 1);
        SetScreenVisible(false);
        return;
    }


    auto defaultPlacement = EnvironmentController::GetDefaultPlacement();
    auto const& settings = Settings::Get();
    auto t = screen->transform;
    t->position = Vector3::op_Addition(_config->screenPosition.value_or(defaultPlacement.position), settings.screenOffset);
    t->eulerAngles = _config->screenRotation.value_or(defaultPlacement.rotation);
    auto height = _config->screenHeight.value_or(defaultPlacement.height) * settings.screenSizeScale;
    t->localScale = Vector3(height * (16.0f / 9.0f), height, 1);

    SetScreenVisible(false);
}

void CinemaPlayer::SetScreenVisible(bool visible) {
    visible = visible && _screenAllowed;
    if (screenRenderer && screenRenderer->enabled != visible) {
        screenRenderer->enabled = visible;
    }
}


void CinemaPlayer::SetAudioSource(AudioSource* source) {
    audioSource = source;
    _forceResync = true;
}

void CinemaPlayer::SetOffset(int offsetMs) {
    if (_config && _config->offset != offsetMs) {
        _config->offset = offsetMs;
        _forceResync = true;
    }
}

void CinemaPlayer::SetActive(bool active) {
    _active = active;
    if (!active) {
        SetScreenVisible(false);
        if (player && player->isPlaying) {
            player->Pause();
        }
    }
}

double CinemaPlayer::GetReferenceTime() {
    auto audioTime = audioSource->time;
    return audioTime * _config->playbackSpeed + _config->GetOffsetInSec();
}


double CinemaPlayer::GetDisplayedTime() {
    auto frame = player->frame;
    auto frameRate = player->frameRate;
    if (frame >= 0 && frameRate > 0) {
        return frame / static_cast<double>(frameRate);
    }
    return player->time;
}

void CinemaPlayer::Resync(double referenceTime) {
    auto target = referenceTime + START_LEAD;

    if (_failedResyncs > 0 && player->frameRate > 0) {
        player->frame = static_cast<int64_t>(target * player->frameRate);
    } else {
        player->time = target;
    }
    _lastSeekRealtime = Time::get_realtimeSinceStartup();
}

void CinemaPlayer::Update() {

    try {
        UpdatePlayback();
    } catch (std::exception const& e) {
        PaperLogger.error("Video playback error, disabling video: {}", e.what());
        _config.reset();
        SetScreenVisible(false);
    }
}

void CinemaPlayer::UpdatePlayback() {
    if (!_config || !player || !audioSource || !_active) {
        return;
    }

    if (_mode == PlayerMode::Gameplay && !_environmentModified && ++_framesSinceInit >= ENVIRONMENT_MODIFICATION_FRAMES &&
        Time::get_realtimeSinceStartup() - _initRealtime >= ENVIRONMENT_MODIFICATION_DELAY) {
        _environmentModified = true;
        _screenAllowed = EnvironmentController::ModifyGameScene(*_config);
    }

    if (!player->isPrepared) {
        return;
    }

    if (!_placementApplied) {

        if (auto texture = player->texture; texture && texture->height > 0) {
            auto t = screenRenderer->transform;
            auto height = t->localScale.y;
            t->localScale = Vector3(height * texture->width / texture->height, height, 1);
            PaperLogger.info("Video prepared: {}x{}, {:.1f}s, {:.2f} fps, can set time: {}", texture->width, texture->height, player->length,
                             player->frameRate, player->canSetTime);
            _placementApplied = true;
        }
    }


    if (!audioSource->isPlaying) {
        if (player->isPlaying) {
            player->Pause();
        }
        return;
    }

    auto length = player->length;
    auto referenceTime = GetReferenceTime();
    if (_config->loop && length > 0) {
        referenceTime = std::fmod(referenceTime, length);
    }

    bool beforeStart = referenceTime < 0;
    bool pastEnd = (_config->endVideoAt && referenceTime >= *_config->endVideoAt) || (!_config->loop && length > 0 && referenceTime >= length);
    if (beforeStart || pastEnd) {
        SetScreenVisible(false);
        if (player->isPlaying) {
            player->Pause();
        }
        return;
    }


    if (!player->isPlaying) {
        PaperLogger.info("Starting video, seeking to {:.3f}s once playing (song at {:.3f}s)", referenceTime, audioSource->time);
        player->Play();
        _seekPending = true;
        return;
    }

    if (_seekPending || _forceResync) {
        _seekPending = false;
        _forceResync = false;
        Resync(referenceTime);
        _lastAudioTime = audioSource->time;
        return;
    }

    auto displayedTime = GetDisplayedTime();
    auto error = referenceTime - displayedTime;

    if (player->frame > 0) {
        SetScreenVisible(std::abs(error) < MAX_VISIBLE_DESYNC);
    }

    auto maxDesync = _mode == PlayerMode::Preview ? PREVIEW_MAX_DESYNC : MAX_DESYNC;
    auto seekCooldown = _mode == PlayerMode::Preview ? PREVIEW_SEEK_COOLDOWN : SEEK_COOLDOWN;
    auto now = Time::get_realtimeSinceStartup();
    auto audioTime = audioSource->time;
    bool audioSeeked = std::abs(audioTime - _lastAudioTime) > MAX_DESYNC;
    _lastAudioTime = audioTime;

    if (std::abs(error) <= maxDesync) {
        _failedResyncs = 0;
    }
    if ((audioSeeked || std::abs(error) > maxDesync) && now - _lastSeekRealtime > seekCooldown) {
        if (!audioSeeked) {
            _failedResyncs++;
        }
        PaperLogger.info("Resyncing video (error {:.0f} ms, showing {:.3f}s, want {:.3f}s{})", error * 1000, displayedTime, referenceTime,
                         audioSeeked ? ", audio seeked" : "");
        Resync(referenceTime);
    }
}


