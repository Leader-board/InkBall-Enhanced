#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace inkball {
constexpr int BoardColumns = 17;
constexpr int BoardRows = 17;
constexpr int TileSize = 32;
constexpr int BoardPixels = BoardColumns * TileSize;
constexpr int HudHeight = 42;
constexpr int BoardCellCount = BoardColumns * BoardRows;

struct LevelTile {
    std::uint16_t code = 0;
    std::uint16_t underlay = 0;
};
struct Level {
    int id = 0;
    int difficulty = 0;
    bool bonus = false;
    int timeSeconds = 120; // Zero means unlimited in the original resource.
    int spawnIntervalSeconds = 4;
    int minBallSpeed = 50;
    int maxBallSpeed = 150;
    bool randomizeQueue = false;
    std::vector<int> queue;
    std::array<LevelTile, BoardCellCount> tiles{};
    std::uint32_t resourceOffset = 0;
    std::uint16_t recordLength = 0;
    std::vector<std::uint8_t> unusedTrailingBytes;
};
struct DifficultyCollection {
    const char* name;
    int firstId;
    int normalCount;
    int bonusCount;
};
extern const std::array<DifficultyCollection, 5> DifficultyCollections;
std::vector<Level> LoadLevels(const std::vector<std::uint8_t>& resource);
const char* DifficultyName(int difficulty);
}
