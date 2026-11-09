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
enum class Pending { None, StartAnalysis, Pass, Fail };
struct Checkpoint { size_t inputs, trace; std::array<bool, 6> held; };
struct Session {
    PlayLayer* owner = nullptr;
    cf::Recording recording;
    State state = State::Idle;
    Pending pending = Pending::None;
    bool injecting = false, resetting = false, restored = false;
    std::array<bool, 6> held{};
    std::array<bool, 6> physicalHeld{};
    std::map<CheckpointObject*, Checkpoint> checkpoints;
    std::vector<cf::Input> trial;
    std::vector<bool> outcomes;
    size_t next = 0, sample = 0, target = 0;
    int offset = 0, tests = 0, lastTick = -1;
    int watchdogTick = -1;
    size_t comparedSamples = 0;
    float stalledSeconds = 0;
    std::array<int, 5> tally{};
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
void release() {
    if (!hasLiveOwner()) return;
    if (!s.owner) {
        s.held.fill(false);
        s.physicalHeld.fill(false);
        return;
    }
    bool twoPlayer = s.owner->m_level && s.owner->m_level->m_twoPlayerMode;
    s.injecting = true;
    for (int b = 1; b <= 3; ++b) {
        if (s.held[slot(b, true)] || s.physicalHeld[slot(b, true)] || running()) {
            s.owner->handleButton(false, b, true);
        }
        if (twoPlayer && (s.held[slot(b, false)] || s.physicalHeld[slot(b, false)] || running())) {
            s.owner->handleButton(false, b, false);
        }
    }
    s.injecting = false;
    s.held.fill(false);
    s.physicalHeld.fill(false);
}
void stop(std::string message) {
    s.state = s.recording.ready() ? State::Ready : State::Idle;
    s.pending = Pending::None; release(); s.message = std::move(message); save();
}
void resetTrial() {
    if (!hasLiveOwner()) { s.message = "No active level. Reopen Frames from pause."; return; }
    s.resetting = true; release();
    // Automated and recorded runs stay in practice mode.
    // Use removeAllCheckpoints() + resetLevel() instead of resetLevelFromStart()
    // so Practice Mode state and UILayer checkpoint buttons remain active.
    if (!s.owner->m_isPracticeMode) s.owner->togglePracticeMode(true);
    s.owner->removeAllCheckpoints();
    s.owner->resetLevel();
    s.resetting = false; s.next = 0; s.sample = 0; s.lastTick = -1;
    s.watchdogTick = -1; s.stalledSeconds = 0;
    s.comparedSamples = 0;
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
    if (s.state == State::Playback)
        text += fmt::format("\n1: {}\n2: {}\n3: {}\n4: {}\n5+: {}", s.tally[0], s.tally[1], s.tally[2], s.tally[3], s.tally[4]);
    label->setString(text.c_str());
}
void marker(size_t index) {
    auto const e = s.recording.inputs[index];
    if (e.button != 1) return;
    auto w = s.recording.windows[index];
    if (!w.measured) return;
    if (e.down) ++s.tally[std::min(w.width(), 5) - 1];
    auto player = e.p1 ? s.owner->m_player1 : s.owner->m_player2;
    if (!player) return;
    auto label = CCLabelBMFont::create(fmt::format("{}{}{}", e.down ? "" : "R ", w.width(), w.lowerBound ? "+" : "").c_str(), "bigFont.fnt");
    label->setScale(static_cast<float>(Mod::get()->getSettingValue<double>("marker-scale")) * .45f);
    label->setColor(e.down ? ccColor3B{255, 220, 70} : ccColor3B{100, 210, 255});
    label->setPosition({0, 40}); player->addChild(label, 100);
    label->runAction(CCSequence::create(CCDelayTime::create(.5f), CCFadeOut::create(.2f), CCRemoveSelf::create(), nullptr));
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
        s.owner->pauseGame(false); return;
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
    if (result == Pending::StartAnalysis) { beginAnalysis(); return; }
    if (s.state == State::Verify) {
        if (result == Pending::Fail) { stop(s.message); s.owner->pauseGame(false); return; }
        s.recording.verified = true; s.state = State::Analyze; s.target = 0;
        nextEvent(); startCandidate();
    } else if (s.state == State::Analyze) {
        ++s.tests; s.outcomes[s.offset + s.recording.radius] = result == Pending::Pass;
        ++s.offset; startCandidate();
    } else if (s.state == State::Playback) {
        stop(result == Pending::Pass ? "Showcase complete. Replay available" : s.message);
        s.owner->pauseGame(false);
    }
}
}

class $modify(CFBase, GJBaseGameLayer) {
    void handleButton(bool down, int button, bool p1) {
        if (static_cast<GJBaseGameLayer*>(s.owner) != this) return GJBaseGameLayer::handleButton(down, button, p1);
        if (button >= 1 && button <= 3 && !s.injecting && !s.resetting) {
            if (!m_level || !m_level->m_twoPlayerMode) p1 = true;
            s.physicalHeld[slot(button, p1)] = down;
        }
        if (running() && !s.injecting) return;
        if (s.state == State::Record && !s.resetting && !s.owner->m_isPaused &&
            !s.owner->m_inResetDelay && !s.owner->m_hasCompletedLevel &&
            s.owner->m_player1 && !s.owner->m_player1->m_isDead &&
            button >= 1 && button <= 3) {
            if (!m_level || !m_level->m_twoPlayerMode) p1 = true;
            auto i = slot(button, p1);
            if (s.held[i] != down) {
                s.recording.inputs.push_back({tick(), down, button, p1});
                s.held[i] = down;
            }
        }
        GJBaseGameLayer::handleButton(down, button, p1);
    }
    void processCommands(float dt, bool half, bool last) {
        if (static_cast<GJBaseGameLayer*>(s.owner) == this && running() && s.pending == Pending::None) {
            bool validStep = std::abs(dt - 1.f / 240.f) <= .0001f || std::abs(dt - 60.f / 240.f) <= .005f;
            if (dt > 0 && !validStep) {
                s.message = "Unsupported physics step: use 240 TPS without CBF"; s.pending = Pending::Fail;
                if (s.state == State::Analyze) { stop(s.message); s.owner->pauseGame(false); }
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
        std::ifstream file(path()); cf::Recording loaded;
        if (cf::read(file, s.recording.key, loaded)) {
            s.recording = std::move(loaded); s.state = s.recording.ready() ? State::Ready : State::Idle;
            s.message = s.recording.ready() ? "Saved showcase available" : "Saved run: Analyze to continue";
        }
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
            if (!s.recording.trace.empty() && s.recording.trace.back().tick == now) {
                s.recording.trace.back() = {now, m_player1->getPositionX(), m_player1->getPositionY(), m_player2->getPositionX(), m_player2->getPositionY()};
            } else {
                s.recording.trace.push_back({now, m_player1->getPositionX(), m_player1->getPositionY(), m_player2->getPositionX(), m_player2->getPositionY()});
            }
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
        if (s.owner == this && s.state == State::Record && cp) {
            s.checkpoints[cp] = {s.recording.inputs.size(), s.recording.trace.size(), s.held};
        }
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
        if (s.owner != this || s.state != State::Record) return;
        auto it = s.checkpoints.find(cp);
        int now = tick();
        if (it == s.checkpoints.end()) {
            while (!s.recording.inputs.empty() && s.recording.inputs.back().tick > now) s.recording.inputs.pop_back();
            while (!s.recording.trace.empty() && s.recording.trace.back().tick > now) s.recording.trace.pop_back();
            s.held.fill(false);
            for (auto const& inp : s.recording.inputs) s.held[slot(inp.button, inp.p1)] = inp.down;
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
        s.held = saved.held; s.restored = true;
        s.lastTick = s.recording.trace.empty() ? -1 : s.recording.trace.back().tick;
    }
    void resetLevel() {
        if (s.owner == this && running() && !s.resetting) {
            s.message = "Unexpected reset during replay"; s.pending = Pending::Fail; return;
        }
        s.restored = false; PlayLayer::resetLevel();
        if (s.owner == this && s.state == State::Record && !s.resetting) {
            if (!s.restored) {
                s.recording.inputs.clear(); s.recording.trace.clear(); s.checkpoints.clear(); s.held.fill(false);
            }
            int now = tick();
            bool twoPlayer = m_level && m_level->m_twoPlayerMode;
            s.resetting = true;
            for (int b = 1; b <= 3; ++b) {
                for (int p = 0; p < (twoPlayer ? 2 : 1); ++p) {
                    bool p1 = (p == 0);
                    auto i = slot(b, p1);
                    bool phys = s.physicalHeld[i];
                    if (s.held[i] != phys) {
                        s.recording.inputs.push_back({now, phys, b, p1});
                        s.held[i] = phys;
                    }
                    if (phys) {
                        handleButton(true, b, p1);
                    }
                }
            }
            s.resetting = false;
            s.lastTick = s.recording.trace.empty() ? -1 : s.recording.trace.back().tick;
        }
    }
    void destroyPlayer(PlayerObject* player, GameObject* object) {
        if (s.owner == this && running()) { s.message = fmt::format("Replay died at tick {}", tick()); s.pending = Pending::Fail; return; }
        PlayLayer::destroyPlayer(player, object);
    }
    void levelComplete() {
        if (s.owner == this && s.state == State::Record) {
            s.recording.complete = true; s.recording.endTick = tick();
            s.state = State::Idle; save(); s.pending = Pending::StartAnalysis; return;
        }
        if (s.owner == this && running()) {
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
    bool init() {
        if (!ensureSession()) return false;
        if (!Popup::init(340, 280)) return false;
        setTitle("Click Frames");
        auto status = CCLabelBMFont::create(s.message.c_str(), "chatFont.fnt");
        status->limitLabelWidth(310, .6f, .2f);
        m_mainLayer->addChildAtPosition(status, Anchor::Top, {0, -45});
        size_t measured = 0, perfect = 0;
        for (size_t i = 0; i < s.recording.windows.size(); ++i) {
            auto w = s.recording.windows[i];
            if (w.measured) ++measured;
            if (w.measured && !w.lowerBound && w.width() == 1 && s.recording.inputs[i].down) ++perfect;
        }
        auto summary = CCLabelBMFont::create(fmt::format("Measured: {} | 1-frame presses: {}", measured, perfect).c_str(), "chatFont.fnt");
        summary->setScale(.55f); m_mainLayer->addChildAtPosition(summary, Anchor::Top, {0, -65});
        auto add = [this](char const* title, float y, std::function<void()> action) {
            auto item = CCMenuItemExt::createSpriteExtra(ButtonSprite::create(title), [action](auto) { action(); });
            item->setScale(.7f); m_buttonMenu->addChildAtPosition(item, Anchor::Center, {0, y});
        };
        add("Record", 50, [this] {
            if (!ensureSession()) return;
            if (!supported()) { FLAlertLayer::create("Unavailable", s.message, "OK")->show(); return; }
            auto key = s.recording.key; s.recording = {}; s.recording.key = key;
            s.state = State::Idle; s.pending = Pending::None; s.checkpoints.clear();
            onClose(nullptr); resumeGame();
            resetTrial(); s.state = State::Record; s.message = "REC - practice";
        });
        add("Analyze / Resume", 15, [this] {
            if (!ensureSession()) return;
            if (!s.recording.complete) { FLAlertLayer::create("Incomplete run", "Finish a practice run first.", "OK")->show(); return; }
            if (!supported()) { FLAlertLayer::create("Unavailable", s.message, "OK")->show(); return; }
            onClose(nullptr); resumeGame();
            beginAnalysis();
        });
        add("Play / Replay", -20, [this] {
            if (!ensureSession()) return;
            if (!s.recording.ready()) { FLAlertLayer::create("Not ready", "Complete and verify the analysis first.", "OK")->show(); return; }
            if (!supported()) { FLAlertLayer::create("Unavailable", s.message, "OK")->show(); return; }
            onClose(nullptr); resumeGame();
            s.state = State::Playback; s.pending = Pending::None; s.tally.fill(0);
            s.message = "Showcase - valid windows at 240 TPS"; resetTrial();
        });
        add("Stop / Save", -55, [this] { if (!ensureSession()) return; stop("Stopped. Partial work saved"); onClose(nullptr); });
        add("Settings", -90, [] { openSettingsPopup(Mod::get()); });
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
        auto button = CCMenuItemExt::createSpriteExtra(ButtonSprite::create("Frames"), [](auto) { if (auto popup = CFPopup::create()) popup->show(); });
        button->setScale(.7f); button->setPosition({0, 0}); menu->addChild(button);
    }
};
 
