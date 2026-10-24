#include "OffsetTuner.hpp"

#include <algorithm>

#include "GlobalNamespace/SongPreviewPlayer.hpp"
#include "UnityEngine/AudioSource.hpp"
#include "UnityEngine/GameObject.hpp"
#include "UnityEngine/Resources.hpp"
#include "UnityEngine/Time.hpp"
#include "UnityEngine/Transform.hpp"
#include "UnityEngine/TextAnchor.hpp"
#include "UnityEngine/UI/HorizontalLayoutGroup.hpp"
#include "UnityEngine/UI/LayoutElement.hpp"
#include "UnityEngine/UI/VerticalLayoutGroup.hpp"
#include "TMPro/TextAlignmentOptions.hpp"
#include "bsml/shared/BSML.hpp"
#include "bsml/shared/BSML-Lite.hpp"
#include "main.hpp"

DEFINE_TYPE(Cinema, OffsetTuner);

using namespace UnityEngine;

namespace Cinema {

static constexpr std::array OFFSET_STEPS = {-1000, -100, -10, 10, 100, 1000};

static constexpr float PREVIEW_VOLUME = -4.0f;

static constexpr float MENU_SEARCH_INTERVAL = 0.5f;
static constexpr float BUTTON_HEIGHT = 7.0f;

static constexpr float MAX_SNIPPET_SOURCE_ERROR = 1.5f;

static StringW FormatTime(float seconds) {
    auto total = static_cast<int>(seconds);
    return fmt::format("{}:{:02}", total / 60, total % 60);
}


static UI::Button* Compact(UI::Button* button, float width, float fontSize) {
    BSML::Lite::SetButtonTextSize(button, fontSize);
    auto element = button->GetComponent<UI::LayoutElement*>();
    if (!element) {
        element = button->gameObject->AddComponent<UI::LayoutElement*>();
    }
    element->preferredWidth = width;
    element->minWidth = width;
    element->preferredHeight = BUTTON_HEIGHT;
    return button;
}

static UI::HorizontalLayoutGroup* CreateRow(Transform* parent) {
    auto row = BSML::Lite::CreateHorizontalLayoutGroup(parent);
    row->spacing = 0.5f;
    row->childForceExpandWidth = false;
    row->childAlignment = TextAnchor::MiddleCenter;
    return row;
}

void OffsetTuner::BuildUI() {
    auto layout = BSML::Lite::CreateVerticalLayoutGroup(transform);
    layout->spacing = 0;
    layout->childForceExpandHeight = false;
    auto root = layout->transform;


    statusText = BSML::Lite::CreateText(root, "", TMPro::FontStyles::Normal, 3.2f);
    statusText->alignment = TMPro::TextAlignmentOptions::Center;

    auto offsetRow = CreateRow(root->transform);
    for (auto step : OFFSET_STEPS) {
        Compact(BSML::Lite::CreateUIButton(offsetRow->transform, fmt::format("{:+}", step), [this, step]() { ChangeOffset(step); }), 11, 2.8f);
    }

    startSlider = BSML::Lite::CreateSliderSetting(root, "Snippet start", 1.0f, 0.0f, 0.0f, 600.0f, [this](float value) { _snippetStart = value; });
    startSlider->formatter = FormatTime;
    auto lengthSlider = BSML::Lite::CreateSliderSetting(root, "Snippet length (s)", 1.0f, _snippetLength, 2.0f, 10.0f, [this](float value) { _snippetLength = value; });
    lengthSlider->isInt = true;

    auto actionRow = CreateRow(root->transform);
    playButton = Compact(BSML::Lite::CreateUIButton(actionRow->transform, "Play", "PlayButton", [this]() {
        if (_playing) {
            Stop(true);
        } else {
            Play();
        }
    }), 15, 3.2f);
    Compact(BSML::Lite::CreateUIButton(actionRow->transform, "Save", [this]() { Save(); }), 13, 3.2f);
    Compact(BSML::Lite::CreateUIButton(actionRow->transform, "Revert", [this]() { Revert(); }), 14, 3.2f);

    UpdateTexts();
}

void OffsetTuner::FindMenuObjects() {
    auto now = Time::get_realtimeSinceStartup();
    if (now < _nextMenuSearch) {
        return;
    }
    _nextMenuSearch = now + MENU_SEARCH_INTERVAL;

    if (!levelDetail || !levelDetail->isActivated) {
        levelDetail = nullptr;
        for (auto detail : Resources::FindObjectsOfTypeAll<GlobalNamespace::StandardLevelDetailViewController*>()) {
            if (detail->isActivated) {
                levelDetail = detail;
                break;
            }
        }
    }
    if (!songPreviewPlayer) {
        auto players = Resources::FindObjectsOfTypeAll<GlobalNamespace::SongPreviewPlayer*>();
        if (players.size() > 0) {
            songPreviewPlayer = players[0];
        }
    }
}



AudioSource* OffsetTuner::FindSnippetSource() {
    auto expected = _snippetStartTime + (Time::get_realtimeSinceStartup() - _snippetStartRealtime);
    AudioSource* best = nullptr;
    float bestError = MAX_SNIPPET_SOURCE_ERROR;
    for (auto controller : songPreviewPlayer->_audioSourceControllers) {
        if (!controller) {
            continue;
        }
        auto source = controller->audioSource;
        if (!source || !source->isPlaying || source->clip.unsafePtr() != snippetClip.unsafePtr()) {
            continue;
        }
        auto error = std::abs(source->time - expected);
        if (error < bestError) {
            best = source.unsafePtr();
            bestError = error;
        }
    }
    return best;
}

void OffsetTuner::UpdateSnippet() {
    if (!songPreviewPlayer || !snippetClip) {
        return Stop(false);
    }

    auto source = FindSnippetSource();
    if (source != snippetAudioSource.unsafePtr()) {
        snippetAudioSource = source;
        if (source) {
            previewPlayer->SetAudioSource(source);
        }
    }

    auto elapsed = Time::get_realtimeSinceStartup() - _snippetStartRealtime;
    if (!source) {

        if (elapsed > MAX_SNIPPET_SOURCE_ERROR) {
            Stop(false);
        }
        return;
    }

    if (source->time >= _snippetEnd || elapsed > _snippetLength + MAX_SNIPPET_SOURCE_ERROR) {
        Stop(true);
    }
}

void OffsetTuner::Update() {

    try {
        FindMenuObjects();

        GlobalNamespace::BeatmapLevel* current = levelDetail ? levelDetail->beatmapLevel : nullptr;
        if (current != level) {
            level = current;
            OnLevelChanged();
        }

        if (_playing) {
            UpdateSnippet();
        }
    } catch (std::exception const& e) {
        PaperLogger.error("Offset tuner error: {}", e.what());
        _playing = false;
        if (previewPlayer) {
            previewPlayer->SetActive(false);
        }
        if (playButton) {
            BSML::Lite::SetButtonText(playButton.unsafePtr(), "Play");
        }
    }
}

void OffsetTuner::OnDisable() {
    if (_playing) {
        Stop(true);
    }
    DestroyPreviewPlayer();

    level = nullptr;
}

void OffsetTuner::OnLevelChanged() {
    if (_playing) {

        Stop(false);
    }
    DestroyPreviewPlayer();

    _config = level ? LoadConfigForLevel(level) : std::nullopt;
    _offset = _savedOffset = _config ? _config->offset : 0;

    if (level && startSlider) {
        auto slider = startSlider->slider;
        auto duration = std::max(1.0f, level->songDuration);
        slider->maxValue = duration;
        slider->numberOfSteps = static_cast<int>(duration / startSlider->increments) + 1;

        _snippetStart = std::clamp(level->previewStartTime, 0.0f, duration);
        startSlider->set_Value(_snippetStart);
    }
    UpdateTexts();
}

void OffsetTuner::Play() {
    if (!_config || !_config->IsPlayable() || !songPreviewPlayer) {
        return;
    }

    auto clip = songPreviewPlayer->activeAudioClip;
    if (!clip || clip.unsafePtr() == songPreviewPlayer->defaultAudioClip.unsafePtr()) {
        statusText->text = "Wait for the song preview to start, then press Play";
        return;
    }
    snippetClip = clip;

    if (!previewPlayer) {
        auto go = GameObject::New_ctor("CinemaOffsetPreview");
        previewPlayer = go->AddComponent<CinemaPlayer*>();
        previewPlayer->Init(nullptr, *_config, 1.0f, PlayerMode::Preview);
    }
    StartSnippet();
}

void OffsetTuner::StartSnippet() {
    auto start = std::clamp(_snippetStart, 0.0f, std::max(0.0f, snippetClip->length - _snippetLength));
    _snippetStartTime = start;
    _snippetEnd = start + _snippetLength;
    _snippetStartRealtime = Time::get_realtimeSinceStartup();

    songPreviewPlayer->CrossfadeTo(snippetClip.unsafePtr(), PREVIEW_VOLUME, start, _snippetLength + 1.0f, nullptr);


    snippetAudioSource = FindSnippetSource();
    previewPlayer->SetOffset(_offset);
    if (snippetAudioSource) {
        previewPlayer->SetAudioSource(snippetAudioSource.unsafePtr());
    }
    previewPlayer->SetActive(true);
    _playing = true;
    BSML::Lite::SetButtonText(playButton, "Stop");
}

void OffsetTuner::Stop(bool returnToPreview) {
    _playing = false;
    if (previewPlayer) {
        previewPlayer->SetActive(false);
    }
    if (playButton) {
        BSML::Lite::SetButtonText(playButton, "Play");
    }
    if (returnToPreview && level && songPreviewPlayer && snippetClip) {
        songPreviewPlayer->CrossfadeTo(snippetClip.unsafePtr(), PREVIEW_VOLUME, level->previewStartTime, level->previewDuration, nullptr);
    }
    snippetAudioSource = nullptr;
}

void OffsetTuner::DestroyPreviewPlayer() {
    if (previewPlayer) {
        Object::Destroy(previewPlayer->gameObject);
    }
    previewPlayer = nullptr;
}

void OffsetTuner::ChangeOffset(int delta) {
    if (!_config) {
        return;
    }
    _offset += delta;
    if (previewPlayer) {
        previewPlayer->SetOffset(_offset);
    }
    UpdateTexts();
}

void OffsetTuner::Save() {
    if (!_config || !level) {
        return;
    }
    if (VideoConfig::SaveOffset(_config->levelDir, std::string(level->levelID), _offset)) {
        _savedOffset = _config->offset = _offset;
        PaperLogger.info("Saved offset {} ms for {}", _offset, std::string(level->songName));
    } else {
        PaperLogger.error("Failed to save offset for {}", std::string(level->songName));
    }
    UpdateTexts();
}

void OffsetTuner::Revert() {
    _offset = _savedOffset;
    if (previewPlayer) {
        previewPlayer->SetOffset(_offset);
    }
    UpdateTexts();
}

void OffsetTuner::UpdateTexts() {
    if (!statusText) {
        return;
    }
    if (!level) {
        statusText->text = "Select a song";
        return;
    }
    if (!_config) {
        statusText->text = "This song has no Cinema config";
        return;
    }
    if (!_config->IsPlayable()) {
        statusText->text = "This song's video isn't on the Quest yet";
        return;
    }
    auto offset = _offset == _savedOffset ? fmt::format("Offset: {} ms", _offset)
                                          : fmt::format("Offset: {} ms <color=#ffcc66>(unsaved, was {})</color>", _offset, _savedOffset);
    statusText->text = fmt::format("<b>{}</b>\n{}", std::string(level->songName), offset);
}

static void TabDidActivate(GameObject* go, bool firstActivation) {
    if (firstActivation) {
        go->AddComponent<OffsetTuner*>()->BuildUI();
    }
}

void RegisterOffsetTunerTab() {

    auto menuType = static_cast<BSML::MenuType>(static_cast<int>(BSML::MenuType::Solo) | static_cast<int>(BSML::MenuType::Custom));
    BSML::Register::RegisterGameplaySetupTab("Cinema", TabDidActivate, menuType);
}

}
