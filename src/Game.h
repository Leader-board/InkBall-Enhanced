#pragma once
#include "Levels.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace inkball {
// Logical board pixels. Share these with the renderer so a visible ink edge
// and its rounded end cap have the same collision thickness at every DPI.
inline constexpr double InkWidth = 10.0;
inline constexpr double InkRadius = InkWidth / 2.0;
inline constexpr int DefaultInkStrokeLimit = 20;
inline constexpr int MaximumInkStrokeLimit = 4096;
struct Vec2 { double x = 0; double y = 0; };
enum class Phase { Playing, LevelComplete, GameOver };
struct RunStatistics {
    double inkLength = 0; // Logical board pixels, independent of display DPI.
    double activeSeconds = 0;
    double completionActiveSeconds = 0, completionRemainingSeconds = 0;
    std::uint64_t timedBoardsCleared = 0;
    std::uint64_t strokes = 0, inkHits = 0, wallHits = 0;
    std::uint64_t ballHits = 0, ballsSunk = 0, boardsCleared = 0;
};
struct Statistics {
    RunStatistics run, game, previousRun, previousGame, lifetime;
    bool hasPreviousRun = false, hasPreviousGame = false;
};
struct CompletedBoard {
    int boardId = 0;
    double timeUsed = 0;
    int timeAvailable = 0; // Zero means unlimited.
    std::uint64_t points = 0, maximumPoints = 0;
    double inkLength = 0; // Total drawn on this board, including consumed/cleared ink.
};
// Scoring potential with recoloring; ignores geometric reachability/time.
std::uint64_t MaximumBoardPoints(const Level& level);
struct Tile {
    std::uint16_t code = 0;
    std::uint16_t underlay = 0;
    int animationFrame = 0; // 0..4: retracted to raised, for types 6 and 7.
    int gateState = 0;      // 0 retracting, 1 waiting until clear, 2 raised.
    double stateTimer = 0;
    double animationTimer = 0;
    int kind() const { return code >> 12; }
    int color() const { return code & 15; }
    int param1() const { return (code >> 4) & 15; }
    int param2() const { return (code >> 8) & 15; }
};
struct Ball {
    int id = 0;
    int color = 0;
    Vec2 position; // Center in board coordinates; the HUD is not included.
    Vec2 velocity;
    double radius = 12;
    int tallness = 0;
    bool active = true;
    bool nearHole = false;
};
struct Stroke {
    std::uint64_t id = 0;
    std::vector<Vec2> points;
};
// Session-only overrides for the current board. Original IKL records are never
// modified. A new board restores its original header and the original physics.
struct BoardMechanics {
    int timeLimitSeconds = 120;       // 0 means unlimited; 0..999.
    int launchIntervalSeconds = 4;    // 0..60; first queued launch remains 4 s.
    int minBallSpeed = 50;            // Original initial-speed range, px/s.
    int maxBallSpeed = 150;           // 1..1000; min <= max.
    double ballSpeedMultiplier = 1;   // 0.1..4; effective initial speed <= 2000.
    double chevronAccelerationMultiplier = 1; // 0..20; 1 preserves +3/step.
    double chevronSpeedLimit = 400;   // 1..2000 px/s in the chevron's axis.
    bool operator==(const BoardMechanics&) const = default;
};
struct GameState {
    std::array<Tile, BoardCellCount> tiles{};
    std::vector<Ball> balls;
    std::vector<Stroke> strokes;
    std::vector<int> queue; // Remaining queued colors, first element launches next.
    int difficulty = 0;
    int levelId = 0;
    int levelNumber = 1;
    bool isBonus = false;
    std::uint64_t score = 0;
    std::uint64_t highScore = 5000;
    int spareBlocks = 0;
    double timeRemaining = 120;
    bool unlimitedTime = false;
    bool timerStarted = false;
    bool timerVisible = true; // Low-time warning blanks the digits, not the HUD.
    double nextLaunch = 4;
    bool launcherBlink = false;
    bool paused = false;
    Phase phase = Phase::Playing;
    std::string message;
    double celebrationElapsed = 0;
    double elapsed = 0; // Active simulation time on this board; pauses freeze it.
    std::uint64_t levelSerial = 0; // Changes even when the same board is replayed.
};

int TileSprite(const Tile& tile);
int BallSprite(const Ball& ball);

class Game {
public:
    explicit Game(std::vector<Level> levels);
    const GameState& state() const { return state_; }
    const std::vector<Level>& levels() const { return levels_; }
    void newGame(int difficulty = 0, std::uint32_t seed = 0);
    void tick(double seconds);
    void pause(bool value);
    void beginStroke(Vec2 point);
    void extendStroke(Vec2 point);
    void endStroke();
    void clearInk();
    int inkStrokeLimit() const { return inkStrokeLimit_; }
    bool setInkStrokeLimit(int limit);
    bool placeBlock(Vec2 point);
    void restartLevel(); // Original loss-dialog behavior: same board, score reset.
    void loadLevel(std::size_t index, bool resetScore = true);
    const BoardMechanics& mechanics() const { return mechanics_; }
    BoardMechanics defaultMechanics() const;
    // Atomic validation and application. Existing positions, tiles, score,
    // phase and pause are preserved. Changing the configured time limit resets
    // remaining time; an unchanged limit preserves the current countdown.
    bool applyMechanics(const BoardMechanics& values, std::string& error);
    void setHighScore(std::uint64_t score);
    bool ballOverlapsTile(int column, int row) const;
    Statistics statistics() const;
    const std::vector<CompletedBoard>& completedBoards() const { return completedBoards_; }
    void seedLifetimeStatistics(const RunStatistics& totals);

private:
    std::vector<Level> levels_;
    GameState state_;
    BoardMechanics mechanics_;
    std::uint32_t randomState_ = 1;
    std::deque<int> normalHistory_;
    std::deque<int> bonusHistory_;
    std::vector<int> launchers_;
    std::uint64_t activeStroke_ = 0;
    std::uint64_t nextStrokeId_ = 1;
    std::uint64_t lastBlockAwardScore_ = 0;
    int nextBallId_ = 1;
    int completedLevels_ = 0;
    std::size_t currentLevelIndex_ = 0;
    double completionAccumulator_ = 0;
    double completionDelay_ = 0;
    double launchBlinkAccumulator_ = 0;
    double timerBlinkAccumulator_ = 0;
    bool strokeConsumed_ = false;
    Statistics statistics_;
    double priorBoardSeconds_ = 0;
    double boardActiveSeconds_ = 0; // Starts with the countdown, after the first launch wait.
    bool newStatisticsGame_ = true;
    bool lifetimeSeeded_ = false;
    std::vector<CompletedBoard> completedBoards_;
    int inkStrokeLimit_ = DefaultInkStrokeLimit; // User preference, retained across boards/games.
    std::uint64_t boardStartingScore_ = 0;
    void startStatisticsGame();
    void count(std::uint64_t RunStatistics::* field) {
        ++(statistics_.run.*field); ++(statistics_.game.*field); ++(statistics_.lifetime.*field);
    }

    int randomInt(int bound);
    std::size_t chooseLevel(bool bonus);
    void initializeLevel(std::size_t index, bool preserveMechanics = false);
    void resetTile(Tile& tile, std::uint16_t code, std::uint16_t underlay = 0);
    Vec2 initialVelocity(double angleDegrees = -1);
    void spawnBall(int color, Vec2 position, double angleDegrees = -1);
    void updateGates(double seconds);
    void updateLaunchers(double seconds);
    void updateBall(Ball& ball, double seconds);
    void collideBalls(Ball& ball);
    void collideInk(Ball& ball, Vec2 oldPosition);
    void collideWalls(Ball& ball, Vec2 oldPosition);
    void interactFloor(Ball& ball);
    void toggleKeyWalls(int color);
    void addScore(std::uint64_t points);
    void awardBlocks();
    void lose(const char* message);
    void checkCompletion();
};
}
