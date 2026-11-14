#pragma once
#include "Levels.h"
#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace inkball {

enum class BoardKindFilter { All, Normal, Bonus };
enum class BoardSort { Id, TimeAscending, TimeDescending };

struct BoardStatistics {
    std::array<int, 11> tileKinds{};
    std::array<int, 5> queuedColors{};
};

// Results are indexes into the immutable level collection, never display rows
// or board IDs. Unlimited time sorts as infinity; equal times use ascending ID.
std::vector<std::size_t> FilterBoards(const std::vector<Level>& levels,
    int difficulty = -1, BoardKindFilter kind = BoardKindFilter::All,
    BoardSort sort = BoardSort::Id);
BoardStatistics InspectBoard(const Level& level);
std::string BoardTimeText(const Level& level);
std::string DescribeBoard(const Level& level);
std::string DescribeBoardTile(const Level& level, int column, int row);

}
