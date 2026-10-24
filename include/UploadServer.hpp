#pragma once

#include <optional>
#include <string>
#include <vector>

namespace Cinema::UploadServer {

static constexpr int PORT = 2468;

struct LevelInfo {
    std::string levelID;
    std::string songName;
    std::string songAuthor;
    std::string mapper;
    std::string path;
};

void SetLevels(std::vector<LevelInfo> levels);

void Start();
void Stop();
bool IsRunning();

std::optional<std::string> GetUrl();

}
