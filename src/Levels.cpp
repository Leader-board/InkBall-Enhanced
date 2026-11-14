#include "Levels.h"
#include <stdexcept>

namespace inkball {
// Addresses of the original collection counts and tables are documented in
// RECONSTRUCTION.md. These ranges come from the supplied x86 executable.
const std::array<DifficultyCollection, 5> DifficultyCollections{{
    {"Beginner", 0, 138, 48}, {"Novice", 186, 78, 28},
    {"Intermediate", 292, 77, 26}, {"Advanced", 395, 73, 21},
    {"Expert", 489, 45, 17}
}};

const char* DifficultyName(int difficulty) {
    return DifficultyCollections[static_cast<std::size_t>(difficulty >= 0 && difficulty < 5 ? difficulty : 0)].name;
}

namespace {
class Reader {
public:
    Reader(const std::vector<std::uint8_t>& bytes, std::size_t first, std::size_t end)
        : bytes_(bytes), position_(first), end_(end) {}
    unsigned byte() {
        if (position_ >= end_ || position_ >= bytes_.size())
            throw std::runtime_error("The original InkBall level resource is truncated.");
        return bytes_[position_++];
    }
    std::uint16_t word() {
        const auto hi = byte();
        return static_cast<std::uint16_t>((hi << 8) | byte());
    }
    std::size_t position() const { return position_; }
private:
    const std::vector<std::uint8_t>& bytes_;
    std::size_t position_, end_;
};

void validateTile(std::uint16_t code) {
    const unsigned kind = code >> 12;
    const unsigned color = code & 15;
    const unsigned p1 = (code >> 4) & 15;
    const unsigned angle = (code >> 4) & 255;
    bool valid = kind <= 10;
    if (kind == 2 || kind == 5) valid = color <= 4 && p1 <= 3;
    if (kind == 3 || kind == 4 || kind == 6) valid = color <= 4;
    if (kind == 7) valid = color <= 4 && p1 <= 4;
    if (kind == 8) valid = p1 <= 3;
    if (kind == 9) valid = color <= 4 && (angle <= 31 || angle == 255);
    if (kind == 10) valid = color <= 4 && p1 <= 1;
    if (!valid) throw std::runtime_error("The InkBall resource contains an invalid tile code.");
}
}

std::vector<Level> LoadLevels(const std::vector<std::uint8_t>& resource) {
    std::vector<Level> result;
    result.reserve(551);
    std::size_t cursor = 0;
    for (int id = 0; id < 551; ++id) {
        Reader prefix(resource, cursor, resource.size());
        Level level;
        level.id = id;
        level.resourceOffset = static_cast<std::uint32_t>(cursor);
        level.recordLength = prefix.word();
        const auto end = cursor + 2 + level.recordLength;
        if (end > resource.size() || level.recordLength < 9)
            throw std::runtime_error("Invalid InkBall level record length.");
        Reader reader(resource, cursor + 2, end);
        level.timeSeconds = reader.word();
        level.spawnIntervalSeconds = static_cast<int>(reader.byte());
        level.minBallSpeed = reader.word();
        level.maxBallSpeed = reader.word();
        level.randomizeQueue = reader.byte() != 0;
        const auto ballCount = reader.byte();
        if (level.timeSeconds > 999 || level.spawnIntervalSeconds > 60 || ballCount > 64 ||
            level.minBallSpeed < 50 || level.maxBallSpeed > 300 ||
            level.minBallSpeed > level.maxBallSpeed)
            throw std::runtime_error("Invalid InkBall level header.");
        for (unsigned i = 0; i < ballCount; ++i) {
            const int color = static_cast<int>(reader.byte());
            if (color > 4) throw std::runtime_error("Invalid queued ball color.");
            level.queue.push_back(color);
        }
        for (auto& tile : level.tiles) {
            tile.code = reader.word();
            validateTile(tile.code);
            if ((tile.code >> 12) == 4) {
                tile.underlay = reader.word();
                validateTile(tile.underlay);
            }
        }
        // Fourteen original records contain extra bytes. The original loader
        // stops after 17*17 cells; preserve rather than reinterpret those bytes.
        while (reader.position() < end)
            level.unusedTrailingBytes.push_back(static_cast<std::uint8_t>(reader.byte()));
        for (std::size_t d = 0; d < DifficultyCollections.size(); ++d) {
            const auto& c = DifficultyCollections[d];
            if (id >= c.firstId && id < c.firstId + c.normalCount + c.bonusCount) {
                level.difficulty = static_cast<int>(d);
                level.bonus = id >= c.firstId + c.normalCount;
                break;
            }
        }
        result.push_back(std::move(level));
        cursor = end;
    }
    if (cursor + 1 != resource.size() || resource[cursor] != 0)
        throw std::runtime_error("The InkBall resource has an unexpected trailer.");
    return result;
}
}
