#include "BoardCatalog.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>
#include <numeric>
#include <stdexcept>
#include <string>

using namespace inkball;
namespace {
int checks = 0;
void require(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
bool contains(const std::string& text, const std::string& value) {
    return text.find(value) != std::string::npos;
}
}

int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("Usage: CatalogTests <levels.ikl>");
        std::ifstream input(argv[1], std::ios::binary);
        if (!input) throw std::runtime_error("Cannot open the original level resource.");
        const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)), {});
        const auto levels = LoadLevels(bytes);
        require(FilterBoards(levels).size() == 551, "The browser must expose all 551 original boards.");
        require(FilterBoards(levels, -1, BoardKindFilter::Normal).size() == 411,
            "The normal-board filter must include all 411 normal boards.");
        require(FilterBoards(levels, -1, BoardKindFilter::Bonus).size() == 140,
            "The bonus-board filter must include all 140 bonus boards.");
        for (int difficulty = 0; difficulty < 5; ++difficulty) {
            const auto& collection = DifficultyCollections[static_cast<std::size_t>(difficulty)];
            const auto normal = FilterBoards(levels, difficulty, BoardKindFilter::Normal);
            const auto bonus = FilterBoards(levels, difficulty, BoardKindFilter::Bonus);
            require(normal.size() == static_cast<std::size_t>(collection.normalCount), "Wrong normal difficulty count.");
            require(bonus.size() == static_cast<std::size_t>(collection.bonusCount), "Wrong bonus difficulty count.");
            require(std::all_of(normal.begin(), normal.end(), [&](std::size_t i) {
                return levels[i].difficulty == difficulty && !levels[i].bonus;
            }), "Difficulty/normal filters leaked other boards.");
            require(std::all_of(bonus.begin(), bonus.end(), [&](std::size_t i) {
                return levels[i].difficulty == difficulty && levels[i].bonus;
            }), "Difficulty/bonus filters leaked other boards.");
        }
        require(FilterBoards(levels, 99).empty(), "A nonexistent difficulty must return no boards.");

        // IDs differ from vector indexes, and the equal-time rows arrive in the
        // reverse order. This catches a browser accidentally loading an ID/row.
        std::vector<Level> fixture(5);
        fixture[0].id = 80; fixture[0].timeSeconds = 120;
        fixture[1].id = 70; fixture[1].timeSeconds = 0;
        fixture[2].id = 60; fixture[2].timeSeconds = 9;
        fixture[3].id = 50; fixture[3].timeSeconds = 120;
        fixture[4].id = 40; fixture[4].timeSeconds = 999;
        require(FilterBoards(fixture, -1, BoardKindFilter::All, BoardSort::TimeAscending) ==
            std::vector<std::size_t>({2, 3, 0, 4, 1}),
            "Ascending time sort must be numeric, use stable ID ties, and put unlimited last.");
        require(FilterBoards(fixture, -1, BoardKindFilter::All, BoardSort::TimeDescending) ==
            std::vector<std::size_t>({1, 4, 3, 0, 2}),
            "Descending time sort must put unlimited first and keep ascending IDs for ties.");
        require(FilterBoards(fixture) == std::vector<std::size_t>({4, 3, 2, 1, 0}),
            "Default order must use IDs while returning collection indexes.");
        require(BoardTimeText(fixture[1]) == "Unlimited" && BoardTimeText(fixture[2]) == "9 s",
            "Time display must distinguish unlimited from a numeric zero-second limit.");

        for (const auto sort : {BoardSort::TimeAscending, BoardSort::TimeDescending}) {
            const auto rows = FilterBoards(levels, -1, BoardKindFilter::All, sort);
            bool sorted = rows.size() == levels.size();
            for (std::size_t i = 1; i < rows.size(); ++i) {
                const auto& previous = levels[rows[i - 1]];
                const auto& current = levels[rows[i]];
                const int previousTime = previous.timeSeconds == 0 ? 100000 : previous.timeSeconds;
                const int currentTime = current.timeSeconds == 0 ? 100000 : current.timeSeconds;
                sorted &= sort == BoardSort::TimeAscending ? previousTime <= currentTime : previousTime >= currentTime;
                if (previousTime == currentTime) sorted &= previous.id < current.id;
            }
            require(sorted, "An original-board time ordering is incorrect.");
        }

        bool countsValid = true;
        for (const auto& level : levels) {
            const auto stats = InspectBoard(level);
            countsValid &= std::accumulate(stats.tileKinds.begin(), stats.tileKinds.end(), 0) == BoardCellCount;
            countsValid &= std::accumulate(stats.queuedColors.begin(), stats.queuedColors.end(), 0) ==
                static_cast<int>(level.queue.size());
        }
        require(countsValid, "Board details lost cells or queued colors from an original board.");

        Level detail;
        detail.id = 321;
        detail.difficulty = 2;
        detail.bonus = true;
        detail.randomizeQueue = true;
        detail.timeSeconds = 0;
        detail.spawnIntervalSeconds = 7;
        detail.minBallSpeed = 70;
        detail.maxBallSpeed = 160;
        detail.queue = {4, 2, 4};
        detail.tiles[3] = {0x6214, 0};
        detail.tiles[4] = {0x4004, 0x2022};
        detail.tiles[5] = {0x9FF2, 0};
        detail.resourceOffset = 1234;
        detail.recordLength = 600;
        detail.unusedTrailingBytes = {0x12, 0xAB};
        const auto description = DescribeBoard(detail);
        require(contains(description, "BOARD 321  |  Intermediate  |  Bonus") &&
            contains(description, "Time allowed: Unlimited") &&
            contains(description, "Launch interval: 7 s") &&
            contains(description, "70 to 160 px/s"),
            "The detail pane must report the selected board's original header values.");
        require(contains(description, "Queued balls: 3; preplaced balls: 1") &&
            contains(description, "Gold, Blue, Gold (shuffled when played)") &&
            contains(description, "Randomize queue: Yes"),
            "Queue details must report stored colors and the shuffle flag separately.");
        require(contains(description, "Record offset: 1234 (0x000004D2)") &&
            contains(description, "Record payload: 600 bytes") &&
            contains(description, "Unused trailing bytes: 2 0x12 0xAB"),
            "Original resource metadata must remain inspectable.");
        const auto timed = DescribeBoardTile(detail, 3, 0);
        require(contains(timed, "column 4, row 1") && contains(timed, "Raw code: 0x6214") &&
            contains(timed, "Initial raised frame: 4 / 4") && contains(timed, "Open: 1 s; closed: 1.5 s"),
            "The tile inspector must decode timed-wall parameters and one-based coordinates correctly.");
        const auto breakable = DescribeBoardTile(detail, 4, 0);
        require(contains(breakable, "Revealed when broken:") && contains(breakable, "Raw code: 0x2022") &&
            contains(breakable, "Color: Blue; quadrant: Lower left"),
            "The inspector must reveal a breakable wall's actual underlay.");
        const auto ball = DescribeBoardTile(detail, 5, 0);
        require(contains(ball, "Ball color: Blue; heading: Random (255)"),
            "The inspector must preserve a preplaced ball's random-direction encoding.");
        require(DescribeBoardTile(detail, -1, 0).empty() && DescribeBoardTile(detail, 17, 0).empty(),
            "Clicking outside the board must not expose an invalid tile.");
        std::cout << "PASS: " << checks << " catalog checks, all 551 original boards.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << " (after " << checks << " checks)\n";
        return 1;
    }
}
