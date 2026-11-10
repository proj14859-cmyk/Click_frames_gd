#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/FMODAudioEngine.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include "Timing.hpp"
#include "Pack.hpp"
#include <array>
#include <cmath>
#include <fstream>
#include <map>
#include <sstream>
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
    std::vector<cf::Row> rows; // counter rows for this run (see cf::makeRows)
    std::vector<int> tally;
    std::string message = "Record a practice run";
    std::string folder; // levels/<name>-<id>/ holds this level's runs
    std::string alert, firstFail; // why a run stopped, shown once pause is back
    int shiftIndex = 0, lastPressTick = -1, divergences = 0;
    bool noRetry = false;
    bool background = false, objectsWereVisible = true;
    float previousTimeScale = 1.f;
    bool musicMutedByCf = false;
    float previousMusicVolume = 0.f;
};
// The recorded tick and the replay's injection point can differ by a step or two, so
// verification retries the whole run shifted before it gives up.
constexpr int VERIFY_SHIFTS[] = {0, 1, -1, 2, -2};
constexpr int VERIFY_SHIFT_COUNT = 5;
// Exactly one live PlayLayer owns this session. It is discarded on exit/init.
Session s;
bool hasLiveOwner() {
    auto current = PlayLayer::get();
    return current && s.owner == current && current->m_level && current->m_player1 && current->m_player2;
}
bool running() { return s.state == State::Verify || s.state == State::Analyze || s.state == State::Playback; }
int tick() { return static_cast<int>(s.owner->m_gameState.m_currentProgress); }
int slot(int button, bool p1) { return (p1 ? 0 : 3) + button - 1; }
std::filesystem::path levelDir(std::string const& folder) { return Mod::get()->getSaveDir() / "levels" / folder; }
std::filesystem::path path() { return levelDir(s.folder) / ("run-" + s.recording.key + ".cfr"); }
// v0.2.x kept runs directly in the save dir; move this level's run into its folder.
void migrateOldRun() {
    auto file = path(), old = Mod::get()->getSaveDir() / ("run-" + s.recording.key + ".cfr");
    std::error_code ec;
    if (std::filesystem::exists(file, ec) || !std::filesystem::exists(old, ec)) return;
    std::filesystem::create_directories(file.parent_path(), ec);
    std::filesystem::rename(old, file, ec);
    if (ec) return;
    auto oldBak = old, newBak = file; oldBak += ".bak"; newBak += ".bak";
    std::filesystem::rename(oldBak, newBak, ec);
}
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
// Stage a temp file, keep the previous version as .bak, then swap in the new one.
bool writeSafe(std::filesystem::path const& file, std::string const& text) {
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    auto tmp = file; tmp += ".tmp";
    std::ofstream out(tmp, std::ios::binary); out << text; out.close();
    if (!out) return false;
    if (std::filesystem::exists(file, ec)) {
        auto backup = file; backup += ".bak";
        std::filesystem::remove(backup, ec); ec.clear();
        std::filesystem::rename(file, backup, ec);
        if (ec) return false;
    }
    std::filesystem::rename(tmp, file, ec);
    if (ec) {
        // Restore the old run if the final rename failed after backing it up.
        auto backup = file; backup += ".bak";
        std::error_code restore;
        if (std::filesystem::exists(backup, restore)) std::filesystem::rename(backup, file, restore);
        return false;
    }
    return true;
}
void save() {
    std::ostringstream text; cf::write(text, s.recording);
    if (!writeSafe(path(), text.str())) s.message = "Could not save recording";
}
std::string readAll(std::filesystem::path const& file) {
    std::error_code ec;
    auto bytes = std::filesystem::file_size(file, ec);
    if (ec || bytes > cf::PACK_MAX_BYTES + 16384) return {};
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
// Export: every run of this level into exports/<folder>.cfpack
std::string exportPack() {
    cf::Pack pack; pack.folder = s.folder;
    size_t bytes = 0; std::error_code ec;
    for (auto const& entry : std::filesystem::directory_iterator(levelDir(s.folder), ec)) {
        auto name = entry.path().filename().string();
        if (entry.path().extension() != ".cfr" || !cf::runName(name) || pack.files.size() >= cf::PACK_MAX_FILES) continue;
        auto size = entry.file_size(ec);
        if (ec || size > cf::PACK_MAX_BYTES - bytes) return "Runs are too large to export.";
        auto data = readAll(entry.path());
        if (data.empty()) return "Could not read a recording for export.";
        bytes += data.size();
        pack.files.push_back({name, std::move(data)});
    }
    if (pack.files.empty()) return "Nothing to export: record and analyze this level first.";
    if (bytes > cf::PACK_MAX_BYTES) return "Runs are too large to export.";
    auto out = Mod::get()->getSaveDir() / "exports" / (s.folder + ".cfpack");
    std::filesystem::create_directories(out.parent_path(), ec);
    std::ofstream file(out, std::ios::binary); cf::packWrite(file, pack); file.close();
    if (!file) return "Could not write the export file.";
    return fmt::format("Exported {} run(s) to:\n{}", pack.files.size(), out.string());
}
// Import: every .cfpack in imports/. A pack is checked completely before any file is written.
std::string importPacks() {
    if (s.state == State::Record || running()) return "Stop the current run first.";
    auto dir = Mod::get()->getSaveDir() / "imports";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    std::vector<std::filesystem::path> found;
    for (auto const& entry : std::filesystem::directory_iterator(dir, ec))
        if (entry.path().extension() == ".cfpack") found.push_back(entry.path());
    if (found.empty()) return fmt::format("No .cfpack files found. Copy them to:\n{}", dir.string());
    size_t runs = 0, files = 0, failed = 0;
    for (auto const& file : found) {
        std::istringstream in(readAll(file));
        cf::Pack pack; bool ok = cf::packRead(in, pack);
        for (auto const& f : pack.files) {
            std::istringstream data(f.data); cf::Recording r;
            ok = cf::read(data, f.name.substr(4, f.name.size() - 8), r) && ok;
        }
        if (ok) for (auto const& f : pack.files) ok = writeSafe(levelDir(pack.folder) / f.name, f.data) && ok;
        if (!ok) { ++failed; continue; }
        ++files; runs += pack.files.size();
        auto done = file; done += ".done"; std::filesystem::rename(file, done, ec);
    }
    return fmt::format("Imported {} run(s) from {} file(s).{}", runs, files,
        failed ? fmt::format("\n{} file(s) were invalid and left in place.", failed) : "");
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
void applyBackground(bool on);
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
            if (!s.alert.empty()) { FLAlertLayer::create("Click Frames", s.alert, "OK")->show(); s.alert.clear(); }
        });
    });
}
void stop(std::string message) {
    s.state = s.recording.ready() ? State::Ready : State::Idle;
    s.pending = Pending::None; releaseInjected(); s.message = std::move(message); save();
    applyBackground(false);
}
void resetTrial() {
    if (!hasLiveOwner()) { s.message = "No active level. Reopen Frames from pause."; return; }
    s.resetting = true; releaseInjected(); clearMarkers();
    if (s.state == State::Verify || s.state == State::Analyze) applyBackground(true);
    // Automated runs stay in practice and reset through the normal practice path.
    if (!s.owner->m_isPracticeMode) s.owner->togglePracticeMode(true);
    s.owner->removeAllCheckpoints(); s.owner->resetLevel();
    if (s.musicMutedByCf) FMODAudioEngine::get()->setBackgroundMusicVolume(0.f);
    s.resetting = false; s.next = 0; s.sample = 0; s.lastTick = -1;
    s.watchdogTick = -1; s.stalledSeconds = 0;
    s.comparedSamples = 0; s.lastPressTick = -1; s.divergences = 0;
    fixPracticeTouchPriority();
}
// Verify/Analyze run the level behind a cover: objects hidden (nothing to draw) and the
// game sped up (setting "scan-speed"; 1 = normal). Called every frame, so it undoes itself.
void applyBackground(bool on) {
    auto scheduler = CCDirector::get()->getScheduler();
    if (!hasLiveOwner()) {
        // Scene replacement may already have cleared PlayLayer::get(). Restore
        // global timing without dereferencing the old layer in that case.
        if (!on && s.background) {
            scheduler->setTimeScale(s.previousTimeScale);
            s.background = false;
        }
        if (!on && s.musicMutedByCf) {
            s.musicMutedByCf = false;
            FMODAudioEngine::get()->setBackgroundMusicVolume(s.previousMusicVolume);
        }
        return;
    }
    if (on && !s.background) {
        s.previousTimeScale = scheduler->getTimeScale();
        s.objectsWereVisible = !s.owner->m_objectLayer || s.owner->m_objectLayer->isVisible();
        s.background = true;
        scheduler->setTimeScale(static_cast<float>(Mod::get()->getSettingValue<double>("scan-speed")));
        if (s.owner->m_objectLayer) s.owner->m_objectLayer->setVisible(false);
        s.previousMusicVolume = FMODAudioEngine::get()->getBackgroundMusicVolume();
        s.musicMutedByCf = true;
    } else if (!on && s.background) {
        scheduler->setTimeScale(s.previousTimeScale);
        if (s.owner->m_objectLayer) s.owner->m_objectLayer->setVisible(s.objectsWereVisible);
        s.background = false;
        if (s.musicMutedByCf) {
            s.musicMutedByCf = false;
            FMODAudioEngine::get()->setBackgroundMusicVolume(s.previousMusicVolume);
        }
    }
    if (on) FMODAudioEngine::get()->setBackgroundMusicVolume(0.f);
    auto cover = s.owner->getChildByID("cf-cover");
    if (on && !cover) {
        auto win = CCDirector::get()->getWinSize();
        auto layer = CCLayerColor::create({8, 12, 22, 255}); layer->setID("cf-cover");
        auto text = CCLabelBMFont::create("", "chatFont.fnt"); text->setID("cf-cover-label");
        text->setAlignment(kCCTextAlignmentCenter); text->setPosition({win.width / 2, win.height / 2});
        layer->addChild(text);
        s.owner->addChild(layer, 900);
    } else if (!on && cover) cover->removeFromParent();
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
    label->setString(s.message.c_str());
    // One colored line per row of this run, like "4: 3", "5: 1", "8+: 12"
    auto rowsNode = pl->getChildByID("cf-rows");
    if (s.state != State::Playback) { if (rowsNode) rowsNode->removeFromParent(); }
    else {
        if (!rowsNode || rowsNode->getChildrenCount() != s.rows.size()) {
            if (rowsNode) rowsNode->removeFromParent();
            rowsNode = CCNode::create(); rowsNode->setID("cf-rows"); pl->addChild(rowsNode, 1000);
            float scale = static_cast<float>(Mod::get()->getSettingValue<double>("counter-scale"));
            auto win = CCDirector::get()->getWinSize();
            for (size_t i = 0; i < s.rows.size(); ++i) {
                auto line = CCLabelBMFont::create("", "bigFont.fnt");
                auto c = cf::colorFor(s.rows[i].from);
                line->setTag(static_cast<int>(i)); line->setColor({c.r, c.g, c.b}); line->setScale(.4f * scale);
                line->setAnchorPoint({0, 1}); line->setPosition({10, win.height - 34 - static_cast<float>(i) * 17.f * scale});
                rowsNode->addChild(line);
            }
        }
        for (size_t i = 0; i < s.rows.size() && i < s.tally.size(); ++i)
            if (auto line = typeinfo_cast<CCLabelBMFont*>(rowsNode->getChildByTag(static_cast<int>(i))))
                line->setString(fmt::format("{}: {}", cf::label(s.rows[i]), s.tally[i]).c_str());
    }
    if (auto cover = pl->getChildByID("cf-cover"))
        if (auto big = typeinfo_cast<CCLabelBMFont*>(cover->getChildByID("cf-cover-label")))
            big->setString((s.message + "\n\nPause to stop and save").c_str());
}
void showMs(float ms) {
    auto label = typeinfo_cast<CCLabelBMFont*>(s.owner->getChildByID("cf-ms"));
    if (!label) {
        label = CCLabelBMFont::create("", "bigFont.fnt"); label->setID("cf-ms");
        auto win = CCDirector::get()->getWinSize();
        label->setAnchorPoint({1, 0}); label->setPosition({win.width - 10, 8});
        s.owner->addChild(label, 1000);
    }
    label->setScale(.5f * static_cast<float>(Mod::get()->getSettingValue<double>("counter-scale")));
    label->setString(fmt::format("{:.1f} MS", ms).c_str());
}
void clearMarkers() {
    if (!hasLiveOwner()) return;
    if (auto ms = s.owner->getChildByID("cf-ms")) ms->removeFromParent();
    if (!s.owner->m_objectLayer) return;
    if (auto node = s.owner->m_objectLayer->getChildByID("cf-markers")) node->removeFromParent();
}
void prepareShowcase() {
    s.rows = cf::makeRows(s.recording, static_cast<int>(Mod::get()->getSettingValue<int64_t>("max-rows")));
    s.tally.assign(s.rows.size(), 0); clearMarkers();
}
void marker(size_t index) {
    auto const e = s.recording.inputs[index];
    if (e.button != 1) return;
    auto w = s.recording.windows[index];
    if (!w.measured) return;
    if (auto row = cf::rowOf(s.rows, w); row >= 0 && static_cast<size_t>(row) < s.tally.size()) ++s.tally[row];
    if (e.down) { // time since the previous press, like the ms readout of other frame counters
        if (s.lastPressTick >= 0) showMs((e.tick - s.lastPressTick) * 1000.f / 240.f);
        s.lastPressTick = e.tick;
    }
    if (Mod::get()->getSettingValue<bool>("markers") && s.owner->m_objectLayer) {
        auto root = s.owner->m_objectLayer->getChildByID("cf-markers");
        if (!root) {
            root = CCNode::create(); root->setID("cf-markers"); s.owner->m_objectLayer->addChild(root, 1000);
        }
        auto node = CCNode::create(); node->setPosition({e.x, e.y});
        float scale = static_cast<float>(Mod::get()->getSettingValue<double>("marker-scale"));
        auto rgb = cf::colorFor(w.width()); ccColor3B color3{rgb.r, rgb.g, rgb.b};
        ccColor4F border{color3.r / 255.f, color3.g / 255.f, color3.b / 255.f, 1.f};
        ccColor4F fill{border.r, border.g, border.b, e.down ? .22f : .04f};
        auto circle = CCDrawNode::create(); circle->drawCircle({0, 0}, 18.f * scale, fill, e.down ? 2.5f : 1.5f, border, 48);
        node->addChild(circle);
        auto label = CCLabelBMFont::create(fmt::format("{}{}F{}\n-{} / +{}\n{:.1f}ms", e.down ? "" : "R ", w.width(), w.lowerBound ? "+" : "", w.early, w.late, w.width() * 1000.f / 240.f).c_str(), "bigFont.fnt");
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
    s.trial = s.recording.inputs; s.shiftIndex = 0; s.noRetry = false; s.firstFail.clear(); s.alert.clear();
    log::info("[CF] verify start: {} inputs, end tick {}", s.recording.inputs.size(), s.recording.endTick);
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
    s.message = fmt::format("Analyzing {}% | event {}/{} | offset {:+} | {} tests", 100 * s.target / std::max<size_t>(1, s.recording.inputs.size()), s.target + 1, s.recording.inputs.size(), s.offset, s.tests);
    if (!cf::trial(s.recording.inputs, s.target, s.offset, s.trial)) {
        s.pending = Pending::Fail; return;
    }
    resetTrial();
}
void consume() {
    auto result = s.pending; s.pending = Pending::None;
    if (s.state == State::Verify) {
        auto by = VERIFY_SHIFTS[s.shiftIndex];
        if (result == Pending::Fail) {
            log::info("[CF] verify failed (shift {:+}): {}", by, s.message);
            if (s.firstFail.empty()) s.firstFail = s.message;
            while (!s.noRetry && ++s.shiftIndex < VERIFY_SHIFT_COUNT) {
                if (!cf::shiftAll(s.recording.inputs, VERIFY_SHIFTS[s.shiftIndex], s.trial)) continue;
                s.message = fmt::format("Verifying original run (shift {:+})", VERIFY_SHIFTS[s.shiftIndex]);
                resetTrial(); return;
            }
            s.alert = s.noRetry ? s.message : fmt::format("Verification failed: {}\n\nFirst failure: {}\nTried the whole run shifted by 0, +1, -1, +2 and -2 ticks.", s.message, s.firstFail);
            stop(s.message); queueReturnToPause(); return;
        }
        if (by) { // keep the shift that reproduces the run
            std::vector<cf::Input> moved;
            if (cf::shiftAll(s.recording.inputs, by, moved)) {
                s.recording.inputs = std::move(moved);
                // Windows measured against the old timings cannot be resumed.
                s.recording.windows.assign(s.recording.inputs.size(), {});
            }
        }
        // Trailing releases beyond completion were never replayed: do not analyze
        // them as if the baseline proved they executed successfully.
        s.recording.inputs.resize(s.next);
        s.recording.windows.resize(s.next);
        s.recording.endTick = tick();
        log::info("[CF] verified (shift {:+}, {} position drifts)", by, s.divergences);
        s.recording.verified = true; s.state = State::Analyze; s.target = 0;
        nextEvent(); startCandidate();
    } else if (s.state == State::Analyze) {
        ++s.tests; s.outcomes[s.offset + s.recording.radius] = result == Pending::Pass;
        ++s.offset; startCandidate();
    } else if (s.state == State::Playback) {
        if (result == Pending::Fail) s.alert = s.message;
        stop(result == Pending::Pass ? "Showcase complete. Replay available" : s.message);
        queueReturnToPause();
    }
}
}

#ifndef GEODE_IS_WINDOWS
class $modify(CFAudio, FMODAudioEngine) {
    void setBackgroundMusicVolume(float volume) {
        FMODAudioEngine::setBackgroundMusicVolume(s.musicMutedByCf ? 0.f : volume);
    }
};
#endif

class $modify(CFPlayer, PlayerObject) {
    bool blocksCheckpoint() {
        return hasLiveOwner() && running() &&
            (this == s.owner->m_player1 || this == s.owner->m_player2);
    }
    void tryPlaceCheckpoint() {
        if (blocksCheckpoint()) return;
        PlayerObject::tryPlaceCheckpoint();
    }
#ifndef GEODE_IS_WINDOWS
    void updateCheckpointTest() {
        if (blocksCheckpoint()) return;
        PlayerObject::updateCheckpointTest();
    }
#endif
};

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
                s.message = "Unsupported physics step: use 240 TPS without CBF"; s.pending = Pending::Fail; s.noRetry = true;
                if (s.state == State::Analyze) { s.alert = s.message; stop(s.message); queueReturnToPause(); }
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
        applyBackground(false);
        s = {}; s.owner = layer;
        auto level = layer->m_level;
        s.folder = fmt::format("{}-{}", cf::slug(std::string(level->m_levelName)), level->m_levelID.value());
        s.recording.key = cf::fingerprint(fmt::format("{}:{}:{}", level->m_levelID.value(), std::string(level->m_levelName), std::string(level->m_levelString)));
        migrateOldRun();
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
        if (m_isPaused) { applyBackground(false); hud(); return; }
        applyBackground(s.state == State::Verify || s.state == State::Analyze);
        if (running() && !m_isPaused && s.pending == Pending::None) {
            if (s.watchdogTick == tick()) s.stalledSeconds += dt;
            else { s.watchdogTick = tick(); s.stalledSeconds = 0; }
            if (s.stalledSeconds > 10) { s.message = "Replay stalled for 10 seconds"; s.pending = Pending::Fail; }
        }
        if (s.pending != Pending::None) consume();
        hud();
    }
    void pauseGame(bool unfocused) {
        if (s.owner == this) { s.physicalHeld.fill(false); applyBackground(false); }
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
                    // Only a warning: what matters is that the replay finishes the level.
                    if (!s.divergences++) log::info("[CF] first position drift at tick {}", now);
                }
            }
        }
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
        // A collision request may be ignored by GD (invulnerability, inactive
        // player, reset). Let the engine decide before classifying a trial.
        bool active = s.owner == this && running() && !s.resetting &&
            s.pending == Pending::None && player && !player->m_isDead &&
            (player == m_player1 || (player == m_player2 && m_gameState.m_isDualMode));
        PlayLayer::destroyPlayer(player, object);
        if (active && player->m_isDead && s.owner == this && running() &&
            s.pending == Pending::None) {
            s.message = fmt::format("Replay died at tick {}", tick());
            s.pending = Pending::Fail;
            log::info("[CF] actual death: tick {}, player {}, first input {}", tick(),
                player == m_player1 ? 1 : 2, s.trial.empty() ? -1 : s.trial.front().tick);
        }
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
                if (!valid && s.state != State::Analyze) { // only trailing releases may be left over
                    valid = true;
                    for (size_t i = s.next; i < events.size(); ++i) if (events[i].down) valid = false;
                }
                s.pending = valid ? Pending::Pass : Pending::Fail;
                if (!valid) s.message = "Level finished before all clicks were replayed";
            }
            return;
        }
        PlayLayer::levelComplete();
    }
    void onExit() {
        // onExit is a scene lifecycle notification, not destruction. Reparenting
        // and scene overlays must not erase the current recording/session.
        if (s.owner == this) {
            applyBackground(false);
            if (!s.recording.key.empty()) save();
        }
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
                          std::function<void()> action, float width = 128) {
            auto card = CCScale9Sprite::create("square02b_001.png");
            card->setContentSize({width, 48});
            card->setColor(enabled ? ccColor3B{34, 57, 83} : ccColor3B{35, 39, 49});
            card->setOpacity(245);
            auto heading = CCLabelBMFont::create(title, "bigFont.fnt");
            heading->limitLabelWidth(width - 12, .34f, .2f); heading->setPosition({width / 2, 32});
            heading->setColor(enabled ? ccColor3B{235, 246, 255} : ccColor3B{115, 124, 139});
            card->addChild(heading);
            auto subtitle = CCLabelBMFont::create(hint, "chatFont.fnt");
            subtitle->limitLabelWidth(width - 10, .43f, .2f); subtitle->setPosition({width / 2, 13});
            subtitle->setColor({150, 175, 195}); card->addChild(subtitle);
            auto item = CCMenuItemExt::createSpriteExtra(card, [action](auto) {
                // Closing a popup may destroy the callback's owning menu.
                auto invoke = action; invoke();
            });
            // Keep touch geometry and resting scale identical; never scale the item.
            item->m_scaleMultiplier = 1.f; item->setEnabled(enabled);
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
        add("SETTINGS", "Options", {46, 59}, true, [] { openSettingsPopup(Mod::get()); }, 62);
        add("HELP", "Guide", {114, 59}, true, [] {
            FLAlertLayer::create("How to use", "<cg>1. Record</c>: starts a NEW run in practice. Finish the level using checkpoints as needed.\n<cy>2. Analyze</c>: verifies the saved run, then replays many tests to measure each input. This can take a long time. Pause to Stop / Save and resume later.\n<cb>3. Play</c>: replays with circles, sounds and the counter. Use it again to replay.\n<cp>Export / Import</c>: Export writes this level's runs to a .cfpack file. To load one you received, copy it into the imports folder and press Import.\nGray buttons unlock after the previous step. 240 TPS; disable CBF and ignore damage.", "OK")->show();
        }, 62);
        add("EXPORT", "Share", {182, 59}, !active, [this] {
            if (!ensureSession()) return;
            auto message = exportPack();
            onClose(nullptr);
            FLAlertLayer::create("Export", message, "OK")->show();
        }, 62);
        add("IMPORT", "From file", {250, 59}, !active, [this] {
            if (!ensureSession()) return;
            auto message = importPacks();
            if (hasLiveOwner() && s.state != State::Record && !running()) CFPlay::bindSession(s.owner); // load what was just imported
            onClose(nullptr);
            FLAlertLayer::create("Import", message, "OK")->show();
        }, 62);
        auto panel = CCScale9Sprite::create("square02b_001.png");
        panel->setContentSize({118, 160}); panel->setColor({20, 30, 47});
        panel->setOpacity(240); panel->setPosition({351, 115}); m_mainLayer->addChild(panel);
        label("FRAME WINDOWS", {351, 181}, .48f, {111, 219, 235});
        auto panelRows = cf::makeRows(s.recording, static_cast<int>(Mod::get()->getSettingValue<int64_t>("max-rows")));
        std::vector<int> totals(panelRows.size(), 0);
        size_t measured = 0;
        for (auto const& window : s.recording.windows) {
            if (!window.measured) continue;
            ++measured;
            if (auto row = cf::rowOf(panelRows, window); row >= 0) ++totals[row];
        }
        float step = panelRows.empty() ? 18.f : std::min(18.f, 90.f / static_cast<float>(panelRows.size()));
        for (size_t i = 0; i < panelRows.size(); ++i) {
            auto c = cf::colorFor(panelRows[i].from);
            label(fmt::format("{}:   {}", cf::label(panelRows[i]), totals[i]),
                  {351, 158.f - static_cast<float>(i) * step}, .55f, {c.r, c.g, c.b});
        }
        if (panelRows.empty()) label("Analyze to fill this in", {351, 120}, .43f, {175, 192, 211});
        label(fmt::format("{} / {} measured", measured, s.recording.inputs.size()), {351, 58}, .43f, {175, 192, 211});
        label("240 TPS | GD 2.2081 | Geode 5.11.2 | v0.5.2", {215, 18}, .43f, {165, 183, 204});
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
        button->m_scaleMultiplier = 1.f; button->setPosition({0, 0}); menu->addChild(button);
    }
};
