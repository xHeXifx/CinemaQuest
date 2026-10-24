#include "UploadServer.hpp"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>
#include <unordered_map>

#include "VideoConfig.hpp"
#include "beatsaber-hook/shared/config/rapidjson-utils.hpp"
#include "beatsaber-hook/shared/rapidjson/include/rapidjson/stringbuffer.h"
#include "beatsaber-hook/shared/rapidjson/include/rapidjson/writer.h"
#include "main.hpp"


extern "C" unsigned char const cinema_upload_page[];
extern "C" size_t const cinema_upload_page_size;

namespace fs = std::filesystem;

namespace Cinema::UploadServer {

static constexpr size_t MAX_HEADER_SIZE = 16 * 1024;
static constexpr size_t MAX_CONFIG_BODY_SIZE = 64 * 1024;
static constexpr size_t MAX_TITLE_LENGTH = 300;

static constexpr int MAX_OFFSET_MS = 10 * 60 * 1000;
static constexpr std::array<std::string_view, 4> VIDEO_EXTENSIONS = {"mp4", "webm", "mkv", "mov"};

static constexpr int POLL_TIMEOUT_MS = 500;
static constexpr int SOCKET_TIMEOUT_S = 30;

static std::mutex levelsMutex;
static std::vector<LevelInfo> levels;
static std::atomic<bool> running = false;

void SetLevels(std::vector<LevelInfo> newLevels) {
    std::lock_guard lock(levelsMutex);
    levels = std::move(newLevels);
}

static std::vector<LevelInfo> GetLevels() {
    std::lock_guard lock(levelsMutex);
    return levels;
}

struct Request {
    std::string method;
    std::string path;
    std::unordered_map<std::string, std::string> query;
    std::unordered_map<std::string, std::string> headers;

    std::string bodyStart;
};

static std::string UrlDecode(std::string_view s) {
    std::string result;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '%' && i + 2 < s.size()) {
            int value = 0;
            std::from_chars(s.data() + i + 1, s.data() + i + 3, value, 16);
            result += static_cast<char>(value);
            i += 2;
        } else if (s[i] == '+') {
            result += ' ';
        } else {
            result += s[i];
        }
    }
    return result;
}

static std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

static std::optional<Request> ReadRequest(int fd) {
    std::string buffer;
    char chunk[4096];
    size_t headerEnd;
    while ((headerEnd = buffer.find("\r\n\r\n")) == std::string::npos) {
        if (buffer.size() > MAX_HEADER_SIZE) {
            return std::nullopt;
        }
        auto n = recv(fd, chunk, sizeof(chunk), 0);
        if (n <= 0) {
            return std::nullopt;
        }
        buffer.append(chunk, n);
    }

    Request request;
    request.bodyStart = buffer.substr(headerEnd + 4);
    std::string_view head(buffer.data(), headerEnd);

    auto lineEnd = head.find("\r\n");
    auto requestLine = head.substr(0, lineEnd);
    auto methodEnd = requestLine.find(' ');
    auto targetEnd = requestLine.find(' ', methodEnd + 1);
    if (methodEnd == std::string_view::npos || targetEnd == std::string_view::npos) {
        return std::nullopt;
    }
    request.method = requestLine.substr(0, methodEnd);
    auto target = requestLine.substr(methodEnd + 1, targetEnd - methodEnd - 1);

    auto queryStart = target.find('?');
    request.path = UrlDecode(target.substr(0, queryStart));
    if (queryStart != std::string_view::npos) {
        auto query = target.substr(queryStart + 1);
        while (!query.empty()) {
            auto pairEnd = query.find('&');
            auto pair = query.substr(0, pairEnd);
            auto eq = pair.find('=');
            if (eq != std::string_view::npos) {
                request.query[UrlDecode(pair.substr(0, eq))] = UrlDecode(pair.substr(eq + 1));
            }
            query = pairEnd == std::string_view::npos ? "" : query.substr(pairEnd + 1);
        }
    }

    while (lineEnd != std::string_view::npos) {
        auto start = lineEnd + 2;
        lineEnd = head.find("\r\n", start);
        auto line = head.substr(start, lineEnd == std::string_view::npos ? std::string_view::npos : lineEnd - start);
        auto colon = line.find(':');
        if (colon == std::string_view::npos) {
            continue;
        }
        auto value = line.substr(colon + 1);
        while (!value.empty() && value.front() == ' ') {
            value.remove_prefix(1);
        }
        request.headers[ToLower(std::string(line.substr(0, colon)))] = value;
    }
    return request;
}

static bool SendAll(int fd, std::string_view data) {
    while (!data.empty()) {
        auto n = send(fd, data.data(), data.size(), MSG_NOSIGNAL);
        if (n <= 0) {
            return false;
        }
        data.remove_prefix(n);
    }
    return true;
}

static void SendResponse(int fd, int status, std::string_view statusText, std::string_view contentType, std::string_view body) {
    auto header = fmt::format(
        "HTTP/1.1 {} {}\r\nContent-Type: {}\r\nContent-Length: {}\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n", status,
        statusText, contentType, body.size());
    if (SendAll(fd, header)) {
        SendAll(fd, body);
    }
}

static void SendError(int fd, int status, std::string_view statusText, std::string_view message) {
    SendResponse(fd, status, statusText, "text/plain; charset=utf-8", message);
}

static std::string MapsJson() {
    rapidjson::StringBuffer buffer;
    rapidjson::Writer writer(buffer);
    writer.StartArray();
    auto allLevels = GetLevels();
    for (size_t i = 0; i < allLevels.size(); i++) {
        auto const& level = allLevels[i];
        auto config = VideoConfig::LoadForLevel(level.path, level.levelID);
        writer.StartObject();
        writer.Key("index");
        writer.Uint64(i);
        writer.Key("songName");
        writer.String(level.songName.c_str());
        writer.Key("songAuthor");
        writer.String(level.songAuthor.c_str());
        writer.Key("mapper");
        writer.String(level.mapper.c_str());
        writer.Key("hasConfig");
        writer.Bool(config.has_value());
        if (!config) {
            writer.EndObject();
            continue;
        }
        writer.Key("author");
        writer.String(config->author.value_or("").c_str());
        writer.Key("offset");
        writer.Int(config->offset);
        writer.Key("title");
        writer.String(config->title.value_or("").c_str());
        writer.Key("videoID");
        writer.String(config->videoID.value_or("").c_str());
        writer.Key("duration");
        writer.Int(config->duration);
        writer.Key("hasVideo");
        writer.Bool(config->IsPlayable());
        writer.Key("bundled");
        writer.Bool(config->bundledConfig);
        writer.EndObject();
    }
    writer.EndArray();
    return buffer.GetString();
}

static std::optional<LevelInfo> FindLevel(std::string_view indexText) {
    size_t index = 0;
    auto [_, ec] = std::from_chars(indexText.data(), indexText.data() + indexText.size(), index);
    auto allLevels = GetLevels();
    if (ec != std::errc() || index >= allLevels.size()) {
        return std::nullopt;
    }
    return allLevels[index];
}

static std::optional<size_t> GetContentLength(Request const& request) {
    auto header = request.headers.find("content-length");
    size_t length = 0;
    if (header == request.headers.end() ||
        std::from_chars(header->second.data(), header->second.data() + header->second.size(), length).ec != std::errc()) {
        return std::nullopt;
    }
    return length;
}

static bool IsValidVideoID(std::string_view id) {
    return id.size() == 11 && std::all_of(id.begin(), id.end(), [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_'; });
}


static void RetireVideos(fs::path const& levelDir) {
    std::error_code ec;
    for (auto const& entry : fs::directory_iterator(levelDir, ec)) {
        auto ext = ToLower(entry.path().extension().string());
        if (!ext.empty() && std::find(VIDEO_EXTENSIONS.begin(), VIDEO_EXTENSIONS.end(), ext.substr(1)) != VIDEO_EXTENSIONS.end()) {
            std::error_code renameError;
            fs::rename(entry.path(), fs::path(entry.path().string() + ".old"), renameError);
            PaperLogger.info("Renamed old video {} to .old", entry.path().filename().string());
        }
    }
}

static void HandleConfig(int fd, Request const& request, std::string_view indexText) {
    auto level = FindLevel(indexText);
    if (!level) {
        return SendError(fd, 404, "Not Found", "Unknown map");
    }
    auto contentLength = GetContentLength(request);
    if (!contentLength || *contentLength > MAX_CONFIG_BODY_SIZE) {
        return SendError(fd, 400, "Bad Request", "Missing or too large body");
    }

    std::string body = request.bodyStart.substr(0, *contentLength);
    char chunk[4096];
    while (body.size() < *contentLength) {
        auto n = recv(fd, chunk, std::min(sizeof(chunk), *contentLength - body.size()), 0);
        if (n <= 0) {
            return;
        }
        body.append(chunk, n);
    }

    rapidjson::Document doc;
    doc.Parse(body.c_str());
    if (doc.HasParseError() || !doc.IsObject() || !doc.HasMember("videoID") || !doc["videoID"].IsString()) {
        return SendError(fd, 400, "Bad Request", "Invalid request");
    }

    VideoConfig::UserConfigFields fields;
    fields.videoID = doc["videoID"].GetString();
    if (!IsValidVideoID(fields.videoID)) {
        return SendError(fd, 400, "Bad Request", "That doesn't look like a YouTube video ID");
    }
    if (doc.HasMember("title") && doc["title"].IsString()) {
        fields.title = doc["title"].GetString();
    }
    if (doc.HasMember("author") && doc["author"].IsString()) {
        fields.author = doc["author"].GetString();
    }
    if (fields.title.size() > MAX_TITLE_LENGTH || fields.author.size() > MAX_TITLE_LENGTH) {
        return SendError(fd, 400, "Bad Request", "Title is too long");
    }
    if (doc.HasMember("offset") && doc["offset"].IsNumber()) {
        fields.offset = static_cast<int>(doc["offset"].GetDouble());
    }
    if (std::abs(fields.offset) > MAX_OFFSET_MS) {
        return SendError(fd, 400, "Bad Request", "Offset must be within 10 minutes");
    }

    auto previous = VideoConfig::LoadForLevel(level->path, level->levelID);
    fields.videoChanged = previous && previous->videoID != fields.videoID;
    if (fields.videoChanged) {
        RetireVideos(level->path);
    }

    if (!VideoConfig::SaveUserConfig(level->path, level->levelID, fields)) {
        return SendError(fd, 500, "Internal Server Error", "Failed to save the config");
    }
    PaperLogger.info("Saved config for {} (video {}, offset {} ms)", level->songName, fields.videoID, fields.offset);
    SendResponse(fd, 200, "OK", "application/json", "{\"ok\":true}");
}

static void HandleUpload(int fd, Request const& request, std::string_view indexText) {
    auto foundLevel = FindLevel(indexText);
    if (!foundLevel) {
        return SendError(fd, 404, "Not Found", "Unknown map");
    }
    auto const& level = *foundLevel;

    auto ext = ToLower(request.query.contains("ext") ? request.query.at("ext") : "");
    if (std::find(VIDEO_EXTENSIONS.begin(), VIDEO_EXTENSIONS.end(), ext) == VIDEO_EXTENSIONS.end()) {
        return SendError(fd, 400, "Bad Request", "Only .mp4, .webm, .mkv and .mov videos can be uploaded");
    }

    auto config = VideoConfig::LoadForLevel(level.path, level.levelID);
    if (!config) {
        return SendError(fd, 404, "Not Found", "This map has no Cinema config");
    }

    auto length = GetContentLength(request);
    if (!length) {
        return SendError(fd, 411, "Length Required", "Missing Content-Length");
    }
    auto contentLength = *length;


    auto target = fs::path(level.path) / (fs::path(config->VideoFileName()).stem().string() + "." + ext);
    auto partial = fs::path(target.string() + ".part");
    PaperLogger.info("Receiving {} bytes for {}", contentLength, target.string());

    {
        std::ofstream out(partial, std::ios::binary | std::ios::trunc);
        auto received = std::min(contentLength, request.bodyStart.size());
        out.write(request.bodyStart.data(), received);

        std::vector<char> chunk(256 * 1024);
        while (received < contentLength && out) {
            auto n = recv(fd, chunk.data(), std::min(chunk.size(), contentLength - received), 0);
            if (n <= 0) {
                break;
            }
            out.write(chunk.data(), n);
            received += n;
        }

        if (received < contentLength || !out) {
            out.close();
            std::error_code removeError;
            fs::remove(partial, removeError);
            PaperLogger.warn("Upload for {} was interrupted after {} of {} bytes", level.songName, received, contentLength);
            return SendError(fd, 500, "Internal Server Error", "Upload interrupted");
        }
    }

    std::error_code renameError;
    fs::rename(partial, target, renameError);
    if (renameError) {
        PaperLogger.error("Failed to save {}: {}", target.string(), renameError.message());
        return SendError(fd, 500, "Internal Server Error", "Failed to save the video");
    }


    if (config->bundledConfig) {
        VideoConfig::SaveBundledConfig(level.path, level.levelID);
    }

    PaperLogger.info("Saved uploaded video for {} as {}", level.songName, target.filename().string());
    SendResponse(fd, 200, "OK", "application/json", "{\"ok\":true}");
}

static void HandleConnection(int fd) {
    timeval timeout{.tv_sec = SOCKET_TIMEOUT_S, .tv_usec = 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    auto request = ReadRequest(fd);
    if (!request) {
        return;
    }

    static constexpr std::string_view UPLOAD_PREFIX = "/api/upload/";
    static constexpr std::string_view CONFIG_PREFIX = "/api/config/";
    if (request->method == "GET" && request->path == "/") {
        SendResponse(fd, 200, "OK", "text/html; charset=utf-8",
                     std::string_view(reinterpret_cast<char const*>(cinema_upload_page), cinema_upload_page_size));
    } else if (request->method == "GET" && request->path == "/api/maps") {
        SendResponse(fd, 200, "OK", "application/json", MapsJson());
    } else if (request->method == "PUT" && request->path.starts_with(UPLOAD_PREFIX)) {
        HandleUpload(fd, *request, std::string_view(request->path).substr(UPLOAD_PREFIX.size()));
    } else if (request->method == "POST" && request->path.starts_with(CONFIG_PREFIX)) {
        HandleConfig(fd, *request, std::string_view(request->path).substr(CONFIG_PREFIX.size()));
    } else {
        SendError(fd, 404, "Not Found", "Not found");
    }
}


static std::atomic<int> generation = 0;

static void ServerLoop(int sock, int myGeneration) {
    PaperLogger.info("Upload server listening on port {}", PORT);
    while (generation == myGeneration) {
        pollfd pfd{.fd = sock, .events = POLLIN, .revents = 0};
        if (poll(&pfd, 1, POLL_TIMEOUT_MS) <= 0 || !(pfd.revents & POLLIN) || generation != myGeneration) {
            continue;
        }
        auto client = accept(sock, nullptr, nullptr);
        if (client < 0) {
            continue;
        }
        try {
            HandleConnection(client);
        } catch (std::exception const& e) {
            PaperLogger.error("Upload server request failed: {}", e.what());
        }
        close(client);
    }
    close(sock);
    PaperLogger.info("Upload server stopped");
}

void Start() {
    if (running.exchange(true)) {
        return;
    }
    VideoConfig::PreloadBundledConfigs();

    auto sock = socket(AF_INET, SOCK_STREAM, 0);
    int reuse = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    sockaddr_in address{.sin_family = AF_INET, .sin_port = htons(PORT), .sin_addr = {.s_addr = htonl(INADDR_ANY)}, .sin_zero = {}};
    if (sock < 0 || bind(sock, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 || listen(sock, 4) != 0) {
        PaperLogger.error("Failed to start upload server on port {}: {}", PORT, strerror(errno));
        if (sock >= 0) {
            close(sock);
        }
        running = false;
        return;
    }
    std::thread(ServerLoop, sock, ++generation).detach();
}

void Stop() {
    if (running.exchange(false)) {

        generation++;
    }
}

bool IsRunning() {
    return running;
}

std::optional<std::string> GetUrl() {
    ifaddrs* interfaces = nullptr;
    if (getifaddrs(&interfaces) != 0) {
        return std::nullopt;
    }
    std::optional<std::string> address;
    for (auto ifa = interfaces; ifa; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) {
            continue;
        }
        char text[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &reinterpret_cast<sockaddr_in*>(ifa->ifa_addr)->sin_addr, text, sizeof(text));
        if (std::string_view(text).starts_with("127.")) {
            continue;
        }
        address = text;

        if (std::string_view(ifa->ifa_name).starts_with("wlan")) {
            break;
        }
    }
    freeifaddrs(interfaces);
    if (!address) {
        return std::nullopt;
    }
    return fmt::format("http://{}:{}", *address, PORT);
}

}
