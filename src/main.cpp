#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include "Timing.hpp"
#include <array>
#include <cmath>
#include <fstream>
#include <map>
using namespace geode::prelude;

namespace {
enum class State { Idle, Record, Verify, Analyze, Ready, Playback };
enum class Pending { None, Pass, Fail };
struct Checkpoint { size_t inputs, trace; std::array<bool, 6> held; };
struct Session {
    PlayLayer* owner = nullptr;
    cf::Recording recording;
    State state = State::Idle;
    Pending pending = Pending::None;
    bool injecting = false, resetting = false, restored = false, returningToPause = false;
    std::array<bool, 6> recordedHeld{};
    std::array<bool, 6> physicalHeld{};
    std::array<bool, 6> injectedHeld{};
    std::map<CheckpointObject*, Checkpoint> checkpoints;
    std::vector<cf::Input> trial;
    std::vector<bool> outcomes;
    size_t next = 0, sample = 0, target = 0;
    int offset = 0, tests = 0, lastTick = -1;
    int watchdogTick = -1;
    size_t comparedSamples = 0;
    float stalledSeconds = 0;
    std::array<int, 5> tally{};
    std::array<bool, 5> visibleRows{};
    std::string message = "Record a practice run";
};
// Exactly one live PlayLayer owns this session. It is discarded on exit/init.
Session s;
bool hasLiveOwner() {
    auto current = PlayLayer::get();
    return current && s.owner == current && current->m_level && current->m_player1 && current->m_player2;
}
bool running() { return s.state == State::Verify || s.state == State::Analyze || s.state == State::Playback; }
int tick() { return static_cast<int>(s.owner->m_gameState.m_currentProgress); }
int slot(int button, bool p1) { return (p1 ? 0 : 3) + button - 1; }
std::filesystem::path path() { return Mod::get()->getSaveDir() / ("run-" + s.recording.key + ".cfr"); }
void fixPracticeTouchPriority() {
    if (!hasLiveOwner() || !s.owner->m_uiLayer) return;
    auto dispatcher = CCDirector::get()->getTouchDispatcher();
    if (!dispatcher) return;
    dispatcher->setPriority(0, s.owner->m_uiLayer);
    if (auto children = s.owner->m_uiLayer->getChildren()) {
        for (unsigned int i = 0; i < children->count(); ++i) {
            if (auto menu = typeinfo_cast<CCMenu*>(children->objectAtIndex(i))) {
                menu->setEnabled(true);
                dispatcher->setPriority(-128, menu);
            }
        }
    }
}
void save() {
    auto tmp = path(); tmp += ".tmp";
    std::ofstream out(tmp); cf::write(out, s.recording); out.close();
    if (!out) { s.message = "Could not save recording"; return; }
    std::error_code ec;
    // Keep a recoverable previous version on platforms where rename cannot replace.
    auto backup = path(); backup += ".bak";
    if (std::filesystem::exists(path())) {
        std::filesystem::remove(backup, ec); ec.clear();
        std::filesystem::rename(path(), backup, ec);
        if (ec) { s.message = "Could not back up recording"; return; }
    }
    std::filesystem::rename(tmp, path(), ec);
    if (ec) s.message = "Could not finish saving recording";
}
void releaseInjected() {
    if (!hasLiveOwner()) return;
    s.injecting = true;
    for (int b = 1; b <= 3; ++b) {
        for (bool p1 : {true, false}) {
            auto index = slot(b, p1);
            if (s.injectedHeld[index]) s.owner->handleButton(false, b, p1);
        }
    }
    s.injecting = false; s.injectedHeld.fill(false);
}
void clearMarkers();
void queueReturnToPause() {
    if (!hasLiveOwner() || s.returningToPause) return;
    s.returningToPause = true;
    Loader::get()->queueInMainThread([] {
        if (!hasLiveOwner()) { s.returningToPause = false; return; }
        s.resetting = true;
        releaseInjected();
        clearMarkers();
        s.owner->removeAllCheckpoints();
        s.owner->resetLevel();
        s.resetting = false;
        fixPracticeTouchPriority();
        Loader::get()->queueInMainThread([] {
            if (hasLiveOwner()) s.owner->pauseGame(false);
            s.returningToPause = false;
        });
    });
}
void stop(std::string message) {
    s.state = s.recording.ready() ? State::Ready : State::Idle;
    s.pending = Pending::None; releaseInjected(); s.message = std::move(message); save();
}
void resetTrial() {
    if (!hasLiveOwner()) { s.message = "No active level. Reopen Frames from pause."; return; }
    s.resetting = true; releaseInjected(); clearMarkers();
    // Automated runs stay in practice and reset through the normal practice path.
    if (!s.owner->m_isPracticeMode) s.owner->togglePracticeMode(true);
    s.owner->removeAllCheckpoints(); s.owner->resetLevel();
    s.resetting = false; s.next = 0; s.sample = 0; s.lastTick = -1;
    s.watchdogTick = -1; s.stalledSeconds = 0;
    s.comparedSamples = 0;
    fixPracticeTouchPriority();
}
void hud() {
    auto pl = s.owner;
    auto label = typeinfo_cast<CCLabelBMFont*>(pl->getChildByID("cf-hud"));
    if (!label) {
        label = CCLabelBMFont::create("", "chatFont.fnt"); label->setID("cf-hud");
        auto win = CCDirector::get()->getWinSize();
        label->setAnchorPoint({0, 1}); label->setPosition({10, win.height - 10});
        pl->addChild(label, 1000);
    }
    label->setScale(static_cast<float>(Mod::get()->getSettingValue<double>("counter-scale")) * .6f);
    std::string text = s.message;
    if (s.state == State::Playback) {
        for (int i = 0; i < 5; ++i)
            if (s.visibleRows[i]) text += fmt::format("\n{}{}F: {}", i + 1, i == 4 ? "+" : "", s.tally[i]);
    }
    label->setString(text.c_str());
}
constexpr ccColor3B WINDOW_COLORS[5] = {
    {255, 75, 75}, {255, 150, 55}, {255, 225, 70}, {245, 245, 245}, {105, 245, 125}
};
void clearMarkers() {
    if (!hasLiveOwner() || !s.owner->m_objectLayer) return;
    if (auto node = s.owner->m_objectLayer->getChildByID("cf-markers")) node->removeFromParent();
}
void prepareShowcase() {
    s.tally.fill(0); s.visibleRows = cf::visibleCategories(s.recording); clearMarkers();
}
void marker(size_t index) {
    auto const e = s.recording.inputs[index];
    if (e.button != 1) return;
    auto w = s.recording.windows[index];
    if (!w.measured) return;
    ++s.tally[cf::category(w)];
    if (Mod::get()->getSettingValue<bool>("markers") && s.owner->m_objectLayer) {
        auto root = s.owner->m_objectLayer->getChildByID("cf-markers");
        if (!root) {
            root = CCNode::create(); root->setID("cf-markers"); s.owner->m_objectLayer->addChild(root, 1000);
        }
        auto node = CCNode::create(); node->setPosition({e.x, e.y});
        float scale = static_cast<float>(Mod::get()->getSettingValue<double>("marker-scale"));
        auto color3 = WINDOW_COLORS[cf::category(w)];
        ccColor4F border{color3.r / 255.f, color3.g / 255.f, color3.b / 255.f, 1.f};
        ccColor4F fill{border.r, border.g, border.b, e.down ? .22f : .04f};
        auto circle = CCDrawNode::create(); circle->drawCircle({0, 0}, 18.f * scale, fill, e.down ? 2.5f : 1.5f, border, 48);
        node->addChild(circle);
        auto label = CCLabelBMFont::create(fmt::format("{}{}F{}\n-{} / +{}", e.down ? "" : "R ", w.width(), w.lowerBound ? "+" : "", w.early, w.late).c_str(), "bigFont.fnt");
        label->setAlignment(kCCTextAlignmentCenter); label->setScale(.28f * scale);
        label->setColor(color3); label->setPosition({0, 27.f * scale}); node->addChild(label, 1);
        root->addChild(node);
    }
    if (Mod::get()->getSettingValue<bool>("sounds")) {
        auto sound = w.width() > 20 ? "frame-wide.wav" : fmt::format("frame-{}.wav", w.width());
        FMODAudioEngine::get()->playEffect(fmt::format("{}/{}", Mod::get()->getID(), sound));
    }
}
bool supported() {
    if (!hasLiveOwner()) { s.message = "No active level. Reopen Frames from pause."; return false; }
    if (s.owner->m_isPlatformer) { s.message = "Platformer is not supported"; return false; }
    if (s.owner->m_isIgnoreDamageEnabled) { s.message = "Disable ignore damage before recording or analysis"; return false; }
    if (Loader::get()->isModLoaded("syzzi.click_between_frames")) {
        s.message = "Disable Click Between Frames for 240 TPS analysis"; return false;
    }
    return true;
}
void beginAnalysis() {
    if (!s.recording.complete || s.recording.inputs.empty() || !supported()) return;
    int radius = static_cast<int>(Mod::get()->getSettingValue<int64_t>("scan-radius"));
    if (s.recording.radius != radius) s.recording.windows.clear();
    s.recording.radius = radius;
    s.recording.windows.resize(s.recording.inputs.size()); s.recording.verified = false;
    s.state = State::Verify; s.pending = Pending::None; s.tests = 0;
    s.trial = s.recording.inputs;
    s.message = "Verifying original run (240 TPS)"; resetTrial();
}
void nextEvent() {
    while (s.target < s.recording.inputs.size() &&
        (s.recording.inputs[s.target].button != 1 || s.recording.windows[s.target].measured)) ++s.target;
    if (s.target == s.recording.inputs.size()) {
        stop("Analysis complete. Pause > Frames > Play");
        queueReturnToPause(); return;
    }
    s.outcomes.assign(2 * s.recording.radius + 1, false);
    s.outcomes[s.recording.radius] = true; s.offset = -s.recording.radius;
}
void startCandidate() {
    if (s.state != State::Analyze) return;
    if (s.offset == 0) ++s.offset;
    if (s.offset > s.recording.radius) {
        s.recording.windows[s.target] = cf::measure(s.outcomes, s.recording.radius);
        save(); ++s.target; nextEvent();
        if (s.state != State::Analyze) return;
    }
    s.message = fmt::format("Event {}/{} | offset {:+} | {} tests", s.target + 1, s.recording.inputs.size(), s.offset, s.tests);
    if (!cf::trial(s.recording.inputs, s.target, s.offset, s.trial)) {
        s.pending = Pending::Fail; return;
    }
    resetTrial();
}
void consume() {
    auto result = s.pending; s.pending = Pending::None;
    if (s.state == State::Verify) {
        if (result == Pending::Fail) { stop(s.message); queueReturnToPause(); return; }
        s.recording.verified = true; s.state = State::Analyze; s.target = 0;
        nextEvent(); startCandidate();
    } else if (s.state == State::Analyze) {
        ++s.tests; s.outcomes[s.offset + s.recording.radius] = result == Pending::Pass;
        ++s.offset; startCandidate();
    } else if (s.state == State::Playback) {
        stop(result == Pending::Pass ? "Showcase complete. Replay available" : s.message);
        queueReturnToPause();
    }
}
}

class $modify(CFBase, GJBaseGameLayer) {
    void handleButton(bool down, int button, bool p1) {
        if (static_cast<GJBaseGameLayer*>(s.owner) != this) return GJBaseGameLayer::handleButton(down, button, p1);
        if (button >= 1 && button <= 3 && (!m_level || !m_level->m_twoPlayerMode)) p1 = true;
        if (button >= 1 && button <= 3 && !s.injecting && !s.resetting)
            s.physicalHeld[slot(button, p1)] = down;
        if (running() && !s.injecting) return;
        if (s.injecting && button >= 1 && button <= 3) s.injectedHeld[slot(button, p1)] = down;
        if (s.state == State::Record && !s.resetting && !s.owner->m_isPaused &&
            !s.owner->m_inResetDelay && !s.owner->m_hasCompletedLevel &&
            s.owner->m_player1 && !s.owner->m_player1->m_isDead && button >= 1 && button <= 3) {
            auto i = slot(button, p1);
            if (s.recordedHeld[i] != down) {
                auto player = p1 ? s.owner->m_player1 : s.owner->m_player2;
                auto position = player ? player->getPosition() : CCPointZero;
                s.recording.inputs.push_back({tick(), down, button, p1, position.x, position.y});
                s.recordedHeld[i] = down;
            }
        }
        GJBaseGameLayer::handleButton(down, button, p1);
    }
    void processCommands(float dt, bool half, bool last) {
        if (static_cast<GJBaseGameLayer*>(s.owner) == this && running() && s.pending == Pending::None) {
            bool validStep = std::abs(dt - 1.f / 240.f) <= .0001f || std::abs(dt - .25f) <= .005f;
            if (dt > 0 && !validStep) {
                s.message = "Unsupported physics step: use 240 TPS without CBF"; s.pending = Pending::Fail;
                if (s.state == State::Analyze) { stop(s.message); queueReturnToPause(); }
                return;
            }
            auto const& events = s.state == State::Playback ? s.recording.inputs : s.trial;
            s.injecting = true;
            while (s.next < events.size() && events[s.next].tick <= tick()) {
                auto index = s.next++; auto e = events[index];
                handleButton(e.down, e.button, e.p1);
                if (s.state == State::Playback) marker(index);
            }
            s.injecting = false;
            if (tick() > s.recording.endTick + 2400) {
                s.message = "Replay timed out"; s.pending = Pending::Fail;
            }
        }
        GJBaseGameLayer::processCommands(dt, half, last);
    }
};

class $modify(CFPlay, PlayLayer) {
    static void bindSession(PlayLayer* layer) {
        s = {}; s.owner = layer;
        auto level = layer->m_level;
        s.recording.key = cf::fingerprint(fmt::format("{}:{}:{}", level->m_levelID.value(), std::string(level->m_levelName), std::string(level->m_levelString)));
        auto recordingPath = path();
        std::ifstream file(recordingPath); cf::Recording loaded;
        if (cf::read(file, s.recording.key, loaded)) {
            s.recording = std::move(loaded); s.state = s.recording.ready() ? State::Ready : State::Idle;
            s.message = s.recording.ready() ? "Saved showcase available" : "Saved run: Analyze to continue";
        } else if (std::filesystem::exists(recordingPath)) s.message = "Recording format changed. Record a new run.";
        layer->unschedule(schedule_selector(CFPlay::service));
        layer->schedule(schedule_selector(CFPlay::service));
    }
    bool init(GJGameLevel* level, bool replay, bool objects) {
        // Other hooks may create/exit temporary layers during the original init.
        // Bind only after the actual level has finished initializing.
        if (!PlayLayer::init(level, replay, objects)) return false;
        bindSession(this);
        return true;
    }
    void service(float dt) {
        if (s.owner != this || !hasLiveOwner()) return;
        if (m_isPaused) { hud(); return; }
        if (running() && !m_isPaused && s.pending == Pending::None) {
            if (s.watchdogTick == tick()) s.stalledSeconds += dt;
            else { s.watchdogTick = tick(); s.stalledSeconds = 0; }
            if (s.stalledSeconds > 10) { s.message = "Replay stalled for 10 seconds"; s.pending = Pending::Fail; }
        }
        if (s.pending != Pending::None) consume();
        hud();
    }
    void pauseGame(bool unfocused) {
        if (s.owner == this) s.physicalHeld.fill(false);
        PlayLayer::pauseGame(unfocused);
    }
    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        if (s.owner != this || s.resetting || s.pending != Pending::None || !m_player1 || !m_player2) return;
        int now = tick();
        if (s.state == State::Record && now != s.lastTick) {
            auto point = cf::Sample{now, m_player1->getPositionX(), m_player1->getPositionY(), m_player2->getPositionX(), m_player2->getPositionY()};
            if (!s.recording.trace.empty() && s.recording.trace.back().tick == now) s.recording.trace.back() = point;
            else s.recording.trace.push_back(point);
            s.lastTick = now;
        } else if (s.state == State::Verify || s.state == State::Playback) {
            auto const& trace = s.recording.trace;
            while (s.sample < trace.size() && trace[s.sample].tick < now) ++s.sample;
            if (s.sample < trace.size() && trace[s.sample].tick == now) {
                ++s.comparedSamples;
                auto point = trace[s.sample++];
                if (std::abs(point.x - m_player1->getPositionX()) > .5f || std::abs(point.y - m_player1->getPositionY()) > .5f ||
                    std::abs(point.x2 - m_player2->getPositionX()) > .5f || std::abs(point.y2 - m_player2->getPositionY()) > .5f) {
                    s.message = fmt::format("Replay divergence at tick {}", now); s.pending = Pending::Fail;
                }
            }
        }
    }
    CheckpointObject* createCheckpoint() {
        if (s.owner == this && running()) return nullptr;
        return PlayLayer::createCheckpoint();
    }
    CheckpointObject* markCheckpoint() {
        if (s.owner == this && running()) return nullptr;
        return PlayLayer::markCheckpoint();
    }
    void storeCheckpoint(CheckpointObject* cp) {
        if (s.owner == this && running()) return;
        PlayLayer::storeCheckpoint(cp);
        if (s.owner == this && s.state == State::Record && cp)
            s.checkpoints[cp] = {s.recording.inputs.size(), s.recording.trace.size(), s.recordedHeld};
    }
    void removeCheckpoint(bool first) {
        if (m_checkpointArray && m_checkpointArray->count()) {
            auto cp = static_cast<CheckpointObject*>(m_checkpointArray->objectAtIndex(first ? 0 : m_checkpointArray->count() - 1));
            s.checkpoints.erase(cp);
        }
        PlayLayer::removeCheckpoint(first);
    }
    void removeAllCheckpoints() {
        s.checkpoints.clear(); PlayLayer::removeAllCheckpoints();
    }
    void loadFromCheckpoint(CheckpointObject* cp) {
        PlayLayer::loadFromCheckpoint(cp);
        if (s.state != State::Record) return;
        auto it = s.checkpoints.find(cp);
        int now = tick();
        if (it == s.checkpoints.end()) {
            while (!s.recording.inputs.empty() && s.recording.inputs.back().tick > now) s.recording.inputs.pop_back();
            while (!s.recording.trace.empty() && s.recording.trace.back().tick > now) s.recording.trace.pop_back();
            s.recordedHeld.fill(false);
            for (auto const& input : s.recording.inputs) s.recordedHeld[slot(input.button, input.p1)] = input.down;
            s.restored = true;
            s.lastTick = s.recording.trace.empty() ? -1 : s.recording.trace.back().tick;
            return;
        }
        auto saved = it->second;
        if (saved.inputs > s.recording.inputs.size() || saved.trace > s.recording.trace.size()) {
            stop("Checkpoint no longer belongs to this run"); return;
        }
        s.recording.inputs.resize(saved.inputs); s.recording.trace.resize(saved.trace);
        while (!s.recording.inputs.empty() && s.recording.inputs.back().tick > now) s.recording.inputs.pop_back();
        while (!s.recording.trace.empty() && s.recording.trace.back().tick > now) s.recording.trace.pop_back();
        s.recordedHeld = saved.held; s.restored = true;
        s.lastTick = s.recording.trace.empty() ? -1 : s.recording.trace.back().tick;
    }
    void resetLevel() {
        if (running() && !s.resetting) stop("Automated run cancelled by manual restart");
        s.restored = false; PlayLayer::resetLevel();
        if (s.state == State::Record && !s.resetting) {
            if (!s.restored) {
                s.recording.inputs.clear(); s.recording.trace.clear(); s.checkpoints.clear();
                s.recordedHeld.fill(false);
            }
            s.lastTick = s.recording.trace.empty() ? -1 : s.recording.trace.back().tick;
        }
    }
    void destroyPlayer(PlayerObject* player, GameObject* object) {
        if (running()) { s.message = fmt::format("Replay died at tick {}", tick()); s.pending = Pending::Fail; return; }
        PlayLayer::destroyPlayer(player, object);
    }
    void levelComplete() {
        if (s.state == State::Record) {
            s.recording.complete = true; s.recording.endTick = tick();
            s.recording.verified = false; s.recording.windows.clear();
            s.state = State::Idle; s.message = "Run saved. Pause > Frames > Analyze"; save();
            queueReturnToPause(); return;
        }
        if (running()) {
            if (s.pending == Pending::None) {
                auto const& events = s.state == State::Playback ? s.recording.inputs : s.trial;
                bool valid = s.next == events.size();
                if (s.state == State::Verify || s.state == State::Playback)
                    valid = valid && std::abs(tick() - s.recording.endTick) <= 1 &&
                        (!s.recording.trace.empty() && s.comparedSamples > 0);
                s.pending = valid ? Pending::Pass : Pending::Fail;
                if (!valid) s.message = "Completion did not match recorded run";
            }
            return;
        }
        PlayLayer::levelComplete();
    }
    void onExit() {
        // onExit is a scene lifecycle notification, not destruction. Reparenting
        // and scene overlays must not erase the current recording/session.
        if (s.owner == this && !s.recording.key.empty()) save();
        PlayLayer::onExit();
    }
};

class CFPopup : public Popup {
    static bool ensureSession() {
        auto layer = PlayLayer::get();
        if (!layer || !layer->m_level || !layer->m_player1 || !layer->m_player2) {
            FLAlertLayer::create("Click Frames", "No active level. Close this menu and reopen it from pause.", "OK")->show();
            return false;
        }
        if (s.owner != layer) CFPlay::bindSession(layer);
        return true;
    }
    static void resumeGame() {
        if (auto scene = CCDirector::get()->getRunningScene())
            if (auto pause = scene->getChildByType<PauseLayer>(0)) pause->onResume(nullptr);
    }
    static void resumeAndRun(std::function<void()> action) {
        Loader::get()->queueInMainThread([action = std::move(action)] {
            if (auto scene = CCDirector::get()->getRunningScene())
                if (auto pause = scene->getChildByType<PauseLayer>(0)) pause->onResume(nullptr);
            if (hasLiveOwner()) { action(); fixPracticeTouchPriority(); }
        });
    }
    bool init() {
        if (!ensureSession()) return false;
        if (!Popup::init(430, 270)) return false;
        setTitle("CLICK FRAMES");
        auto label = [this](std::string const& text, CCPoint pos, float scale, ccColor3B color) {
            auto node = CCLabelBMFont::create(text.c_str(), "chatFont.fnt");
            node->setScale(scale); node->setColor(color); node->setPosition(pos);
            m_mainLayer->addChild(node); return node;
        };
        label("PRACTICE  >  ANALYZE  >  SHOWCASE", {215, 229}, .55f, {111, 219, 235});
        auto status = label(s.message, {215, 211}, .5f, {220, 229, 244});
        status->limitLabelWidth(398, .5f, .25f);
        bool active = s.state == State::Record || running();
        auto add = [this](char const* title, char const* hint, CCPoint pos, bool enabled,
                          std::function<void()> action) {
            auto card = CCScale9Sprite::create("square02b_001.png");
            card->setContentSize({128, 48});
            card->setColor(enabled ? ccColor3B{34, 57, 83} : ccColor3B{35, 39, 49});
            card->setOpacity(245);
            auto heading = CCLabelBMFont::create(title, "bigFont.fnt");
            heading->limitLabelWidth(116, .34f, .2f); heading->setPosition({64, 32});
            heading->setColor(enabled ? ccColor3B{235, 246, 255} : ccColor3B{115, 124, 139});
            card->addChild(heading);
            auto subtitle = CCLabelBMFont::create(hint, "chatFont.fnt");
            subtitle->limitLabelWidth(118, .43f, .2f); subtitle->setPosition({64, 13});
            subtitle->setColor({150, 175, 195}); card->addChild(subtitle);
            auto item = CCMenuItemExt::createSpriteExtra(card, [action](auto) {
                // Closing a popup may destroy the callback's owning menu.
                auto invoke = action; invoke();
            });
            // Keep touch geometry and resting scale identical; never scale the item.
            item->setScaleMult(1.f); item->setEnabled(enabled);
            m_buttonMenu->addChild(item);
            item->setPosition(m_buttonMenu->convertToNodeSpace(m_mainLayer->convertToWorldSpace(pos)));
        };
        add("1. RECORD", "New practice run", {79, 171}, !active, [this] {
            if (!ensureSession()) return;
            if (!supported()) { FLAlertLayer::create("Unavailable", s.message, "OK")->show(); return; }
            auto key = s.recording.key; s.recording = {}; s.recording.key = key;
            s.state = State::Idle; s.pending = Pending::None; s.checkpoints.clear();
            s.recordedHeld.fill(false); s.physicalHeld.fill(false); s.injectedHeld.fill(false);
            onClose(nullptr);
            resumeAndRun([] { resetTrial(); s.state = State::Record; s.message = "REC - finish the level in practice"; });
        });
        add("2. ANALYZE", "Verify + measure", {215, 171}, !active && s.recording.complete && !s.recording.inputs.empty(), [this] {
            if (!ensureSession()) return;
            if (!supported()) { FLAlertLayer::create("Unavailable", s.message, "OK")->show(); return; }
            onClose(nullptr); resumeAndRun([] { beginAnalysis(); });
        });
        add("3. PLAY", "Circles + sounds", {79, 115}, !active && s.recording.ready(), [this] {
            if (!ensureSession() || !supported()) {
                FLAlertLayer::create("Unavailable", s.message, "OK")->show(); return;
            }
            onClose(nullptr);
            resumeAndRun([] {
                s.state = State::Playback; s.pending = Pending::None; prepareShowcase();
                s.message = "Showcase - valid windows at 240 TPS"; resetTrial();
            });
        });
        add("STOP / SAVE", "Keep partial analysis", {215, 115}, active, [this] {
            if (!ensureSession()) return;
            stop("Stopped. Partial analysis saved; Analyze to resume");
            onClose(nullptr);
            if (auto popup = CFPopup::create()) popup->show();
        });
        add("SETTINGS", "Sounds, circles, scan", {79, 59}, true, [] { openSettingsPopup(Mod::get()); });
        add("HELP", "How to test this mod", {215, 59}, true, [] {
            FLAlertLayer::create("How to use", "<cg>1. Record</c>: starts a NEW run in practice. Finish the level using checkpoints as needed.\n<cy>2. Analyze</c>: verifies the saved run, then replays many tests to measure each input. This can take a long time. Pause to Stop / Save and resume later.\n<cb>3. Play</c>: replays with circles, sounds and the counter. Use it again to replay.\nGray buttons unlock after the previous step. 240 TPS; disable CBF and ignore damage.", "OK")->show();
        });
        auto panel = CCScale9Sprite::create("square02b_001.png");
        panel->setContentSize({118, 160}); panel->setColor({20, 30, 47});
        panel->setOpacity(240); panel->setPosition({351, 115}); m_mainLayer->addChild(panel);
        label("FRAME WINDOWS", {351, 181}, .48f, {111, 219, 235});
        std::array<int, 5> totals{};
        size_t measured = 0;
        for (auto const& window : s.recording.windows) {
            if (window.measured) { ++measured; ++totals[cf::category(window)]; }
        }
        for (int i = 0; i < 5; ++i) {
            label(fmt::format("{}{}F     {}", i + 1, i == 4 ? "+" : "", totals[i]),
                  {351, 158.f - i * 18.f}, .55f, WINDOW_COLORS[i]);
        }
        label(fmt::format("{} / {} measured", measured, s.recording.inputs.size()), {351, 58}, .43f, {175, 192, 211});
        label("240 TPS | GD 2.2081 | Geode 5.11.2 | v0.2.2", {215, 18}, .43f, {165, 183, 204});
        return true;
    }
public:
    static CFPopup* create() { auto p = new CFPopup; if (p->init()) { p->autorelease(); return p; } delete p; return nullptr; }
};
class $modify(CFPause, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();
        auto menu = CCMenu::create();
        menu->setID("click-frames-bottom-menu"_spr);
        menu->setPosition({CCDirector::get()->getWinSize().width / 2, 35});
        addChild(menu, 100);
        auto sprite = ButtonSprite::create("Frames"); sprite->setScale(.7f);
        auto button = CCMenuItemExt::createSpriteExtra(sprite, [](auto) { if (auto popup = CFPopup::create()) popup->show(); });
        button->setScaleMult(1.f); button->setPosition({0, 0}); menu->addChild(button);
    }
};
