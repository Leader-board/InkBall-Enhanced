#pragma once
#include "Game.h"
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <cmath>
#include <charconv>

namespace inkball {
// Separate versioned file: existing settings and older data remain intact.
inline bool LoadLifetimeStatistics(const std::wstring& path, RunStatistics& result) {
    std::ifstream input{std::filesystem::path(path)};
    std::string magic;
    unsigned version = 0;
    RunStatistics s;
    if (!(input >> magic >> version) || magic != "InkBallStatistics" || version != 1) return false;
    const auto counter = [&](std::uint64_t& value) {
        std::string token;
        if (!(input >> token)) return false;
        const auto parsed = std::from_chars(token.data(), token.data()+token.size(),value);
        return parsed.ec == std::errc{} && parsed.ptr == token.data()+token.size();
    };
    if (!(input >> s.inkLength >> s.activeSeconds) || !counter(s.strokes) || !counter(s.inkHits)
        || !counter(s.wallHits) || !counter(s.ballHits) || !counter(s.ballsSunk) || !counter(s.boardsCleared)
        || !(input >> s.completionActiveSeconds >> s.completionRemainingSeconds) || !counter(s.timedBoardsCleared)) return false;
    for (const double value : {s.inkLength, s.activeSeconds, s.completionActiveSeconds, s.completionRemainingSeconds})
        if (!std::isfinite(value) || value < 0) return false;
    if (s.timedBoardsCleared > s.boardsCleared) return false;
    input >> std::ws;
    if (!input.eof()) return false;
    result = s;
    return true;
}
inline bool SaveLifetimeStatistics(const std::wstring& path, const RunStatistics& s) {
    const auto pending = path + L".tmp";
    {
        std::ofstream output{std::filesystem::path(pending), std::ios::trunc};
        output << "InkBallStatistics 1\n" << std::setprecision(17)
            << s.inkLength << ' ' << s.activeSeconds << ' ' << s.strokes << ' ' << s.inkHits << ' '
            << s.wallHits << ' ' << s.ballHits << ' ' << s.ballsSunk << ' ' << s.boardsCleared << ' '
            << s.completionActiveSeconds << ' ' << s.completionRemainingSeconds << ' ' << s.timedBoardsCleared << '\n';
        output.flush();
        output.close();
        if (!output) return false;
    }
    return MoveFileExW(pending.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
}
}
