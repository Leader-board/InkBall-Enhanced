#include "BoardCatalog.h"
#include <algorithm>
#include <iomanip>
#include <limits>
#include <sstream>

namespace inkball {
namespace {
constexpr const char* Colors[] = {"Gray", "Red", "Blue", "Green", "Gold"};
constexpr const char* Kinds[] = {
    "Floor", "Launcher", "Hole quadrant", "Solid / recoloring wall",
    "Breakable wall", "One-way wall / filter", "Timed wall", "Keyhole wall",
    "Ramp", "Preplaced ball", "Two-way color filter"
};
constexpr const char* Directions[] = {"Up", "Down", "Left", "Right"};

const char* ColorName(int color) {
    return color >= 0 && color < 5 ? Colors[color] : "Unknown";
}
const char* DirectionName(int direction) {
    return direction >= 0 && direction < 4 ? Directions[direction] : "Unknown";
}
std::string Hex(std::uint32_t value, int width) {
    std::ostringstream out;
    out << "0x" << std::uppercase << std::hex << std::setw(width)
        << std::setfill('0') << value;
    return out.str();
}
void DescribeCode(std::ostringstream& out, std::uint16_t code) {
    const int kind = code >> 12, p0 = code & 15;
    const int p1 = (code >> 4) & 15, p2 = (code >> 8) & 15;
    out << (kind < 11 ? Kinds[kind] : "Unknown tile") << "\r\n"
        << "Raw code: " << Hex(code, 4) << "; kind " << kind
        << ", p0 " << p0 << ", p1 " << p1 << ", p2 " << p2 << "\r\n";
    switch (kind) {
    case 2: {
        constexpr const char* quadrants[] = {"Upper left", "Upper right", "Lower left", "Lower right"};
        out << "Color: " << ColorName(p0) << "; quadrant: "
            << (p1 < 4 ? quadrants[p1] : "Unknown") << "\r\n";
        break;
    }
    case 3: case 4:
        out << "Color: " << ColorName(p0) << "\r\n";
        break;
    case 5:
        out << "Color: " << ColorName(p0) << "; permitted direction: "
            << DirectionName(p1) << "\r\n";
        break;
    case 6:
        out << "Initial raised frame: " << p0 << " / 4\r\n"
            << "Open: " << (p1 + 1) * 0.5 << " s; closed: " << (p2 + 1) * 0.5 << " s\r\n";
        break;
    case 7:
        out << "Key color: " << ColorName(p0) << "; initial raised frame: " << p1 << " / 4\r\n";
        break;
    case 8:
        out << "Ramp direction: " << DirectionName(p1) << "\r\n";
        break;
    case 9: {
        const int heading = (code >> 4) & 255;
        out << "Ball color: " << ColorName(p0) << "; heading: ";
        if (heading == 255) out << "Random (255)";
        else out << heading << " (" << heading * 11.25 << " degrees clockwise from up)";
        out << "\r\n";
        break;
    }
    case 10:
        out << "Passes color: " << ColorName(p0) << "; orientation: "
            << (p1 == 0 ? "Horizontal" : p1 == 1 ? "Vertical" : "Unknown") << "\r\n";
        break;
    default: break;
    }
}
}

std::vector<std::size_t> FilterBoards(const std::vector<Level>& levels,
    int difficulty, BoardKindFilter kind, BoardSort sort) {
    std::vector<std::size_t> result;
    result.reserve(levels.size());
    for (std::size_t i = 0; i < levels.size(); ++i) {
        const auto& level = levels[i];
        if (difficulty >= 0 && level.difficulty != difficulty) continue;
        if (kind == BoardKindFilter::Normal && level.bonus) continue;
        if (kind == BoardKindFilter::Bonus && !level.bonus) continue;
        result.push_back(i);
    }
    std::stable_sort(result.begin(), result.end(), [&](std::size_t a, std::size_t b) {
        const auto& left = levels[a];
        const auto& right = levels[b];
        if (sort != BoardSort::Id) {
            const int leftTime = left.timeSeconds == 0 ? std::numeric_limits<int>::max() : left.timeSeconds;
            const int rightTime = right.timeSeconds == 0 ? std::numeric_limits<int>::max() : right.timeSeconds;
            if (leftTime != rightTime)
                return sort == BoardSort::TimeAscending ? leftTime < rightTime : leftTime > rightTime;
        }
        return left.id < right.id;
    });
    return result;
}

BoardStatistics InspectBoard(const Level& level) {
    BoardStatistics result;
    for (const auto& tile : level.tiles) {
        const int kind = tile.code >> 12;
        if (kind < static_cast<int>(result.tileKinds.size())) ++result.tileKinds[static_cast<std::size_t>(kind)];
    }
    for (int color : level.queue)
        if (color >= 0 && color < static_cast<int>(result.queuedColors.size()))
            ++result.queuedColors[static_cast<std::size_t>(color)];
    return result;
}

std::string BoardTimeText(const Level& level) {
    return level.timeSeconds == 0 ? "Unlimited" : std::to_string(level.timeSeconds) + " s";
}

std::string DescribeBoard(const Level& level) {
    const auto stats = InspectBoard(level);
    std::ostringstream out;
    out << "BOARD " << level.id << "  |  " << DifficultyName(level.difficulty)
        << "  |  " << (level.bonus ? "Bonus" : "Normal") << "\r\n"
        << "Resource board ID: " << level.id << " (zero-based)\r\n"
        << "Time allowed: " << BoardTimeText(level) << "\r\n"
        << "Launch interval: " << level.spawnIntervalSeconds << " s (first queued launch: 4 s)\r\n"
        << "Initial speed bounds: " << level.minBallSpeed << " to " << level.maxBallSpeed << " px/s\r\n"
        << "Queued balls: " << level.queue.size() << "; preplaced balls: " << stats.tileKinds[9] << "\r\n"
        << "Randomize queue: " << (level.randomizeQueue ? "Yes" : "No") << "\r\n"
        << "Stored queue: ";
    if (level.queue.empty()) out << "None";
    for (std::size_t i = 0; i < level.queue.size(); ++i) {
        if (i) out << ", ";
        out << ColorName(level.queue[i]);
    }
    if (level.randomizeQueue) out << " (shuffled when played)";
    out << "\r\nQueue colors: ";
    for (std::size_t i = 0; i < stats.queuedColors.size(); ++i) {
        if (i) out << "; ";
        out << Colors[i] << " " << stats.queuedColors[i];
    }
    out << "\r\n\r\nTILE COUNTS (17 x 17 cells)\r\n";
    for (std::size_t i = 0; i < stats.tileKinds.size(); ++i)
        out << Kinds[i] << ": " << stats.tileKinds[i] << "\r\n";
    out << "Hole counts above are quadrants; a complete hole occupies four cells.\r\n"
        << "Breakable-wall replacements are shown by clicking their tiles.\r\n"
        << "\r\nRESOURCE PARAMETERS\r\n"
        << "Record offset: " << level.resourceOffset << " (" << Hex(level.resourceOffset, 8) << ")\r\n"
        << "Record payload: " << level.recordLength << " bytes\r\n"
        << "Unused trailing bytes: " << level.unusedTrailingBytes.size();
    for (auto byte : level.unusedTrailingBytes) out << " " << Hex(byte, 2);
    out << "\r\n\r\nThe preview is static. It does not advance the current game.\r\n";
    return out.str();
}

std::string DescribeBoardTile(const Level& level, int column, int row) {
    if (column < 0 || column >= BoardColumns || row < 0 || row >= BoardRows) return {};
    const auto& tile = level.tiles[static_cast<std::size_t>(row * BoardColumns + column)];
    std::ostringstream out;
    out << "SELECTED TILE: column " << column + 1 << ", row " << row + 1
        << " (zero-based " << column << ", " << row << ")\r\n";
    DescribeCode(out, tile.code);
    if ((tile.code >> 12) == 4) {
        out << "Revealed when broken:\r\n";
        DescribeCode(out, tile.underlay);
    }
    out << "\r\n";
    return out.str();
}
}
