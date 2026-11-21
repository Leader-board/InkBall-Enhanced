#include "Game.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace inkball {
namespace {
constexpr double Pi = 3.1415926535897932384626433832795;
constexpr std::array<unsigned, 5> DrainPoints{{0, 200, 400, 800, 1600}};
struct Rect { double left, top, right, bottom; };
Vec2 operator+(Vec2 a, Vec2 b) { return {a.x + b.x, a.y + b.y}; }
Vec2 operator-(Vec2 a, Vec2 b) { return {a.x - b.x, a.y - b.y}; }
Vec2 operator*(Vec2 a, double s) { return {a.x * s, a.y * s}; }
double dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
double lengthSquared(Vec2 a) { return dot(a, a); }
bool validPoint(Vec2 p) { return std::isfinite(p.x) && std::isfinite(p.y); }
bool inside(Vec2 p, Rect r) {
    return p.x >= r.left && p.x < r.right && p.y >= r.top && p.y < r.bottom;
}
Vec2 clipped(Vec2 p) {
    return {std::clamp(p.x, 0.0, static_cast<double>(BoardPixels - 1)),
            std::clamp(p.y, 0.0, static_cast<double>(BoardPixels - 1))};
}
Rect tileRect(int index) {
    const double x = (index % BoardColumns) * TileSize;
    const double y = (index / BoardColumns) * TileSize;
    return {x, y, x + TileSize, y + TileSize};
}
int tileAt(Vec2 p) {
    if (p.x < 0 || p.y < 0 || p.x >= BoardPixels || p.y >= BoardPixels) return -1;
    return static_cast<int>(p.y) / TileSize * BoardColumns + static_cast<int>(p.x) / TileSize;
}
Vec2 tileCenter(int index) {
    const auto r = tileRect(index);
    return {(r.left + r.right) / 2, (r.top + r.bottom) / 2};
}
Vec2 integerCenter(const Ball& ball) {
    return {static_cast<double>(static_cast<int>(ball.position.x)),
            static_cast<double>(static_cast<int>(ball.position.y))};
}
std::array<Vec2, 4> boundingCorners(const Ball& ball) {
    const double x = static_cast<int>(ball.position.x - ball.radius);
    const double y = static_cast<int>(ball.position.y - ball.radius);
    const double size = ball.radius * 2;
    return {{{x, y}, {x + size, y}, {x, y + size}, {x + size, y + size}}};
}
std::array<Vec2, 32> perimeter(const Ball& ball) {
    std::array<Vec2, 32> points{};
    // The original stores integer perimeter offsets at 11.25 degree intervals.
    for (int i = 0; i < 32; ++i) {
        const double angle = Pi + i * Pi / 16;
        points[static_cast<std::size_t>(i)] = {
            static_cast<double>(static_cast<int>(std::cos(angle) * ball.radius)),
            static_cast<double>(static_cast<int>(std::sin(angle) * ball.radius))};
    }
    points[0] = {-ball.radius, 0};
    points[8] = {0, -ball.radius};
    points[16] = {ball.radius, 0};
    points[24] = {0, ball.radius};
    return points;
}
bool perimeterIntersects(const Ball& ball, Rect rect) {
    const auto center = integerCenter(ball);
    for (const auto p : perimeter(ball)) if (inside(center + p, rect)) return true;
    return false;
}
bool constrainToBounds(Ball& b) {
    bool collision = false;
    if (b.position.x < b.radius) { b.position.x = b.radius; b.velocity.x = -b.velocity.x; collision = true; }
    else if (b.position.x > BoardPixels - b.radius) {
        b.position.x = BoardPixels - b.radius - 1; b.velocity.x = -b.velocity.x; collision = true;
    }
    if (b.position.y < b.radius) { b.position.y = b.radius; b.velocity.y = -b.velocity.y; collision = true; }
    else if (b.position.y > BoardPixels - b.radius) {
        b.position.y = BoardPixels - b.radius - 1; b.velocity.y = -b.velocity.y; collision = true;
    }
    return collision;
}
double distanceToSegmentSquared(Vec2 point, Vec2 a, Vec2 b) {
    const Vec2 segment = b - a;
    const double length = lengthSquared(segment);
    if (length <= 1e-18) return lengthSquared(point - a);
    const double t = std::clamp(dot(point - a, segment) / length, 0.0, 1.0);
    return lengthSquared(point - (a + segment * t));
}
bool strokeTouches(const Stroke& stroke, Vec2 center, double radius) {
    if (stroke.points.empty()) return false;
    const double rr = radius * radius;
    if (stroke.points.size() == 1) return lengthSquared(center - stroke.points[0]) <= rr;
    for (std::size_t i = 1; i < stroke.points.size(); ++i)
        if (distanceToSegmentSquared(center, stroke.points[i - 1], stroke.points[i]) <= rr) return true;
    return false;
}
template<class Predicate> bool reflectFromSamples(Ball& ball, Predicate hit) {
    const auto points = perimeter(ball);
    int mostForward = 0;
    double largest = dot(points[0], ball.velocity);
    for (int i = 1; i < 32; ++i) {
        const double projection = dot(points[static_cast<std::size_t>(i)], ball.velocity);
        if (projection > largest) { largest = projection; mostForward = i; }
    }
    const int start = (mostForward + 24) % 32;
    int first = -1, last = -1;
    const Vec2 center = integerCenter(ball);
    for (int j = 0; j <= 16; ++j) {
        const int index = (start + j) % 32;
        if (hit(center + points[static_cast<std::size_t>(index)])) {
            if (first < 0) first = index;
            last = index;
        }
    }
    if (first < 0) return false;
    int normalIndex = (first + last) / 2;
    if (last < first) normalIndex = (normalIndex + 16) % 32;
    const Vec2 normal = points[static_cast<std::size_t>(normalIndex)];
    const double nn = lengthSquared(normal);
    if (nn <= 1e-12) return false;
    ball.velocity = ball.velocity - normal * (2 * dot(ball.velocity, normal) / nn);
    return true;
}

bool deflectsAt(const Tile& tile, int index, const Ball& ball, Vec2 point) {
    const int kind = tile.kind();
    if (kind == 3 || kind == 4) return true;
    if (kind == 6 || kind == 7) return tile.animationFrame > 0;
    auto r = tileRect(index);
    if (kind == 10) {
        if (tile.color() == 0 || tile.color() == ball.color) return false;
        if (tile.param1() == 0) { r.top += 10; r.bottom -= 10; }
        else { r.left += 10; r.right -= 10; }
        return inside(point, r);
    }
    if (kind != 5) return false;
    const auto center = integerCenter(ball);
    bool reverse = false;
    switch (tile.param1()) {
    case 0: r.bottom -= 20; reverse = center.y < (r.top + r.bottom) / 2 && ball.velocity.y > 0; break;
    case 1: r.top += 20; reverse = center.y > (r.top + r.bottom) / 2 && ball.velocity.y < 0; break;
    case 2: r.right -= 20; reverse = center.x < (r.left + r.right) / 2 && ball.velocity.x > 0; break;
    case 3: r.left += 20; reverse = center.x > (r.left + r.right) / 2 && ball.velocity.x < 0; break;
    default: return false;
    }
    return reverse || (tile.color() != 0 && tile.color() != ball.color && inside(point, r));
}
}

int TileSprite(const Tile& t) {
    switch (t.kind()) {
    case 0: case 9: return 0;
    case 1: return 1;
    case 2: return 17 + 4 * t.color() + t.param1();
    case 3: return 37 + t.color();
    case 4: return 42 + t.color();
    case 5: return 47 + 5 * t.param1() + t.color();
    case 6: return 77 + 5 * t.animationFrame;
    case 7: return 77 + 5 * t.animationFrame + t.color();
    case 8: return 102 + t.param1();
    case 10: return 67 + 5 * t.param1() + t.color();
    default: return 0;
    }
}
int BallSprite(const Ball& b) { return (b.nearHole ? 7 : 2) + b.color; }

std::uint64_t MaximumBoardPoints(const Level& level) {
    std::array<std::uint64_t,5> balls{};
    for(int color:level.queue) ++balls[static_cast<std::size_t>(color)];
    for(const auto& tile:level.tiles)
        if((tile.code>>12)==9) ++balls[tile.code&15];
    unsigned initial=0;
    for(unsigned c=0;c<5;++c) if(balls[c]) initial|=1u<<c;
    if(!initial)return 0;
    unsigned paint=0,drains=0;
    const auto canBreak=[&](unsigned color){return color==0 || ((initial|paint)&(1u<<color));};
    const auto inspect=[&](std::uint16_t code){
        const unsigned kind=code>>12,color=code&15;
        if(kind==3&&color)paint|=1u<<color;
        if(kind==2)drains|=1u<<color;
    };
    // A breakable square can reveal another recoloring wall or drain.
    // Resolve that dependency without simulating any trajectories.
    unsigned previous;
    do {
        previous=paint;
        for(const auto& tile:level.tiles){
            inspect(tile.code);
            if((tile.code>>12)==4 && canBreak(tile.code&15))inspect(tile.underlay);
        }
    } while(paint!=previous);
    std::uint64_t points=0;
    for(unsigned c=0;c<5;++c){
        const unsigned choices=((1u<<c)|paint)&drains;
        unsigned best=0;
        for(unsigned target=0;target<5;++target)
            if(choices&(1u<<target))best=std::max(best,DrainPoints[target]);
        points+=balls[c]*best;
    }
    for(const auto& tile:level.tiles){
        if((tile.code>>12)!=4 || !canBreak(tile.code&15))continue;
        points+=10;
        if((tile.underlay>>12)==4 && canBreak(tile.underlay&15))points+=10;
    }
    return points;
}

Game::Game(std::vector<Level> levels) : levels_(std::move(levels)) {
    if (levels_.empty()) throw std::invalid_argument("InkBall requires at least one level.");
    state_.balls.reserve(64);
    newGame();
}
int Game::randomInt(int bound) {
    if (bound <= 0) return 0;
    // Match the MSVCRT 15-bit rand sequence used by the original executable.
    // Unsigned arithmetic intentionally wraps modulo 2^32.
    randomState_ = randomState_ * 214013u + 2531011u;
    return static_cast<int>(((randomState_ >> 16) & 0x7fffu) % static_cast<unsigned>(bound));
}
void Game::newGame(int difficulty, std::uint32_t seed) {
    startStatisticsGame();
    difficulty = std::clamp(difficulty, 0, 4);
    const bool firstGame = state_.levelSerial == 0;
    const bool resetHistory = firstGame || seed != 0 || difficulty != state_.difficulty;
    if (firstGame || seed != 0) {
        if (!seed) seed = static_cast<std::uint32_t>(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()));
        randomState_ = seed;
    }
    // F2 in the original retains board history and the running random sequence.
    // Difficulty changes clear history; an explicit seed also starts a replay.
    if (resetHistory) { normalHistory_.clear(); bonusHistory_.clear(); }
    state_.difficulty = difficulty;
    completedLevels_ = 0;
    state_.score = 0;
    state_.spareBlocks = 0;
    lastBlockAwardScore_ = 0;
    initializeLevel(chooseLevel(false));
}
std::size_t Game::chooseLevel(bool bonus) {
    std::vector<std::size_t> pool;
    for (std::size_t i = 0; i < levels_.size(); ++i)
        if (levels_[i].difficulty == state_.difficulty && levels_[i].bonus == bonus) pool.push_back(i);
    if (pool.empty()) {
        for (std::size_t i = 0; i < levels_.size(); ++i)
            if (levels_[i].difficulty == state_.difficulty) pool.push_back(i);
    }
    if (pool.empty()) pool.push_back(0);
    auto& history = bonus ? bonusHistory_ : normalHistory_;
    const std::size_t historyLimit = std::min<std::size_t>(10, pool.size() - 1);
    while (history.size() > historyLimit) history.pop_back();
    std::size_t selected;
    do selected = pool[static_cast<std::size_t>(randomInt(static_cast<int>(pool.size())))];
    while (std::find(history.begin(), history.end(), static_cast<int>(selected)) != history.end());
    history.push_front(static_cast<int>(selected));
    while (history.size() > 10) history.pop_back();
    return selected;
}
void Game::resetTile(Tile& tile, std::uint16_t code, std::uint16_t underlay) {
    tile = Tile{};
    tile.code = code;
    tile.underlay = underlay;
    if (tile.kind() == 6 || tile.kind() == 7) {
        tile.animationFrame = tile.kind() == 6 ? tile.color() : tile.param1();
        tile.gateState = tile.animationFrame ? 2 : 0;
    }
}
void Game::initializeLevel(std::size_t index, bool preserveMechanics) {
    if (index >= levels_.size()) throw std::out_of_range("InkBall level index is out of range.");
    if (state_.levelSerial && !newStatisticsGame_) {
        const auto current = statistics();
        statistics_.previousRun = current.run;
        statistics_.hasPreviousRun = true;
        priorBoardSeconds_ = current.game.activeSeconds;
    }
    statistics_.run = {};
    newStatisticsGame_ = false;
    currentLevelIndex_ = index;
    boardStartingScore_ = state_.score;
    const Level& level = levels_[index];
    if (!preserveMechanics) mechanics_ = defaultMechanics();
    state_.difficulty = level.difficulty;
    state_.levelId = level.id;
    state_.levelNumber = completedLevels_ + 1;
    state_.isBonus = level.bonus;
    state_.timeRemaining = mechanics_.timeLimitSeconds;
    state_.unlimitedTime = mechanics_.timeLimitSeconds == 0;
    state_.timerStarted = level.queue.empty();
    state_.timerVisible = true;
    timerBlinkAccumulator_ = 0;
    state_.phase = Phase::Playing;
    state_.paused = false;
    state_.message.clear();
    state_.balls.clear();
    state_.strokes.clear();
    state_.queue = level.queue;
    state_.nextLaunch = 4.0;
    state_.launcherBlink = false;
    state_.celebrationElapsed = 0;
    state_.elapsed = 0;
    boardActiveSeconds_ = 0;
    ++state_.levelSerial;
    completionAccumulator_ = completionDelay_ = launchBlinkAccumulator_ = 0;
    activeStroke_ = 0;
    strokeConsumed_ = false;
    nextBallId_ = 1;
    launchers_.clear();
    if (level.randomizeQueue) {
        for (std::size_t i = 0; i < state_.queue.size(); ++i)
            std::swap(state_.queue[i], state_.queue[static_cast<std::size_t>(randomInt(static_cast<int>(state_.queue.size())))]);
    }
    for (int i = 0; i < BoardCellCount; ++i) {
        const auto& source = level.tiles[static_cast<std::size_t>(i)];
        auto& tile = state_.tiles[static_cast<std::size_t>(i)];
        resetTile(tile, source.code, source.underlay);
        if (tile.kind() == 1) launchers_.push_back(i);
        if (tile.kind() == 9) {
            const int encodedAngle = (tile.code >> 4) & 255;
            spawnBall(tile.color(), tileCenter(i), encodedAngle == 255 ? -1.0 : encodedAngle * 11.25);
            resetTile(tile, 0);
        }
    }
}
void Game::loadLevel(std::size_t index, bool resetScore) {
    if (index >= levels_.size()) throw std::out_of_range("InkBall level index is out of range.");
    if (resetScore) {
        startStatisticsGame();
        completedLevels_ = 0;
        state_.score = 0;
        state_.spareBlocks = 0;
        lastBlockAwardScore_ = 0;
    }
    initializeLevel(index);
}
void Game::restartLevel() {
    startStatisticsGame();
    completedLevels_ = 0;
    state_.score = 0;
    state_.spareBlocks = 0;
    lastBlockAwardScore_ = 0;
    // Preplaced balls must be spawned with the retained overrides too; applying
    // the values only after a normal load would leave them at original speeds.
    initializeLevel(currentLevelIndex_, true);
}
BoardMechanics Game::defaultMechanics() const {
    const auto& level = levels_[currentLevelIndex_];
    BoardMechanics result;
    result.timeLimitSeconds = level.timeSeconds;
    result.launchIntervalSeconds = level.spawnIntervalSeconds;
    result.minBallSpeed = level.minBallSpeed;
    result.maxBallSpeed = level.maxBallSpeed;
    return result;
}
bool Game::applyMechanics(const BoardMechanics& values, std::string& error) {
    auto reject = [&](const char* message) { error = message; return false; };
    if (values.timeLimitSeconds < 0 || values.timeLimitSeconds > 999)
        return reject("Time limit must be from 0 to 999 seconds; 0 means unlimited.");
    if (values.launchIntervalSeconds < 0 || values.launchIntervalSeconds > 60)
        return reject("Launch interval must be from 0 to 60 seconds.");
    if (values.minBallSpeed < 1 || values.maxBallSpeed > 1000 ||
        values.minBallSpeed > values.maxBallSpeed)
        return reject("Initial speeds must be from 1 to 1000 px/s, with minimum no greater than maximum.");
    if (!std::isfinite(values.ballSpeedMultiplier) ||
        values.ballSpeedMultiplier < 0.1 || values.ballSpeedMultiplier > 4)
        return reject("Ball speed multiplier must be from 0.1 to 4.");
    if (values.maxBallSpeed * values.ballSpeedMultiplier > 2000)
        return reject("Maximum initial speed times the ball multiplier must not exceed 2000 px/s.");
    if (!std::isfinite(values.chevronAccelerationMultiplier) ||
        values.chevronAccelerationMultiplier < 0 || values.chevronAccelerationMultiplier > 20)
        return reject("Chevron boost must be from 0 to 20 times the original strength.");
    if (!std::isfinite(values.chevronSpeedLimit) ||
        values.chevronSpeedLimit < 1 || values.chevronSpeedLimit > 2000)
        return reject("Chevron speed limit must be from 1 to 2000 px/s per axis.");
    const double ratio = values.ballSpeedMultiplier / mechanics_.ballSpeedMultiplier;
    if (values.ballSpeedMultiplier != mechanics_.ballSpeedMultiplier) {
        for (const auto& ball : state_.balls) {
            if (!ball.active) continue;
            const Vec2 scaled = ball.velocity * ratio;
            if (!validPoint(scaled) || std::abs(scaled.x) > 2000 || std::abs(scaled.y) > 2000)
                return reject("This multiplier would raise a moving ball above 2000 px/s per axis. Use a smaller multiplier.");
        }
    }

    // Do not touch state until every field and every resulting velocity has
    // passed validation. Applying the same configuration is an exact no-op.
    if (values.timeLimitSeconds != mechanics_.timeLimitSeconds) {
        state_.timeRemaining = values.timeLimitSeconds;
        state_.unlimitedTime = values.timeLimitSeconds == 0;
        state_.timerVisible = true;
        timerBlinkAccumulator_ = 0;
    }
    if (values.launchIntervalSeconds != mechanics_.launchIntervalSeconds &&
        state_.timerStarted && !state_.queue.empty()) {
        state_.nextLaunch = values.launchIntervalSeconds;
        state_.launcherBlink = false;
        launchBlinkAccumulator_ = 0;
    }
    if (values.ballSpeedMultiplier != mechanics_.ballSpeedMultiplier)
        for (auto& ball : state_.balls)
            if (ball.active) ball.velocity = ball.velocity * ratio;
    mechanics_ = values;
    error.clear();
    return true;
}
void Game::setHighScore(std::uint64_t value) { state_.highScore = std::max(value, state_.score); }
void Game::pause(bool value) { state_.paused = value; if (value) endStroke(); }

Vec2 Game::initialVelocity(double angleDegrees) {
    const double speed = (mechanics_.minBallSpeed +
        randomInt(mechanics_.maxBallSpeed - mechanics_.minBallSpeed)) * mechanics_.ballSpeedMultiplier;
    if (angleDegrees >= 0) {
        const double angle = angleDegrees * Pi / 180;
        return {speed * std::sin(angle), -speed * std::cos(angle)};
    }
    double x = randomInt(100) / 100.0 * speed;
    double y = std::sqrt(std::max(0.0, speed * speed - x * x));
    if (randomInt(2)) x = -x;
    if (randomInt(2)) y = -y;
    return {x, y};
}
void Game::spawnBall(int color, Vec2 position, double angleDegrees) {
    if (state_.balls.size() >= 64) return;
    Ball b;
    b.id = nextBallId_++;
    b.color = std::clamp(color, 0, 4);
    b.position = position;
    b.velocity = initialVelocity(angleDegrees);
    state_.balls.push_back(b);
}
bool Game::ballOverlapsTile(int column, int row) const {
    if (column < 0 || row < 0 || column >= BoardColumns || row >= BoardRows) return false;
    const auto r = tileRect(row * BoardColumns + column);
    for (const auto& b : state_.balls) {
        if (!b.active) continue;
        // The original uses bounding rectangles here, for launch and gate safety.
        if (b.position.x + b.radius > r.left && b.position.x - b.radius < r.right &&
            b.position.y + b.radius > r.top && b.position.y - b.radius < r.bottom) return true;
    }
    return false;
}
void Game::updateLaunchers(double dt) {
    if (state_.queue.empty()) { state_.timerStarted = true; return; }
    state_.nextLaunch -= dt;
    if (state_.nextLaunch <= 0.8) {
        launchBlinkAccumulator_ += dt;
        if (launchBlinkAccumulator_ >= 0.1) {
            launchBlinkAccumulator_ = 0;
            state_.launcherBlink = !state_.launcherBlink;
        }
    }
    if (state_.nextLaunch > 0 || launchers_.empty()) return;
    const int launcher = launchers_[static_cast<std::size_t>(randomInt(static_cast<int>(launchers_.size())))];
    if (ballOverlapsTile(launcher % BoardColumns, launcher / BoardColumns)) {
        state_.nextLaunch = 0;
        return;
    }
    spawnBall(state_.queue.front(), tileCenter(launcher));
    state_.queue.erase(state_.queue.begin());
    state_.timerStarted = true;
    state_.launcherBlink = false;
    state_.nextLaunch = mechanics_.launchIntervalSeconds;
}
void Game::updateGates(double dt) {
    for (int i = 0; i < BoardCellCount; ++i) {
        auto& t = state_.tiles[static_cast<std::size_t>(i)];
        if (t.kind() != 6 && t.kind() != 7) continue;
        t.stateTimer += dt;
        t.animationTimer += dt;
        if (t.kind() == 6) {
            // Original EXE: state 0 uses the second constructor duration
            // (p1); raised state 2 uses the first duration (p2).
            const double openTime = (t.param1() + 1) * 0.5;
            const double closedTime = (t.param2() + 1) * 0.5;
            if (t.gateState == 0) {
                if (t.stateTimer >= openTime) { t.gateState = 1; t.stateTimer = 0; }
                else if (t.animationFrame > 0 && t.animationTimer > 0.05) {
                    --t.animationFrame; t.animationTimer = 0;
                }
            } else if (t.gateState == 1) {
                if (t.stateTimer > closedTime) { t.gateState = 0; t.stateTimer = 0; }
                else if (!ballOverlapsTile(i % BoardColumns, i / BoardColumns)) {
                    t.gateState = 2; t.stateTimer = t.animationTimer = 0;
                }
            } else if (t.stateTimer >= closedTime) {
                t.gateState = 0; t.stateTimer = t.animationTimer = 0;
            } else if (t.animationFrame < 4 && t.animationTimer > 0.05) {
                ++t.animationFrame; t.animationTimer = 0;
            }
        } else {
            if (t.gateState == 0 && t.animationFrame > 0 && t.animationTimer > 0.05) {
                --t.animationFrame; t.animationTimer = 0;
            } else if (t.gateState == 1) {
                if (!ballOverlapsTile(i % BoardColumns, i / BoardColumns)) {
                    t.gateState = 2; t.animationTimer = 0;
                }
            } else if (t.gateState == 2 && t.animationFrame < 4 && t.animationTimer > 0.05) {
                ++t.animationFrame; t.animationTimer = 0;
            }
        }
    }
}
void Game::toggleKeyWalls(int color) {
    for (auto& tile : state_.tiles)
        if (tile.kind() == 7 && tile.color() == color) tile.gateState = tile.gateState == 0 ? 1 : 0;
}
void Game::addScore(std::uint64_t value) {
    state_.score += value;
    state_.highScore = std::max(state_.highScore, state_.score);
    awardBlocks();
}
void Game::awardBlocks() {
    const auto total = state_.score / 1600;
    const auto previous = lastBlockAwardScore_ / 1600;
    if (total > previous) state_.spareBlocks += static_cast<int>(total - previous);
    lastBlockAwardScore_ = total * 1600;
}
void Game::lose(const char* message) {
    state_.timerVisible = true;
    statistics_.run.activeSeconds = boardActiveSeconds_;
    state_.phase = Phase::GameOver;
    state_.message = message;
    endStroke();
}
void Game::checkCompletion() {
    if (state_.phase != Phase::Playing || !state_.queue.empty()) return;
    if (std::any_of(state_.balls.begin(), state_.balls.end(), [](const Ball& b) { return b.active; })) return;
    completedBoards_.push_back({state_.levelId, boardActiveSeconds_, mechanics_.timeLimitSeconds,
        state_.score - boardStartingScore_, MaximumBoardPoints(levels_[currentLevelIndex_]), statistics_.run.inkLength});
    state_.phase = Phase::LevelComplete;
    state_.timerVisible = true;
    count(&RunStatistics::boardsCleared);
    statistics_.run.activeSeconds = boardActiveSeconds_;
    for (auto* totals : {&statistics_.run, &statistics_.game, &statistics_.lifetime}) {
        totals->completionActiveSeconds += boardActiveSeconds_;
        if (!state_.unlimitedTime) {
            totals->completionRemainingSeconds += state_.timeRemaining;
            ++totals->timedBoardsCleared;
        }
    }
    state_.celebrationElapsed = 0;
    completionAccumulator_ = completionDelay_ = 0;
    endStroke();
}

void Game::interactFloor(Ball& b) {
    int closest = -1;
    double distance = std::numeric_limits<double>::max();
    for (const auto corner : boundingCorners(b)) {
        const int index = tileAt(corner);
        if (index < 0) continue;
        const auto& tile = state_.tiles[static_cast<std::size_t>(index)];
        // The base floor/launcher virtual method participates only while the
        // ball is shrunken; touching it restores the ball's original diameter.
        const bool restoreSize = (tile.kind() == 0 || tile.kind() == 1) && b.tallness != 0;
        if (tile.kind() != 2 && tile.kind() != 8 && !restoreSize) continue;
        if (!perimeterIntersects(b, tileRect(index))) continue;
        const double d = lengthSquared(tileCenter(index) - integerCenter(b));
        if (d < distance) { distance = d; closest = index; }
    }
    if (closest < 0) return;
    auto& tile = state_.tiles[static_cast<std::size_t>(closest)];
    if (tile.kind() == 0 || tile.kind() == 1) {
        b.tallness = 0;
        b.radius = 12;
        constrainToBounds(b);
        return;
    }
    if (tile.kind() == 8) {
        // Keep the original discrete impulse exactly at the default 1x.
        // Its historical step timing is intentional, not a dt-scaled force.
        const double push = 3 * mechanics_.chevronAccelerationMultiplier;
        const double limit = mechanics_.chevronSpeedLimit;
        switch (tile.param1()) {
        case 0: b.velocity.y = std::clamp(b.velocity.y - push, -limit, limit); break;
        case 1: b.velocity.y = std::clamp(b.velocity.y + push, -limit, limit); break;
        case 2: b.velocity.x = std::clamp(b.velocity.x - push, -limit, limit); break;
        case 3: b.velocity.x = std::clamp(b.velocity.x + push, -limit, limit); break;
        }
        return;
    }
    const Rect r = tileRect(closest);
    const int quadrant = tile.param1();
    const Vec2 center{quadrant == 0 || quadrant == 2 ? r.right : r.left,
                      quadrant == 0 || quadrant == 1 ? r.bottom : r.top};
    const Vec2 offset = integerCenter(b) - center;
    const double d = std::sqrt(lengthSquared(offset));
    b.nearHole = d <= 42;
    if (d + b.radius <= 12) {
        if (tile.color() && b.color && tile.color() != b.color) {
            lose("You put a ball in a hole of a different color. Game over.");
        } else {
            b.active = false;
            count(&RunStatistics::ballsSunk);
            if (tile.color()) addScore(DrainPoints[static_cast<std::size_t>(b.color)]);
            if (tile.color() && b.color) toggleKeyWalls(tile.color());
        }
        return;
    }
    if (d <= 32 && d > 1e-12) {
        b.velocity = b.velocity * 0.993 - offset * (25 / (d * d));
        b.tallness = static_cast<int>(-24 / d);
        b.radius = std::max(1.0, 12.0 + b.tallness);
    }
}
void Game::collideBalls(Ball& b) {
    for (auto& other : state_.balls) {
        if (!other.active || other.id == b.id) continue;
        const Vec2 delta = b.position - other.position;
        const double d2 = lengthSquared(delta);
        const double r = b.radius + other.radius;
        if (d2 >= r * r || d2 <= 1e-12) continue;
        const Vec2 normal = delta * (1 / std::sqrt(d2));
        const double relative = dot(b.velocity - other.velocity, normal);
        if (relative >= 0) continue;
        count(&RunStatistics::ballHits);
        b.velocity = b.velocity - normal * relative;
        other.velocity = other.velocity + normal * relative;
    }
}
void Game::collideInk(Ball& b, Vec2 /*oldPosition*/) {
    bool close = false;
    for (const auto& s : state_.strokes)
        if (strokeTouches(s, integerCenter(b), b.radius + InkRadius)) { close = true; break; }
    if (!close) return;
    const bool reflected = reflectFromSamples(b, [&](Vec2 point) {
        for (const auto& s : state_.strokes)
            // The one-pixel tolerance compensates for the original integer
            // perimeter samples. Keep the exact capsule overlap as a guard so
            // this tolerance cannot make ink outside the visible edge collide.
            if (strokeTouches(s, integerCenter(b), b.radius + InkRadius) &&
                strokeTouches(s, point, InkRadius + 1.0)) return true;
        return false;
    });
    if (!reflected) return;
    count(&RunStatistics::inkHits);
    // IInkDisp::HitTest returned all intersecting strokes; each whole stroke was
    // removed after the first deflection. Preserve that behavior for overlaps.
    state_.strokes.erase(std::remove_if(state_.strokes.begin(), state_.strokes.end(), [&](const Stroke& s) {
        if (!strokeTouches(s, integerCenter(b), b.radius + InkRadius)) return false;
        if (s.id == activeStroke_) { activeStroke_ = 0; strokeConsumed_ = true; }
        return true;
    }), state_.strokes.end());
}
void Game::collideWalls(Ball& b, Vec2 /*oldPosition*/) {
    int selected = -1;
    double distance = std::numeric_limits<double>::max();
    for (const auto corner : boundingCorners(b)) {
        const int index = tileAt(corner);
        if (index < 0) continue;
        const auto& tile = state_.tiles[static_cast<std::size_t>(index)];
        if (!deflectsAt(tile, index, b, corner) || !perimeterIntersects(b, tileRect(index))) continue;
        const double d = lengthSquared(tileCenter(index) - integerCenter(b));
        if (d < distance) { distance = d; selected = index; }
    }
    if (selected < 0) return;
    auto& tile = state_.tiles[static_cast<std::size_t>(selected)];
    if (tile.kind() == 3 && tile.color()) { b.color = tile.color(); b.nearHole = false; }
    const bool wallReflected = reflectFromSamples(b, [&](Vec2 point) {
        const int index = tileAt(point);
        return index >= 0 && deflectsAt(state_.tiles[static_cast<std::size_t>(index)], index, b, point);
    });
    if (wallReflected) count(&RunStatistics::wallHits);
    if (tile.kind() == 4 && (tile.color() == 0 || tile.color() == b.color)) {
        const auto replacement = tile.underlay;
        resetTile(tile, replacement);
        addScore(10);
    }
}
void Game::updateBall(Ball& b, double dt) {
    if (!b.active) return;
    const Vec2 old = b.position;
    const double oldRadius = b.radius;
    b.position = b.position + b.velocity * dt;
    if (constrainToBounds(b)) return;
    interactFloor(b);
    if (!b.active || state_.phase != Phase::Playing) return;
    if (static_cast<int>(old.x - oldRadius) == static_cast<int>(b.position.x - b.radius) &&
        static_cast<int>(old.y - oldRadius) == static_cast<int>(b.position.y - b.radius)) return;
    collideBalls(b);
    collideInk(b, old);
    collideWalls(b, old);
}

void Game::tick(double seconds) {
    if (!std::isfinite(seconds) || seconds <= 0 || state_.paused || state_.phase == Phase::GameOver) return;
    // Split large caller updates into ordinary frame intervals; moving objects
    // then use the original maximum 4 ms physics step within each frame.
    while (seconds > 1e-10 && !state_.paused && state_.phase != Phase::GameOver) {
        const double frame = std::min(seconds, 0.016);
        seconds -= frame;
        state_.elapsed += frame;
        if (state_.phase == Phase::LevelComplete) {
            state_.celebrationElapsed += frame;
            if (!state_.unlimitedTime && state_.timeRemaining > 0) {
                completionAccumulator_ += frame;
                while (completionAccumulator_ >= 0.020 && state_.timeRemaining > 0) {
                    completionAccumulator_ -= 0.020;
                    state_.timeRemaining = std::max(0.0, state_.timeRemaining - 1.0);
                    addScore(1);
                }
            } else {
                completionDelay_ += frame;
                if (completionDelay_ >= 1.5) {
                    ++completedLevels_;
                    initializeLevel(chooseLevel(completedLevels_ % 5 == 0));
                }
            }
            continue;
        }
        if (state_.timerStarted) {
            boardActiveSeconds_ += frame;
            statistics_.lifetime.activeSeconds += frame;
        }
        // The original update-list inserts the timer before the launcher.
        if (state_.timerStarted && !state_.unlimitedTime) {
            state_.timeRemaining = std::max(0.0, state_.timeRemaining - frame);
            if (state_.timeRemaining <= 0) { lose("Time has run out. Game over."); break; }
            // The original uses <11000 ms (displayed 010 or less), toggles
            // every 333 ms, and shows the final 000 continuously.
            if (state_.timeRemaining < 11 && state_.timeRemaining >= 1) {
                timerBlinkAccumulator_ += frame;
                if (timerBlinkAccumulator_ + 1e-12 >= 0.333) {
                    state_.timerVisible = !state_.timerVisible;
                    timerBlinkAccumulator_ = 0;
                }
            } else {
                state_.timerVisible = true;
                timerBlinkAccumulator_ = 0;
            }
        }
        updateLaunchers(frame);
        updateGates(frame);
        double remaining = frame;
        while (remaining > 1e-10 && state_.phase == Phase::Playing) {
            const double step = std::min(remaining, 0.004);
            remaining -= step;
            for (auto& ball : state_.balls) {
                updateBall(ball, step);
                if (state_.phase != Phase::Playing) break;
            }
            checkCompletion();
        }
        awardBlocks();
    }
}

void Game::beginStroke(Vec2 point) {
    endStroke();
    if (state_.paused || state_.phase != Phase::Playing || !validPoint(point) || tileAt(point) < 0) return;
    strokeConsumed_ = false;
    if (state_.strokes.size() >= static_cast<std::size_t>(inkStrokeLimit_)) state_.strokes.erase(state_.strokes.begin());
    activeStroke_ = nextStrokeId_++;
    state_.strokes.push_back({activeStroke_, {clipped(point)}});
    count(&RunStatistics::strokes);
}
void Game::extendStroke(Vec2 point) {
    if (!activeStroke_ || strokeConsumed_ || state_.paused || state_.phase != Phase::Playing || !validPoint(point)) return;
    auto it = std::find_if(state_.strokes.begin(), state_.strokes.end(), [&](const Stroke& s) { return s.id == activeStroke_; });
    if (it == state_.strokes.end() || it->points.size() >= 32768) return;
    point = clipped(point);
    const double segmentSquared = lengthSquared(it->points.back() - point);
    if (segmentSquared >= 0.25) {
        const double length = std::sqrt(segmentSquared);
        statistics_.run.inkLength += length;
        statistics_.game.inkLength += length;
        statistics_.lifetime.inkLength += length;
        it->points.push_back(point);
    }
}
void Game::endStroke() { activeStroke_ = 0; strokeConsumed_ = false; }
Statistics Game::statistics() const {
    auto result = statistics_;
    if (state_.phase == Phase::Playing) result.run.activeSeconds = boardActiveSeconds_;
    result.game.activeSeconds = priorBoardSeconds_ + result.run.activeSeconds;
    return result;
}
void Game::seedLifetimeStatistics(const RunStatistics& totals) {
    if (lifetimeSeeded_) return;
    lifetimeSeeded_ = true;
    statistics_.lifetime = totals;
}
void Game::startStatisticsGame() {
    completedBoards_.clear();
    if (state_.levelSerial) {
        const auto current = statistics();
        statistics_.previousGame = current.game;
        statistics_.previousRun = current.run;
        statistics_.hasPreviousGame = statistics_.hasPreviousRun = true;
    }
    statistics_.game = {};
    statistics_.run = {};
    priorBoardSeconds_ = 0;
    newStatisticsGame_ = true;
}
void Game::clearInk() { state_.strokes.clear(); endStroke(); }
bool Game::setInkStrokeLimit(int limit) {
    if (limit < 1 || limit > MaximumInkStrokeLimit) return false;
    inkStrokeLimit_ = limit;
    const auto retained = static_cast<std::size_t>(limit);
    if (state_.strokes.size() > retained)
        state_.strokes.erase(state_.strokes.begin(), state_.strokes.end() - retained);
    return true;
}
bool Game::placeBlock(Vec2 point) {
    if (state_.paused || state_.phase != Phase::Playing || state_.spareBlocks <= 0 || !validPoint(point)) return false;
    const int index = tileAt(point);
    if (index < 0 || state_.tiles[static_cast<std::size_t>(index)].kind() != 0 ||
        ballOverlapsTile(index % BoardColumns, index / BoardColumns)) return false;
    resetTile(state_.tiles[static_cast<std::size_t>(index)], 0x3000);
    --state_.spareBlocks;
    return true;
}
}
