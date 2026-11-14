#include "Game.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>

using namespace inkball;
namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void near(double actual, double expected, double tolerance, const char* message) {
    if (std::abs(actual - expected) > tolerance) throw std::runtime_error(message);
}
Level emptyLevel() {
    Level l;
    l.timeSeconds = 600;
    l.minBallSpeed = l.maxBallSpeed = 100;
    for (int y = 0; y < BoardRows; ++y) for (int x = 0; x < BoardColumns; ++x)
        if (x == 0 || y == 0 || x == 16 || y == 16) l.tiles[static_cast<std::size_t>(y * 17 + x)].code = 0x3000;
    return l;
}
void tile(Level& level, int x, int y, unsigned code, unsigned underlay = 0) {
    level.tiles[static_cast<std::size_t>(y * BoardColumns + x)] = {
        static_cast<std::uint16_t>(code), static_cast<std::uint16_t>(underlay)};
}
void ball(Level& level, int x, int y, int color, int angle) {
    tile(level, x, y, 0x9000u | static_cast<unsigned>(angle << 4) | static_cast<unsigned>(color));
}
void hole(Level& level, int x, int y, int color) {
    tile(level, x, y, 0x2000 | color); tile(level, x + 1, y, 0x2010 | color);
    tile(level, x, y + 1, 0x2020 | color); tile(level, x + 1, y + 1, 0x2030 | color);
}
Game gameFor(Level level) {
    Game game({std::move(level)});
    game.newGame(0, 12345);
    return game;
}
void advanceUntil(Game& g, const std::function<bool()>& predicate, double limit = 10) {
    for (double elapsed = 0; elapsed < limit && !predicate(); elapsed += 0.004) g.tick(0.004);
    require(predicate(), "Expected event did not occur within the simulation limit.");
}
}

int main(int argc, char** argv) {
    int checks = 0;
    try {
        if (argc != 2) throw std::runtime_error("Usage: EngineTests <levels.bin>");
        std::ifstream input(argv[1], std::ios::binary);
        if (!input) throw std::runtime_error("Cannot open the extracted level resource.");
        const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)), {});
        const auto levels = LoadLevels(bytes);
        require(levels.size() == 551, "All 551 original levels must decode."); ++checks;
        int tails = 0;
        std::array<int, 5> regular{}, bonus{};
        for (const auto& l : levels) {
            if (!l.unusedTrailingBytes.empty()) ++tails;
            (l.bonus ? bonus : regular)[static_cast<std::size_t>(l.difficulty)]++;
        }
        require(tails == 14, "All fourteen original ignored tails must be retained."); ++checks;
        require(regular == std::array<int, 5>{{138, 78, 77, 73, 45}}, "Normal difficulty counts differ from the binary."); ++checks;
        require(bonus == std::array<int, 5>{{48, 28, 26, 21, 17}}, "Bonus difficulty counts differ from the binary."); ++checks;
        require(levels[0].timeSeconds == 120 && levels[0].queue == std::vector<int>({1, 2}), "Original first-level header mismatch."); ++checks;
        bool rejected = false;
        try { auto truncated = bytes; truncated.resize(200); (void)LoadLevels(truncated); }
        catch (const std::runtime_error&) { rejected = true; }
        require(rejected, "A truncated level resource must be rejected."); ++checks;

        // Load and simulate every original board, including special blocks and
        // preplaced balls. This exercises bounds and all recovered tile kinds.
        Game all(levels);
        for (std::size_t i = 0; i < levels.size(); ++i) {
            all.loadLevel(i);
            all.tick(0.12);
            require(all.state().levelId == static_cast<int>(i), "Direct level loading selected the wrong board.");
            for (const auto& b : all.state().balls)
                require(std::isfinite(b.position.x) && std::isfinite(b.position.y) &&
                        std::isfinite(b.velocity.x) && std::isfinite(b.velocity.y), "Physics produced non-finite values.");
            for (const auto& t : all.state().tiles)
                require(TileSprite(t) >= 0 && TileSprite(t) <= 105, "Tile sprite is outside the original atlas.");
        }
        checks += 551;
        {
            Game g(levels); g.newGame(0, 42);
            std::deque<int> recent;
            for (int i = 0; i < 24; ++i) {
                const int id = g.state().levelId;
                require(std::find(recent.begin(), recent.end(), id) == recent.end(),
                        "F2 must avoid the ten most recently selected normal boards.");
                recent.push_back(id);
                if (recent.size() > 10) recent.pop_front();
                g.newGame(0);
            }
            ++checks;
        }

        {
            auto l = emptyLevel(); ball(l, 2, 3, 1, 8);
            auto g = gameFor(l);
            g.tick(0.10);
            near(g.state().balls[0].position.x, 90, 1e-7, "Preplaced ball speed or heading is incorrect.");
            const auto x = g.state().balls[0].position.x;
            const auto time = g.state().timeRemaining;
            g.pause(true); g.tick(2);
            near(g.state().balls[0].position.x, x, 1e-9, "Paused physics must not advance.");
            near(g.state().timeRemaining, time, 1e-9, "Paused timer must not advance.");
            g.pause(false); g.tick(0.1);
            near(g.state().balls[0].position.x, x + 10, 1e-7, "Resuming must restart physics.");
            checks += 4;
        }
        {
            auto l = emptyLevel(); l.queue = {1, 2}; l.spawnIntervalSeconds = 2;
            tile(l, 8, 8, 0x1000);
            auto g = gameFor(l); g.tick(3.9);
            require(g.state().balls.empty() && !g.state().timerStarted, "Initial launch must wait four seconds.");
            near(g.state().timeRemaining, 600, 1e-8, "Timer must wait for the first launch.");
            g.tick(0.11);
            require(g.state().balls.size() == 1 && g.state().queue.size() == 1 && g.state().timerStarted,
                    "First launch must start the timer and consume one queued ball.");
            g.tick(2.1);
            require(g.state().queue.empty() && g.state().balls.size() == 2, "Subsequent launches must use the level interval.");
            checks += 4;
        }
        {
            auto l = emptyLevel(); ball(l, 1, 3, 1, 8); tile(l, 3, 3, 0x3002);
            auto g = gameFor(l); g.tick(0.40);
            require(g.state().balls[0].velocity.x < 0, "A solid wall must reflect the ball.");
            require(g.state().balls[0].color == 2, "A colored solid wall must repaint the ball.");
            require(g.statistics().run.wallHits > 0 && g.statistics().game.wallHits == g.statistics().run.wallHits,
                "Successful wall reflections must count in run and game totals."); ++checks;
            checks += 2;
        }
        for (const auto code : {0x4000u, 0x4001u, 0x4002u}) {
            auto l = emptyLevel(); ball(l, 1, 3, 1, 8); tile(l, 3, 3, code, 0x8030);
            auto g = gameFor(l); g.tick(0.4);
            const bool shouldBreak = code != 0x4002u;
            require((g.state().tiles[54].code == 0x8030) == shouldBreak, "Break-wall color or hidden replacement behavior is wrong.");
            require(g.state().score == (shouldBreak ? 10u : 0u), "Each matching break wall must award ten points.");
            checks += 2;
        }
        {
            auto l = emptyLevel(); ball(l, 2, 3, 1, 8);
            auto g = gameFor(l);
            g.beginStroke({120, 90}); g.extendStroke({120, 135}); g.endStroke();
            g.tick(0.4);
            require(g.state().balls[0].velocity.x < 0, "An ink stroke must deflect the ball.");
            require(g.state().strokes.empty(), "The complete ink stroke must disappear on its first hit.");
            require(g.statistics().run.inkHits == 1 && g.statistics().game.inkHits == 1,
                "A consumed stroke must count as one ink deflection."); ++checks;
            g.beginStroke({200, 200}); g.extendStroke({210, 210}); g.endStroke(); g.clearInk();
            require(g.state().strokes.empty(), "Clear ink must remove every stroke.");
            checks += 3;
        }
        {
            // The reference stroke is ten logical pixels wide: a radius-12
            // ball reaches its visible left edge at center x=103, rather than
            // having to overlap the thin center line of the old renderer.
            auto l = emptyLevel(); ball(l, 2, 3, 1, 8);
            auto g = gameFor(l);
            g.beginStroke({120, 80}); g.extendStroke({120, 150}); g.endStroke();
            g.tick(0.22);
            require(g.state().balls[0].velocity.x > 0 && !g.state().strokes.empty(),
                    "Ink must not collide before the ball reaches its visible edge.");
            g.tick(0.02);
            require(g.state().balls[0].velocity.x < 0 && g.state().strokes.empty(),
                    "The outer part of the ten-pixel ink stroke must deflect the ball.");
            checks += 2;
        }
        {
            // This trajectory misses the line itself but grazes its round end.
            auto l = emptyLevel(); ball(l, 2, 3, 1, 8);
            auto g = gameFor(l);
            g.beginStroke({120, 128}); g.extendStroke({120, 155}); g.endStroke();
            g.tick(0.60);
            require(g.state().balls[0].velocity.y < -1 && g.state().strokes.empty(),
                    "A glancing hit on a rounded ink end cap must deflect and consume it.");
            ++checks;
        }
        {
            auto l = emptyLevel(); ball(l, 2, 3, 1, 8);
            auto g = gameFor(l);
            g.beginStroke({120, 130}); g.extendStroke({120, 155}); g.endStroke();
            g.tick(0.60);
            require(g.state().balls[0].velocity.x > 0 && g.state().strokes.size() == 1,
                    "A trajectory one pixel outside the rounded ink end must miss.");
            near(g.state().balls[0].position.y, 112, 1e-7,
                 "Missing a rounded ink end must leave the ball's heading unchanged.");
            checks += 2;
        }
        {
            auto l = emptyLevel(); ball(l, 2, 3, 1, 8);
            auto g = gameFor(l);
            g.beginStroke({120, 112}); // A held-down, single-point ink dot.
            g.tick(0.24);
            require(g.state().balls[0].velocity.x < 0 && g.state().strokes.empty(),
                    "A single-point ink dot must have the same five-pixel radius as a stroke cap.");
            g.extendStroke({200, 112});
            require(g.state().strokes.empty(),
                    "Continuing a consumed active stroke must not recreate its ink.");
            g.endStroke();
            g.beginStroke({200, 200}); g.extendStroke({210, 210}); g.endStroke();
            require(g.state().strokes.size() == 1 && g.state().strokes[0].points.size() == 2,
                    "A fresh stroke must work after an active stroke is consumed.");
            checks += 3;
        }
        {
            auto l = emptyLevel(); ball(l, 2, 5, 1, 8); tile(l, 3, 5, 0x8030);
            auto g = gameFor(l); g.tick(0.2);
            require(g.state().balls[0].velocity.x > 100 && g.state().balls[0].velocity.x <= 400,
                    "Ramps must add directional acceleration while respecting the velocity cap."); ++checks;
        }
        for (const int ballColor : {1, 2}) {
            auto l = emptyLevel(); ball(l, 1, 3, ballColor, 8); tile(l, 3, 3, 0xa011);
            auto g = gameFor(l); g.tick(0.60);
            require((g.state().balls[0].velocity.x > 0) == (ballColor == 1),
                    "A color filter must pass matching balls and deflect other colors."); ++checks;
        }
        {
            auto l = emptyLevel(); ball(l, 1, 3, 1, 8); tile(l, 3, 3, 0x5020);
            auto g = gameFor(l); g.tick(0.4);
            require(g.state().balls[0].velocity.x < 0, "A one-way wall must block the opposite direction."); ++checks;
        }
        {
            auto l = emptyLevel(); ball(l, 5, 3, 1, 24); tile(l, 3, 3, 0x5020);
            auto g = gameFor(l); g.tick(0.8);
            require(g.state().balls[0].velocity.x < 0 && g.state().balls[0].position.x < 110,
                    "A one-way wall must permit travel in the arrow direction."); ++checks;
        }
        {
            auto l = emptyLevel(); ball(l, 2, 3, 1, 8); ball(l, 5, 3, 2, 24);
            auto g = gameFor(l); g.tick(0.4);
            require(g.state().balls[0].velocity.x < 0 && g.state().balls[1].velocity.x > 0,
                    "Equal-mass ball collisions must exchange their normal velocities."); ++checks;
        }
        for (const auto& pair : {std::pair<int, int>{1, 1}, {2, 2}, {3, 3}, {4, 4}, {0, 1}, {2, 0}, {1, 2}}) {
            auto l = emptyLevel(); ball(l, 5, 4, pair.first, 14); hole(l, 5, 5, pair.second);
            tile(l, 10, 10, static_cast<unsigned>(0x7040 | pair.second));
            auto g = gameFor(l);
            advanceUntil(g, [&] { return g.state().phase != Phase::Playing; });
            const bool wrong = pair.first && pair.second && pair.first != pair.second;
            require((g.state().phase == Phase::GameOver) == wrong, "Wrong-hole failure or gray neutrality is incorrect.");
            if (!wrong) {
                const std::uint64_t expected = pair.first && pair.second ? (100u << pair.first) : 0;
                require(g.state().score == expected, "Drain score must match the original color table.");
                if (pair.first && pair.second)
                    require(g.state().tiles[180].gateState == 0, "Sinking a colored ball must toggle its colored keyhole walls.");
            } else {
                const auto id = g.state().levelId;
                g.restartLevel();
                require(g.state().phase == Phase::Playing && g.state().levelId == id && g.state().score == 0,
                        "After a loss, restart must reset score and replay the same board.");
            }
            checks += 2;
        }
        {
            // A partially uncovered hole is legal in the original levels:
            // breakable quadrants can reveal the shared center independently.
            auto l = emptyLevel(); l.minBallSpeed = l.maxBallSpeed = 300;
            ball(l, 3, 5, 1, 8); tile(l, 5, 5, 0x2001);
            auto g = gameFor(l);
            advanceUntil(g, [&] { return g.state().balls[0].radius < 12; }, 1);
            g.tick(0.4);
            require(g.state().balls[0].active && g.state().balls[0].position.x > 224,
                    "Escaped-hole regression must actually leave the exposed quadrant.");
            near(g.state().balls[0].radius, 12, 1e-9,
                 "A ball escaping a hole onto floor must regain its original diameter.");
            checks += 2;
        }
        {
            auto l = emptyLevel(); ball(l, 5, 4, 4, 14); hole(l, 5, 5, 4); ball(l, 13, 13, 0, 0);
            auto g = gameFor(l);
            advanceUntil(g, [&] { return g.state().score >= 1600; });
            require(g.state().spareBlocks == 1, "Each 1600 points must grant one placeable wall.");
            require(!g.placeBlock({16, 16}), "A wall cannot replace an existing wall.");
            require(g.placeBlock({270, 270}), "An earned wall must be placeable on an empty tile.");
            require(g.state().spareBlocks == 0 && !g.placeBlock({302, 270}), "Wall placement must consume its credit.");
            const auto high = g.state().highScore; g.newGame(0, 123);
            require(g.state().highScore == high && g.state().score == 0, "New game must preserve the high score.");
            checks += 5;
        }
        {
            auto l = emptyLevel(); ball(l, 1, 1, 1, 8); tile(l, 10, 10, 0x6114);
            auto g = gameFor(l); g.tick(1.5);
            require(g.state().tiles[180].animationFrame == 0, "A clock wall must retract on its encoded schedule.");
            g.tick(0.9);
            require(g.state().tiles[180].animationFrame > 0, "A clock wall must rise again after its open interval.");
            checks += 2;
        }
        {
            auto l = emptyLevel(); l.timeSeconds = 1; ball(l, 2, 3, 1, 8);
            auto g = gameFor(l); g.tick(1.1);
            require(g.state().phase == Phase::GameOver && g.state().timeRemaining == 0, "Timer expiry must end the game."); ++checks;
        }
        {
            auto l = emptyLevel(); l.timeSeconds = 20;
            ball(l, 5, 4, 1, 14); hole(l, 5, 5, 1);
            auto g = gameFor(l);
            advanceUntil(g, [&] { return g.state().phase == Phase::LevelComplete; });
            const auto score = g.state().score;
            const auto bonusPoints = static_cast<std::uint64_t>(std::ceil(g.state().timeRemaining));
            g.tick(static_cast<double>(bonusPoints) * 0.020 + 0.1);
            require(g.state().score == score + bonusPoints && g.state().timeRemaining == 0,
                    "Level completion must convert each remaining second to one point."); ++checks;
        }
        {
            auto normal = emptyLevel(); normal.id = 0; normal.timeSeconds = 0;
            auto bonusLevel = normal; bonusLevel.id = 1; bonusLevel.bonus = true;
            Game g({normal, bonusLevel}); g.newGame(0, 42);
            advanceUntil(g, [&] { return g.state().levelNumber == 6; }, 15);
            require(g.state().isBonus, "Original bonus selection must occur after five completed boards."); ++checks;
        }

        // Current-board mechanics are session overrides. Applying values must
        // not rebuild the board or silently restart its countdown.
        {
            auto l = emptyLevel(); l.timeSeconds = 120; l.spawnIntervalSeconds = 7;
            ball(l, 2, 3, 1, 8);
            auto g = gameFor(l);
            const auto defaults = g.defaultMechanics();
            require(g.mechanics() == defaults && defaults.timeLimitSeconds == 120 &&
                    defaults.launchIntervalSeconds == 7 && defaults.minBallSpeed == 100 &&
                    defaults.maxBallSpeed == 100 && defaults.ballSpeedMultiplier == 1 &&
                    defaults.chevronAccelerationMultiplier == 1 && defaults.chevronSpeedLimit == 400,
                    "Mechanics defaults must reproduce the selected board and original physics."); ++checks;
            g.tick(0.2);
            g.beginStroke({250, 250}); g.extendStroke({300, 250}); g.endStroke();
            g.setHighScore(12000);
            g.pause(true);
            const auto before = g.state();
            auto values = defaults;
            values.ballSpeedMultiplier = 2;
            values.chevronAccelerationMultiplier = 3;
            values.chevronSpeedLimit = 600;
            std::string error;
            require(g.applyMechanics(values, error) && error.empty(), "Valid mechanics must apply while paused."); ++checks;
            require(g.state().paused && g.state().phase == before.phase &&
                    g.state().levelSerial == before.levelSerial && g.state().score == before.score &&
                    g.state().highScore == 12000 && g.state().strokes.size() == before.strokes.size() &&
                    g.state().balls[0].position.x == before.balls[0].position.x &&
                    g.state().balls[0].position.y == before.balls[0].position.y &&
                    g.state().tiles[0].code == before.tiles[0].code,
                    "Applying mechanics must preserve pause, board contents, position, score and ink."); ++checks;
            near(g.state().balls[0].velocity.x, 200, 1e-8, "Changing the multiplier must scale a moving ball immediately."); ++checks;
            near(g.state().timeRemaining, before.timeRemaining, 1e-8, "Changing physics must not reset an unchanged time limit."); ++checks;
            require(g.applyMechanics(values, error), "Applying unchanged values must succeed.");
            near(g.state().balls[0].velocity.x, 200, 1e-8, "Repeated Apply must not multiply the velocity again.");
            near(g.state().timeRemaining, before.timeRemaining, 1e-8, "Repeated Apply must not restart the countdown."); ++checks;
            values.timeLimitSeconds = 77;
            require(g.applyMechanics(values, error) && g.state().timeRemaining == 77 && !g.state().unlimitedTime,
                    "Changing the time limit must reset remaining time to the new limit."); ++checks;
            values.timeLimitSeconds = 0;
            require(g.applyMechanics(values, error) && g.state().unlimitedTime && g.state().timeRemaining == 0,
                    "A zero time limit must explicitly enable unlimited time."); ++checks;
            values.minBallSpeed = values.maxBallSpeed = 250;
            require(g.applyMechanics(values, error), "A new initial-speed range must apply.");
            near(g.state().balls[0].velocity.x, 200, 1e-8,
                    "Changing the initial-speed range alone must not reset current ball velocities."); ++checks;
            g.restartLevel();
            require(g.mechanics() == values && g.state().levelId == l.id &&
                    g.state().score == 0 && g.state().spareBlocks == 0 && g.state().highScore == 12000 &&
                    g.state().unlimitedTime && g.state().strokes.empty(),
                    "Restart must retain overrides and high score while resetting the current board."); ++checks;
            near(g.state().balls[0].position.x, 80, 1e-8, "Restart must restore preplaced-ball positions.");
            near(g.state().balls[0].velocity.x, 500, 1e-8,
                    "Restart must spawn preplaced balls with the configured initial range and multiplier."); ++checks;
            require(g.levels()[0].timeSeconds == 120 && g.levels()[0].minBallSpeed == 100 &&
                    g.levels()[0].spawnIntervalSeconds == 7,
                    "Runtime settings must never modify original level records."); ++checks;
            require(g.applyMechanics(g.defaultMechanics(), error) && g.mechanics() == defaults &&
                    g.state().timeRemaining == 120 && !g.state().unlimitedTime,
                    "Applying original defaults must restore all current-board settings."); ++checks;
        }
        {
            auto l = emptyLevel(); l.queue = {1, 2, 3}; l.spawnIntervalSeconds = 5;
            tile(l, 8, 8, 0x1000);
            auto g = gameFor(l); g.tick(0.9);
            auto values = g.mechanics();
            values.launchIntervalSeconds = 2;
            values.minBallSpeed = values.maxBallSpeed = 150;
            values.ballSpeedMultiplier = 2;
            const double initialWait = g.state().nextLaunch;
            std::string error;
            require(g.applyMechanics(values, error) && g.state().nextLaunch == initialWait,
                    "Editing the launch interval must preserve the original first-launch countdown."); ++checks;
            g.tick(3.11);
            require(g.state().balls.size() == 1 && g.state().queue.size() == 2,
                    "The first queued ball must still launch after four seconds."); ++checks;
            near(std::hypot(g.state().balls[0].velocity.x, g.state().balls[0].velocity.y), 300, 1e-8,
                    "A future queued ball must use the configured initial speed and multiplier."); ++checks;
            require(g.state().nextLaunch > 1.9 && g.state().nextLaunch <= 2,
                    "Subsequent launches must use the configured interval."); ++checks;
            values.launchIntervalSeconds = 1;
            require(g.applyMechanics(values, error) && g.state().nextLaunch == 1,
                    "Editing an active launch interval must reset the pending interval."); ++checks;
            g.tick(1.01);
            require(g.state().balls.size() == 2 && g.state().queue.size() == 1,
                    "The pending queued ball must honor the new launch interval."); ++checks;
        }
        for (int direction = 0; direction < 4; ++direction) {
            auto l = emptyLevel(); ball(l, 2, 5, 1, 8);
            tile(l, 3, 5, static_cast<unsigned>(0x8000 | (direction << 4)));
            auto g = gameFor(l);
            auto values = g.mechanics(); values.chevronAccelerationMultiplier = 2;
            std::string error;
            require(g.applyMechanics(values, error), "Valid chevron boost must apply.");
            advanceUntil(g, [&] {
                return std::abs(g.state().balls[0].velocity.x - 100) > 1e-8 ||
                       std::abs(g.state().balls[0].velocity.y) > 1e-8;
            }, 0.3);
            const auto velocity = g.state().balls[0].velocity;
            const double expectedX = direction == 2 ? 94 : direction == 3 ? 106 : 100;
            const double expectedY = direction == 0 ? -6 : direction == 1 ? 6 : 0;
            near(velocity.x, expectedX, 1e-8, "The configured chevron boost must affect the correct horizontal direction.");
            near(velocity.y, expectedY, 1e-8, "The configured chevron boost must affect the correct vertical direction.");
            ++checks;
        }
        {
            auto l = emptyLevel(); ball(l, 2, 5, 1, 8); tile(l, 3, 5, 0x8030);
            auto g = gameFor(l);
            auto values = g.mechanics(); values.chevronAccelerationMultiplier = 0;
            std::string error;
            require(g.applyMechanics(values, error), "Zero chevron boost must be supported.");
            g.tick(0.25);
            near(g.state().balls[0].velocity.x, 100, 1e-8,
                    "Zero boost must remove the acceleration from a chevron tile."); ++checks;
            g.restartLevel(); values.chevronAccelerationMultiplier = 20; values.chevronSpeedLimit = 105;
            require(g.applyMechanics(values, error), "A configured chevron speed limit must apply.");
            g.tick(0.2);
            near(g.state().balls[0].velocity.x, 105, 1e-8,
                    "A chevron must obey its configured speed cap rather than the old fixed cap."); ++checks;
        }
        {
            auto first = emptyLevel(); first.id = 100; ball(first, 2, 3, 1, 8);
            auto second = first; second.id = 101; second.timeSeconds = 35;
            second.minBallSpeed = second.maxBallSpeed = 220;
            Game g({first, second}); g.loadLevel(0);
            auto values = g.mechanics(); values.timeLimitSeconds = 77;
            values.ballSpeedMultiplier = 2; values.chevronAccelerationMultiplier = 3;
            std::string error;
            require(g.applyMechanics(values, error), "Current-board overrides must apply.");
            g.loadLevel(1);
            require(g.mechanics() == g.defaultMechanics() && g.mechanics().timeLimitSeconds == 35 &&
                    g.mechanics().minBallSpeed == 220 && g.mechanics().ballSpeedMultiplier == 1 &&
                    g.mechanics().chevronAccelerationMultiplier == 1,
                    "Loading another board must discard the previous board's overrides."); ++checks;
            values = g.mechanics(); values.chevronAccelerationMultiplier = 2;
            require(g.applyMechanics(values, error), "A second board must accept independent overrides.");
            g.newGame(0, 12345);
            require(g.mechanics() == g.defaultMechanics(), "New Game must restore original mechanics."); ++checks;
            auto empty = emptyLevel(); empty.timeSeconds = 0;
            auto automatic = gameFor(empty);
            values = automatic.mechanics(); values.chevronSpeedLimit = 999;
            require(automatic.applyMechanics(values, error), "An empty board must accept mechanics.");
            const auto serial = automatic.state().levelSerial;
            advanceUntil(automatic, [&] { return automatic.state().levelSerial != serial; }, 3);
            require(automatic.mechanics() == automatic.defaultMechanics(),
                    "Automatic board progression must restore original mechanics too."); ++checks;
        }
        {
            auto l = emptyLevel(); ball(l, 2, 3, 1, 8);
            auto g = gameFor(l); g.tick(0.25); g.pause(true);
            const auto original = g.mechanics();
            const auto before = g.state();
            std::vector<BoardMechanics> invalid;
            auto add = [&](const std::function<void(BoardMechanics&)>& change) {
                auto candidate = original; change(candidate); invalid.push_back(candidate);
            };
            add([](auto& v) { v.timeLimitSeconds = -1; });
            add([](auto& v) { v.timeLimitSeconds = 1000; });
            add([](auto& v) { v.launchIntervalSeconds = -1; });
            add([](auto& v) { v.launchIntervalSeconds = 61; });
            add([](auto& v) { v.minBallSpeed = 0; });
            add([](auto& v) { v.maxBallSpeed = 1001; });
            add([](auto& v) { v.minBallSpeed = 101; v.maxBallSpeed = 100; });
            add([](auto& v) { v.ballSpeedMultiplier = 0; });
            add([](auto& v) { v.ballSpeedMultiplier = 4.1; });
            add([](auto& v) { v.ballSpeedMultiplier = std::numeric_limits<double>::quiet_NaN(); });
            add([](auto& v) { v.maxBallSpeed = 1000; v.ballSpeedMultiplier = 4; });
            add([](auto& v) { v.chevronAccelerationMultiplier = -1; });
            add([](auto& v) { v.chevronAccelerationMultiplier = 21; });
            add([](auto& v) { v.chevronAccelerationMultiplier = std::numeric_limits<double>::infinity(); });
            add([](auto& v) { v.chevronSpeedLimit = 0; });
            add([](auto& v) { v.chevronSpeedLimit = 2001; });
            add([](auto& v) { v.chevronSpeedLimit = std::numeric_limits<double>::quiet_NaN(); });
            for (const auto& candidate : invalid) {
                std::string error;
                require(!g.applyMechanics(candidate, error) && !error.empty() &&
                        g.mechanics() == original && g.state().timeRemaining == before.timeRemaining &&
                        g.state().balls[0].velocity.x == before.balls[0].velocity.x &&
                        g.state().balls[0].position.x == before.balls[0].position.x && g.state().paused,
                        "Invalid settings must be rejected atomically with a helpful error.");
                ++checks;
            }
        }
        {
            auto l = emptyLevel(); ball(l, 2, 5, 1, 8); tile(l, 3, 5, 0x8030);
            auto g = gameFor(l);
            auto values = g.mechanics(); values.ballSpeedMultiplier = 0.1;
            values.chevronAccelerationMultiplier = 20; values.chevronSpeedLimit = 2000;
            std::string error;
            require(g.applyMechanics(values, error), "A slow ball and strong chevron are valid independently.");
            advanceUntil(g, [&] { return g.state().balls[0].velocity.x > 100; }, 2);
            const double oldSpeed = g.state().balls[0].velocity.x;
            values.ballSpeedMultiplier = 4;
            require(!g.applyMechanics(values, error) && !error.empty() &&
                    g.mechanics().ballSpeedMultiplier == 0.1 && g.state().balls[0].velocity.x == oldSpeed,
                    "A multiplier that would excessively accelerate an already boosted ball must be rejected atomically."); ++checks;
        }
        {
            auto l = emptyLevel(); ball(l, 2, 3, 1, 8);
            auto g = gameFor(l);
            auto values = g.mechanics(); values.timeLimitSeconds = 1;
            values.chevronAccelerationMultiplier = 2;
            std::string error;
            require(g.applyMechanics(values, error), "A shortened board time must apply.");
            g.tick(1.1);
            require(g.state().phase == Phase::GameOver, "An edited time limit must actually expire."); ++checks;
            values.timeLimitSeconds = 10;
            require(g.applyMechanics(values, error) && g.state().phase == Phase::GameOver,
                    "Editing settings must not silently revive a lost board."); ++checks;
            g.restartLevel();
            require(g.state().phase == Phase::Playing && g.mechanics() == values && g.state().timeRemaining == 10,
                    "Restart after a loss must retain current-board settings and restart the edited timer."); ++checks;
        }
        // Asymmetric durations expose swapped on/off fields. Exercise both
        // initial states and multiple cycles, with a ball away from the wall.
        for (const unsigned initialFrame : {0u, 4u}) {
            auto level = emptyLevel();
            ball(level, 2, 2, 0, 0);
            tile(level, 12, 12, 0x6390u | initialFrame);
            auto g = gameFor(level);
            const auto& wall = g.state().tiles[12 * BoardColumns + 12];
            if (initialFrame) {
                g.tick(1.9);
                require(wall.gateState == 2, "Initially raised timed wall must stay on for two seconds."); ++checks;
                advanceUntil(g, [&] { return wall.gateState == 0; }, 0.2);
            }
            for (int cycle = 0; cycle < 3; ++cycle) {
                g.tick(4.9);
                require(wall.gateState == 0 && wall.animationFrame == 0,
                    "Timed wall must remain off for five seconds, not two."); ++checks;
                advanceUntil(g, [&] { return wall.gateState == 2; }, 0.2);
                g.tick(1.9);
                require(wall.gateState == 2 && wall.animationFrame == 4,
                    "Timed wall must remain on for two seconds."); ++checks;
                advanceUntil(g, [&] { return wall.gateState == 0; }, 0.2);
            }
        }
        {
            auto level=emptyLevel();ball(level,2,2,0,0);auto g=gameFor(level);
            g.beginStroke({200,200});g.extendStroke({203,204});g.extendStroke({203,204});g.endStroke();
            auto s=g.statistics();near(s.run.inkLength,5,1e-12,"Ink length must measure accepted segments once.");++checks;
            require(s.run.strokes==1 && s.game.strokes==1,"A drawn stroke must count once.");++checks;
            g.clearInk();near(g.statistics().run.inkLength,5,1e-12,"Clearing ink must retain drawn length.");++checks;
            g.tick(0.5);g.pause(true);g.tick(4);
            near(g.statistics().run.activeSeconds,0.5,1e-9,"Statistics must exclude paused time.");++checks;
            g.restartLevel();s=g.statistics();
            require(s.hasPreviousRun&&s.hasPreviousGame&&s.previousRun.strokes==1&&s.previousGame.strokes==1,
                "Restart must retain the completed run and game snapshots.");++checks;
            require(s.run.strokes==0&&s.game.strokes==0&&s.game.activeSeconds==0,
                "Restart must begin fresh counters and time.");++checks;
            g.beginStroke({200,200});g.extendStroke({203,204});g.loadLevel(0,false);s=g.statistics();
            require(s.run.strokes==0&&s.game.strokes==1&&s.previousRun.strokes==1,
                "A board transition must reset the run and retain game totals.");++checks;
            g.loadLevel(0);s=g.statistics();
            require(s.game.strokes==0&&s.previousGame.strokes==1,"Play Selected Board must start a fresh game.");++checks;
            level.timeSeconds=1;auto timed=gameFor(level);
            timed.beginStroke({200,200});timed.extendStroke({203,204});timed.endStroke();timed.tick(1.1);
            const auto ended=timed.statistics();
            require(timed.state().phase==Phase::GameOver&&ended.run.activeSeconds>0,
                "A lost run must retain its active duration.");++checks;
            timed.restartLevel();const auto restarted=timed.statistics();
            near(restarted.previousGame.inkLength,5,1e-12,"Loss restart must preserve previous game ink length.");++checks;
            near(restarted.previousGame.activeSeconds,ended.game.activeSeconds,1e-12,
                "Loss restart must preserve previous game duration.");++checks;
            require(restarted.game.activeSeconds==0&&restarted.run.inkLength==0,
                "Automatic restart must start clean statistics.");++checks;
        }
        {
            auto level=emptyLevel();ball(level,2,2,0,0);auto g=gameFor(level);
            RunStatistics saved;saved.strokes=20;saved.inkLength=100;saved.activeSeconds=12;
            g.seedLifetimeStatistics(saved);g.seedLifetimeStatistics(saved);
            g.beginStroke({200,200});g.extendStroke({203,204});g.endStroke();g.tick(0.25);
            auto totals=g.statistics().lifetime;
            require(totals.strokes==21,"Lifetime seed must apply only once and new strokes once.");++checks;
            near(totals.inkLength,105,1e-12,"Lifetime ink must include persisted totals.");++checks;
            near(totals.activeSeconds,12.25,1e-9,"Lifetime time must add active play once.");++checks;
            g.pause(true);g.tick(4);g.pause(false);g.restartLevel();g.loadLevel(0);g.newGame(0,12);
            totals=g.statistics().lifetime;
            require(totals.strokes==21,"New games and restarts must retain lifetime counters.");++checks;
            near(totals.activeSeconds,12.25,1e-9,"Pause and transitions must not double-count lifetime time.");++checks;
        }
        {
            auto level=emptyLevel();auto g=gameFor(level);g.tick(0.016);
            const auto completed=g.statistics();
            require(g.state().phase==Phase::LevelComplete&&completed.run.boardsCleared==1,
                "Empty board must produce one completion.");++checks;
            const double remaining=g.state().timeRemaining;
            near(completed.run.completionRemainingSeconds,remaining,1e-12,"Completion must capture original time remaining.");++checks;
            near(completed.run.completionActiveSeconds,completed.run.activeSeconds,1e-12,"Completion must capture active run duration.");++checks;
            g.tick(0.5);const auto celebration=g.statistics();
            require(g.state().timeRemaining<remaining,"Completion celebration must consume the countdown.");++checks;
            near(celebration.run.completionRemainingSeconds,remaining,1e-12,"Celebration must not consume captured completion time.");++checks;
            near(celebration.lifetime.activeSeconds,completed.lifetime.activeSeconds,1e-12,"Celebration must not count as active lifetime time.");++checks;
            g.loadLevel(0,false);const auto next=g.statistics();
            require(next.previousRun.boardsCleared==1&&next.game.boardsCleared==1&&next.lifetime.boardsCleared==1,
                "Completion snapshots and totals must survive a board transition without duplicates.");++checks;
            near(next.game.completionRemainingSeconds,remaining,1e-12,"Game completion time must survive transitions.");++checks;
            g.tick(0.016);require(g.statistics().game.boardsCleared==2&&g.statistics().lifetime.boardsCleared==2,
                "Each cleared board must count once across the game and lifetime.");++checks;
            level.timeSeconds=0;auto unlimited=gameFor(level);unlimited.tick(0.016);
            require(unlimited.statistics().run.boardsCleared==1&&unlimited.statistics().run.timedBoardsCleared==0,
                "Unlimited boards must not invent a remaining-time completion value.");++checks;
        }
        {
            auto l=emptyLevel();l.timeSeconds=11;ball(l,2,2,0,0);auto g=gameFor(l);
            g.tick(0.332);require(g.state().timerVisible,"Low-time digits must remain visible before 333 ms.");++checks;
            g.tick(0.001);require(!g.state().timerVisible,"Displayed ten seconds must flash off after 333 ms.");++checks;
            g.pause(true);g.tick(2);require(!g.state().timerVisible,"Pausing must freeze the warning phase.");++checks;
            g.pause(false);g.tick(0.333);require(g.state().timerVisible,"Warning must flash back on after another 333 ms.");++checks;
            g.tick(0.333);g.restartLevel();require(g.state().timerVisible,"Restart must reset warning visibility.");++checks;
            auto values=g.mechanics();values.timeLimitSeconds=20;std::string error;
            require(g.applyMechanics(values,error)&&g.state().timerVisible,"Editing the countdown must reset the flash.");++checks;
            g.tick(1);require(g.state().timerVisible,"A displayed countdown above ten must not flash.");++checks;
            values.timeLimitSeconds=0;require(g.applyMechanics(values,error),"Unlimited time must apply.");++checks;
            g.tick(2);require(g.state().timerVisible,"Unlimited time must not flash.");++checks;
            l.timeSeconds=1;auto finalSecond=gameFor(l);finalSecond.tick(0.5);
            require(finalSecond.state().timerVisible,"The original final 000 display stays visible.");++checks;
            finalSecond.tick(1);require(finalSecond.state().phase==Phase::GameOver&&finalSecond.state().timerVisible,
                "Expiration must retain visible zero digits.");++checks;
            l.timeSeconds=11;l.queue={1};tile(l,3,3,0x1000);auto queued=gameFor(l);queued.tick(0.5);
            require(!queued.state().timerStarted&&queued.state().timerVisible&&queued.state().timeRemaining==11,
                "The warning must wait for the countdown to start.");++checks;
        }
        {
            auto l=emptyLevel();l.queue={1,2,0};ball(l,2,2,3,0);
            hole(l,4,4,1);hole(l,7,4,2);hole(l,10,4,3);
            tile(l,4,9,0x4000);tile(l,5,9,0x4002);tile(l,6,9,0x4004);
            require(MaximumBoardPoints(l)==1420,"Scoring potential must include queued/preplaced balls and hittable squares.");++checks;
            tile(l,8,9,0x3004);
            require(MaximumBoardPoints(l)==1430,"Recoloring must unlock matching breakable-square points.");++checks;
            hole(l,10,10,4);
            require(MaximumBoardPoints(l)==6430,"All balls, including gray ones, may gain gold value through recoloring.");++checks;
            auto hidden=emptyLevel();ball(hidden,2,2,1,0);hole(hidden,4,4,1);hole(hidden,8,8,4);
            tile(hidden,5,9,0x4001,0x3004);
            require(MaximumBoardPoints(hidden)==1610,"Maximum must include an unlocked hidden recoloring wall.");++checks;
            tile(hidden,5,9,0x4004,0x3004);
            require(MaximumBoardPoints(hidden)==200,"A color locked behind its own unbreakable wall must not be assumed available.");++checks;
            tile(hidden,5,9,0x4000,0x4001);
            require(MaximumBoardPoints(hidden)==220,"Revealed breakable squares must each contribute ten points.");++checks;
            auto noBalls=emptyLevel();tile(noBalls,2,2,0x4000);
            require(MaximumBoardPoints(noBalls)==0,"A board without balls cannot obtain square-hit points.");++checks;
        }
        {
            auto l=emptyLevel();l.id=73;l.timeSeconds=120;
            ball(l,5,4,1,14);hole(l,5,5,1);tile(l,10,10,0x4000);
            auto g=gameFor(l);
            require(g.completedBoards().empty(),"An unfinished board must not appear in the table.");++checks;
            g.beginStroke({300,400});g.extendStroke({303,404});g.endStroke();g.clearInk();
            advanceUntil(g,[&]{return g.state().phase==Phase::LevelComplete;});
            require(g.completedBoards().size()==1,"Completing a board must add exactly one record.");++checks;
            const auto first=g.completedBoards().front();
            near(first.inkLength,5,1e-12,"Completed ink must include all drawn ink, even after clearing.");++checks;
            require(first.boardId==73&&first.timeAvailable==120&&first.points==200&&first.maximumPoints==210,
                "The completion row must preserve ID, limit and non-bonus scoring potential.");++checks;
            near(first.timeUsed,g.statistics().run.activeSeconds,1e-12,"Time used must match active completion time.");++checks;
            g.tick(0.4);
            near(g.completedBoards()[0].inkLength,5,1e-12,"The completed board's ink must stay fixed through the bonus animation.");++checks;
            require(g.state().score>200&&g.completedBoards().size()==1&&g.completedBoards()[0].points==200,
                "Time bonus must neither modify nor duplicate the completed row.");++checks;
            g.loadLevel(0,false);g.beginStroke({300,400});g.extendStroke({312,400});g.endStroke();g.clearInk();
            advanceUntil(g,[&]{return g.state().phase==Phase::LevelComplete;});
            near(g.completedBoards()[1].inkLength,12,1e-12,"Board ink must exclude ink drawn on earlier boards.");++checks;
            near(g.completedBoards()[0].inkLength,5,1e-12,"Later boards must not modify earlier ink records.");++checks;
            require(g.completedBoards().size()==2&&g.completedBoards()[1].points==200,
                "Later board rows must exclude all prior board points and bonuses.");++checks;
            g.restartLevel();require(g.completedBoards().empty(),"Restart must clear the current-run table.");++checks;
            advanceUntil(g,[&]{return g.state().phase==Phase::LevelComplete;});g.newGame(0,42);
            require(g.completedBoards().empty(),"New Game must clear the current-run table.");++checks;
            auto timed=emptyLevel();timed.timeSeconds=1;ball(timed,2,2,0,0);auto lost=gameFor(timed);lost.tick(1.1);
            require(lost.completedBoards().empty(),"Failed boards must not appear as completed.");++checks;
            auto unlimited=l;unlimited.timeSeconds=0;auto u=gameFor(unlimited);
            advanceUntil(u,[&]{return u.state().phase==Phase::LevelComplete;});
            require(u.completedBoards()[0].timeAvailable==0,"Unlimited boards must preserve their unlimited limit.");++checks;
        }
        {
            auto l=emptyLevel();l.minBallSpeed=l.maxBallSpeed=300;
            ball(l,1,3,1,8);tile(l,3,3,0x4000);hole(l,8,8,1);
            auto g=gameFor(l);
            advanceUntil(g,[&]{return g.state().score==10;});
            // Place the deflected ball at the drain center to isolate score
            // bookkeeping from the unrelated route back across the board.
            auto& drained=const_cast<Ball&>(g.state().balls.front());
            drained.position={288,288};drained.velocity={};
            advanceUntil(g,[&]{return g.state().phase==Phase::LevelComplete;});
            require(g.completedBoards()[0].points==210&&g.completedBoards()[0].maximumPoints==210,
                "A completed row must include both the ten-point square hit and the drain score.");++checks;
        }
        std::cout << "PASS: " << checks << " checks, including all 551 original boards and gameplay regressions.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL after " << checks << " checks: " << e.what() << '\n';
        return 1;
    }
}
