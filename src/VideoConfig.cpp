#include "VideoConfig.hpp"

#include <fstream>
#include <sstream>

#include "beatsaber-hook/shared/config/rapidjson-utils.hpp"
#include "beatsaber-hook/shared/rapidjson/include/rapidjson/prettywriter.h"
#include "beatsaber-hook/shared/rapidjson/include/rapidjson/stringbuffer.h"
#include "main.hpp"
#include "songcore/shared/SongLoader/CustomBeatmapLevel.hpp"

namespace fs = std::filesystem;

namespace Cinema {

static constexpr auto CONFIG_FILENAME = "cinema-video.json";


static std::string ReplaceIllegalFilesystemChars(std::string s) {
    for (auto& c : s) {
        if (static_cast<unsigned char>(c) < 32 || std::string_view("<>:\"/\\|?*.").find(c) != std::string_view::npos) {
            c = '_';
        }
    }
    return s;
}

static std::string WithMp4Extension(std::string name) {
    if (!name.ends_with(".mp4")) {
        name += ".mp4";
    }
    return name;
}

static std::optional<std::string> GetString(rapidjson::Value const& obj, char const* key) {
    auto itr = obj.FindMember(key);
    if (itr == obj.MemberEnd() || !itr->value.IsString()) {
        return std::nullopt;
    }
    return std::string(itr->value.GetString(), itr->value.GetStringLength());
}

static std::optional<float> GetFloat(rapidjson::Value const& obj, char const* key) {
    auto itr = obj.FindMember(key);
    if (itr == obj.MemberEnd() || !itr->value.IsNumber()) {
        return std::nullopt;
    }
    return itr->value.GetFloat();
}

static std::optional<UnityEngine::Vector3> GetVector3(rapidjson::Value const& obj, char const* key) {
    auto itr = obj.FindMember(key);
    if (itr == obj.MemberEnd() || !itr->value.IsObject()) {
        return std::nullopt;
    }
    auto const& v = itr->value;
    return UnityEngine::Vector3(GetFloat(v, "x").value_or(0), GetFloat(v, "y").value_or(0), GetFloat(v, "z").value_or(0));
}

static std::optional<bool> GetBool(rapidjson::Value const& obj, char const* key) {
    auto itr = obj.FindMember(key);
    if (itr == obj.MemberEnd() || !itr->value.IsBool()) {
        return std::nullopt;
    }
    return itr->value.GetBool();
}

static std::vector<EnvironmentModification> GetEnvironmentModifications(rapidjson::Value const& obj) {
    std::vector<EnvironmentModification> modifications;
    auto itr = obj.FindMember("environment");
    if (itr == obj.MemberEnd() || !itr->value.IsArray()) {
        return modifications;
    }
    for (auto const& entry : itr->value.GetArray()) {
        if (!entry.IsObject()) {
            continue;
        }
        auto name = GetString(entry, "name");

        if (!name) {
            continue;
        }
        modifications.push_back({
            .name = *name,
            .parentName = GetString(entry, "parentName"),
            .cloneFrom = GetString(entry, "cloneFrom"),
            .active = GetBool(entry, "active"),
            .position = GetVector3(entry, "position"),
            .rotation = GetVector3(entry, "rotation"),
            .scale = GetVector3(entry, "scale"),
        });
    }
    return modifications;
}

static std::optional<fs::path> ResolveVideoPath(VideoConfig const& config) {
    std::vector<std::string> candidates = {config.VideoFileName()};

    if (config.title) {
        candidates.push_back(WithMp4Extension(*config.title));
    }

    std::error_code ec;
    for (auto const& name : candidates) {
        auto path = config.levelDir / name;
        if (fs::is_regular_file(path, ec)) {
            return path;
        }
    }


    for (auto const& entry : fs::directory_iterator(config.levelDir, ec)) {
        auto ext = entry.path().extension().string();
        if (entry.is_regular_file(ec) && (ext == ".mp4" || ext == ".webm" || ext == ".mkv" || ext == ".mov")) {
            PaperLogger.info("Using fallback video file {}", entry.path().filename().string());
            return entry.path();
        }
    }

    return std::nullopt;
}

std::string VideoConfig::VideoFileName() const {
    if (videoFile) {
        return WithMp4Extension(*videoFile);
    }
    return WithMp4Extension(ReplaceIllegalFilesystemChars(title.value_or(videoID.value_or("video"))));
}

static VideoConfig FromJson(rapidjson::Value const& doc, fs::path const& levelDir) {
    VideoConfig config;
    config.levelDir = levelDir;
    config.videoID = GetString(doc, "videoID");
    config.title = GetString(doc, "title");
    config.author = GetString(doc, "author");
    config.videoFile = GetString(doc, "videoFile");
    config.duration = static_cast<int>(GetFloat(doc, "duration").value_or(0));
    config.offset = static_cast<int>(GetFloat(doc, "offset").value_or(0));
    config.playbackSpeed = GetFloat(doc, "playbackSpeed").value_or(1.0f);
    config.loop = GetBool(doc, "loop").value_or(false);
    config.endVideoAt = GetFloat(doc, "endVideoAt");
    config.screenPosition = GetVector3(doc, "screenPosition");
    config.screenRotation = GetVector3(doc, "screenRotation");
    config.screenHeight = GetFloat(doc, "screenHeight");
    config.environmentName = GetString(doc, "environmentName");
    config.disableDefaultModifications = GetBool(doc, "disableDefaultModifications").value_or(false);
    config.environment = GetEnvironmentModifications(doc);
    config.videoPath = ResolveVideoPath(config);
    return config;
}


static bool ReadConfigDocument(fs::path const& levelDir, rapidjson::Document& doc) {
    auto configPath = levelDir / CONFIG_FILENAME;
    std::error_code ec;
    if (!fs::is_regular_file(configPath, ec)) {
        return false;
    }

    std::ifstream file(configPath);
    std::stringstream buffer;
    buffer << file.rdbuf();
    auto json = buffer.str();

    if (json.starts_with("\xEF\xBB\xBF")) {
        json.erase(0, 3);
    }

    doc.Parse(json.c_str());
    if (doc.HasParseError() || !doc.IsObject()) {
        PaperLogger.error("Failed to parse {}", configPath.string());
        return false;
    }
    return true;
}

static std::optional<VideoConfig> LoadFromFile(fs::path const& levelDir) {
    rapidjson::Document doc;
    if (!ReadConfigDocument(levelDir, doc)) {
        return std::nullopt;
    }
    return FromJson(doc, levelDir);
}

static bool WriteConfigDocument(fs::path const& levelDir, rapidjson::Document const& doc) {
    rapidjson::StringBuffer buffer;
    rapidjson::PrettyWriter writer(buffer);
    doc.Accept(writer);

    std::ofstream file(levelDir / CONFIG_FILENAME);
    file << buffer.GetString();
    return file.good();
}


extern "C" unsigned char const cinema_bundled_configs[];
extern "C" size_t const cinema_bundled_configs_size;

static rapidjson::Value const* FindBundledConfig(std::string_view levelID) {
    static rapidjson::Document doc;
    static std::unordered_map<std::string, rapidjson::Value const*> configs;
    static bool loaded = false;
    if (!loaded) {
        loaded = true;
        doc.Parse(reinterpret_cast<char const*>(cinema_bundled_configs), cinema_bundled_configs_size);
        if (doc.HasParseError() || !doc.IsArray()) {
            PaperLogger.error("Failed to parse bundled configs");
            return nullptr;
        }
        for (auto const& entry : doc.GetArray()) {
            auto id = GetString(entry, "levelID");
            auto config = entry.FindMember("config");
            if (id && config != entry.MemberEnd() && config->value.IsObject()) {
                configs.emplace(*id, &config->value);
            }
        }
        PaperLogger.info("Loaded {} bundled configs", configs.size());
    }
    auto itr = configs.find(std::string(levelID));
    return itr != configs.end() ? itr->second : nullptr;
}

void VideoConfig::PreloadBundledConfigs() {
    FindBundledConfig("");
}

bool VideoConfig::SaveBundledConfig(fs::path const& levelDir, std::string_view levelID) {
    auto bundled = FindBundledConfig(levelID);
    if (!bundled) {
        return false;
    }

    rapidjson::Document doc;
    doc.CopyFrom(*bundled, doc.GetAllocator());
    doc.RemoveMember("bundledConfig");
    doc.AddMember("bundledConfig", true, doc.GetAllocator());
    return WriteConfigDocument(levelDir, doc);
}


static void ReadConfigOrBundled(fs::path const& levelDir, std::string_view levelID, rapidjson::Document& doc) {
    if (ReadConfigDocument(levelDir, doc)) {
        return;
    }
    if (auto bundled = FindBundledConfig(levelID)) {
        doc.CopyFrom(*bundled, doc.GetAllocator());
        doc.AddMember("bundledConfig", true, doc.GetAllocator());
    } else {
        doc.SetObject();
    }
}

bool VideoConfig::SaveOffset(fs::path const& levelDir, std::string_view levelID, int offset) {
    rapidjson::Document doc;
    ReadConfigOrBundled(levelDir, levelID, doc);
    doc.RemoveMember("offset");
    doc.AddMember("offset", offset, doc.GetAllocator());
    return WriteConfigDocument(levelDir, doc);
}

bool VideoConfig::SaveUserConfig(fs::path const& levelDir, std::string_view levelID, UserConfigFields const& fields) {

    rapidjson::Document doc;
    ReadConfigOrBundled(levelDir, levelID, doc);

    auto& allocator = doc.GetAllocator();
    auto set = [&](char const* key, rapidjson::Value value) {
        doc.RemoveMember(key);
        doc.AddMember(rapidjson::Value(key, allocator), value, allocator);
    };
    set("videoID", rapidjson::Value(fields.videoID.c_str(), allocator));
    if (fields.title.empty()) {
        doc.RemoveMember("title");
    } else {
        set("title", rapidjson::Value(fields.title.c_str(), allocator));
    }
    if (!fields.author.empty()) {
        set("author", rapidjson::Value(fields.author.c_str(), allocator));
    }
    set("offset", rapidjson::Value(fields.offset));

    if (doc.HasMember("videoFile") && doc["videoFile"].IsString() && fields.videoChanged) {
        doc.RemoveMember("videoFile");
    }
    return WriteConfigDocument(levelDir, doc);
}

std::optional<VideoConfig> VideoConfig::LoadForLevel(fs::path const& levelDir, std::string_view levelID) {
    auto config = LoadFromFile(levelDir);
    if (!config) {
        auto bundled = FindBundledConfig(levelID);
        if (!bundled) {
            return std::nullopt;
        }
        config = FromJson(*bundled, levelDir);
        config->bundledConfig = true;
    }

    PaperLogger.debug("Loaded {}config for {} (offset {} ms), video: {}", config->bundledConfig ? "bundled " : "",
                     config->title.value_or("<untitled>"), config->offset, config->videoPath ? config->videoPath->string() : "<not found>");
    return config;
}

std::optional<VideoConfig> LoadConfigForLevel(GlobalNamespace::BeatmapLevel* level) {
    auto customLevel = il2cpp_utils::try_cast<SongCore::SongLoader::CustomBeatmapLevel>(level);
    if (!customLevel) {
        return std::nullopt;
    }
    return VideoConfig::LoadForLevel(std::string(customLevel.value()->customLevelPath), std::string(level->levelID));
}

}
