#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/FMODAudioEngine.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include "Timing.hpp"
#include "Storage.hpp"
#include "Pack.hpp"
#include "Analysis.hpp"
#include "Editor.hpp"
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
    float lastIntervalMs = -1.f;
    bool noRetry = false;
    bool background = false, objectsWereVisible = true;
    float previousTimeScale = 1.f;
    bool musicMutedByCf = false;
    float previousMusicVolume = 0.f;
    std::map<std::string, std::string> savedViews;
    cf::ContinuousSearch search;
    std::vector<cf::Landing> referenceLandings;
    std::array<size_t, 2> activeJump{size_t(-1), size_t(-1)};
    std::array<bool, 2> airborne{};
    bool localLandingPassed = false;
    Ref<CheckpointObject> snapshot;
    bool internalCheckpoint = false, preparingSnapshot = false, validatingSnapshot = false, snapshotChecked = false;
    bool fullRestartRetry = false;
    int snapshotTick = -1, snapshotEndTick = 0;
    size_t snapshotNext = 0, snapshotCompared = 0;
    std::array<bool, 6> snapshotHeld{};
    gd::vector<PlayerButtonCommand> snapshotButtons;
    gd::vector<RecordButtonCommand> snapshotRecordedButtons, snapshotReplayButtons;
    uint64_t snapshotRandomSeed = 0, snapshotReplaySeed = 0;
    double snapshotExtraDelta = 0;
    int snapshotStep = 0, snapshotRecordedSize = 0, snapshotResumeTimer = 0;
    std::map<int, cf::Sample> snapshotTrace;
    cf::Predictions predictions;
    std::vector<cf::Sample> candidateTrace;
    int drawnPreview = -1;
    cf::VisualEdits visual;
    size_t nextVisual = 0;
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
std::filesystem::path levelDir(std::string const& folder) { return Mod::get()->getSaveDir() / "recordings" / folder; }
std::filesystem::path path() { return levelDir(s.folder) / s.recording.key / "capture.cfr"; }
// Copy and validate legacy captures; never delete the old data during migration.
void migrateOldRun() {
    auto file = path();
    std::error_code ec;
    if (std::filesystem::exists(file, ec)) return;
    auto name = "run-" + s.recording.key + ".cfr";
    for (auto const& old : {Mod::get()->getSaveDir() / "levels" / s.folder / name,
                           Mod::get()->getSaveDir() / name}) {
        std::ifstream in(old); cf::Recording checked;
        if (!cf::read(in, s.recording.key, checked)) continue;
        std::filesystem::create_directories(file.parent_path(), ec);
        if (ec) { s.message = "Could not create recording folder"; return; }
        auto tmp = file; tmp += ".tmp";
        std::filesystem::copy_file(old, tmp, std::filesystem::copy_options::overwrite_existing, ec);
        if (!ec) std::filesystem::rename(tmp, file, ec);
        if (ec) s.message = "Could not migrate recording; original preserved";
        return;
    }
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
    if (!writeSafe(path(), text.str())) { s.message = "Could not save recording"; return; }
    auto view = [](std::string const& name, std::string const& data) {
        auto digest = cf::fingerprint(data);
        auto it = s.savedViews.find(name);
        if (it != s.savedViews.end() && it->second == digest) return;
        if (writeSafe(path().parent_path() / name, data)) s.savedViews[name] = digest;
        else s.message = "Capture saved; could not update report files";
    };
    view("metadata.json", cf::metadataJson(s.recording));
    view("inputs.csv", cf::inputsCsv(s.recording));
    view("trace.csv", cf::traceCsv(s.recording));
    view("windows.csv", cf::windowsCsv(s.recording));
    view("scan-report.txt", cf::scanReport(s.recording, s.message));
    std::ostringstream predictions;
    cf::writePredictions(predictions, cf::fingerprint(text.str()), s.predictions);
    view("prediction-trails.txt", predictions.str());
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
    std::map<std::string, std::filesystem::path> captures;
    auto legacy = Mod::get()->getSaveDir() / "levels" / s.folder;
    for (auto const& entry : std::filesystem::directory_iterator(legacy, ec)) {
        auto name = entry.path().filename().string();
        if (cf::runName(name)) captures[name] = entry.path();
    }
    ec.clear();
    // Keep the existing wire format: pack members still use run-<key>.cfr.
    for (auto const& entry : std::filesystem::directory_iterator(levelDir(s.folder), ec)) {
        auto name = "run-" + entry.path().filename().string() + ".cfr";
        if (cf::runName(name)) captures[name] = entry.path() / "capture.cfr";
    }
    ec.clear();
    for (auto const& [name, capture] : captures) {
        if (pack.files.size() >= cf::PACK_MAX_FILES) return "Too many recordings for one pack.";
        auto size = std::filesystem::file_size(capture, ec);
        if (ec || size > cf::PACK_MAX_BYTES - bytes) return "Runs are too large to export.";
        auto data = readAll(capture);
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
        if (ok) for (auto const& f : pack.files) {
            auto key = f.name.substr(4, f.name.size() - 8);
            ok = writeSafe(levelDir(pack.folder) / key / "capture.cfr", f.data) && ok;
        }
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
    s.snapshot = nullptr; s.snapshotChecked = false;
    s.preparingSnapshot = false; s.validatingSnapshot = false;
    s.fullRestartRetry = false;
    s.snapshotTrace.clear();
    s.pending = Pending::None; releaseInjected(); s.message = std::move(message); save();
    applyBackground(false);
}
void resetTrial() {
    if (!hasLiveOwner()) { s.message = "No active level. Reopen Frames from pause."; return; }
    s.resetting = true; releaseInjected(); clearMarkers();
    if (s.state == State::Verify || s.state == State::Analyze) applyBackground(true);
    // Automated runs stay in practice and reset through the normal practice path.
    if (!s.owner->m_isPracticeMode) s.owner->togglePracticeMode(true);
    s.owner->removeAllCheckpoints();
    bool restoring = s.snapshot && (s.validatingSnapshot || s.snapshotChecked) && !s.preparingSnapshot && !s.fullRestartRetry;
    if (restoring) {
        s.internalCheckpoint = true;
        s.owner->storeCheckpoint(s.snapshot.data());
        s.internalCheckpoint = false;
    }
    s.owner->resetLevel();
    if (s.musicMutedByCf) FMODAudioEngine::get()->setBackgroundMusicVolume(0.f);
    s.resetting = false; s.next = 0; s.sample = 0; s.lastTick = -1;
    if (restoring) {
        if (tick() != s.snapshotTick) {
            log::warn("[CF] snapshot restore tick mismatch; disabling native cache");
            s.snapshot = nullptr; s.snapshotChecked = false;
            s.resetting = true; s.owner->removeAllCheckpoints(); s.owner->resetLevel(); s.resetting = false;
        } else {
            s.next = s.snapshotNext; s.injectedHeld = s.snapshotHeld;
            s.owner->m_queuedButtons = s.snapshotButtons;
            s.owner->m_queuedRecordedButtons = s.snapshotRecordedButtons;
            s.owner->m_queuedReplayButtons = s.snapshotReplayButtons;
            s.owner->m_randomSeed = s.snapshotRandomSeed; s.owner->m_replayRandSeed = s.snapshotReplaySeed;
            s.owner->m_extraDelta = s.snapshotExtraDelta; s.owner->m_currentStep = s.snapshotStep;
            s.owner->m_queuedRecordedButtonsSize = s.snapshotRecordedSize; s.owner->m_resumeTimer = s.snapshotResumeTimer;
        }
    }
    s.watchdogTick = -1; s.stalledSeconds = 0;
    s.comparedSamples = 0; s.lastPressTick = -1; s.lastIntervalMs = -1.f; s.divergences = 0;
    s.activeJump.fill(size_t(-1)); s.airborne.fill(false); s.localLandingPassed = false;
    if (s.state == State::Verify) s.referenceLandings.assign(s.recording.inputs.size(), {});
    s.candidateTrace.clear(); s.snapshotCompared = 0;
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
    label->setString(s.state == State::Playback ? "" : s.message.c_str());
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
                line->setAnchorPoint({0, 1}); line->setPosition({10, win.height - 10 - static_cast<float>(i) * 17.f * scale});
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
        label->setAnchorPoint({0, 0});
        s.owner->addChild(label, 1000);
    }
    label->setScale(.5f * static_cast<float>(Mod::get()->getSettingValue<double>("counter-scale")));
    auto win = CCDirector::get()->getWinSize();
    float x = static_cast<float>(Mod::get()->getSettingValue<double>("ms-x")) / 100.f;
    float y = static_cast<float>(Mod::get()->getSettingValue<double>("ms-y")) / 100.f;
    label->setString(fmt::format("{:.2f} ms", ms).c_str());
    auto size = label->getScaledContentSize();
    label->setPosition({x * std::max(0.f, win.width - size.width), y * std::max(0.f, win.height - size.height)});
    label->setColor(tick() - s.lastPressTick < 60 ? ccColor3B{130, 245, 225} : ccColor3B{255, 255, 255});
}
void clearMarkers() {
    if (!hasLiveOwner()) return;
    if (auto ms = s.owner->getChildByID("cf-ms")) ms->removeFromParent();
    if (!s.owner->m_objectLayer) return;
    if (auto node = s.owner->m_objectLayer->getChildByID("cf-markers")) node->removeFromParent();
    if (auto node = s.owner->m_objectLayer->getChildByID("cf-prediction")) node->removeFromParent();
    s.drawnPreview = -1;
}
void prepareShowcase() {
    cf::Recording visual;
    if (s.visual.enabled) visual = cf::visualCounter(s.visual);
    auto const& counter = s.visual.enabled ? visual : s.recording;
    s.rows = cf::makeRows(counter, static_cast<int>(Mod::get()->getSettingValue<int64_t>("max-rows")));
    s.nextVisual = 0;
    s.tally.assign(s.rows.size(), 0); clearMarkers();
}
cf::Rgb visualColor(cf::VisualMarker const& m) {
    static constexpr cf::Rgb colors[] = {{255, 255, 255}, {255, 75, 75}, {255, 225, 70}, {80, 235, 245}, {105, 245, 125}, {255, 255, 255}};
    return m.color == 0 ? cf::colorFor(m.width) : colors[std::clamp(m.color, 1, 5)];
}
void visualMarker(cf::VisualMarker const& m) {
    cf::Window window{m.width - 1, 0, true, false};
    if (m.count) if (int row = cf::rowOf(s.rows, window); row >= 0 && size_t(row) < s.tally.size()) ++s.tally[row];
    if (Mod::get()->getSettingValue<bool>("markers") && s.owner->m_objectLayer) {
        auto root = s.owner->m_objectLayer->getChildByID("cf-markers");
        if (!root) { root = CCNode::create(); root->setID("cf-markers"); s.owner->m_objectLayer->addChild(root, 1000); }
        auto position = cf::positionAt(s.recording.trace, m.tick, m.p1);
        auto node = CCNode::create(); node->setPosition({position.x, position.y}); root->addChild(node);
        auto color = visualColor(m);
        ccColor4F border{color.r / 255.f, color.g / 255.f, color.b / 255.f, 1.f};
        float scale = m.scale * static_cast<float>(Mod::get()->getSettingValue<double>("marker-scale"));
        auto circle = CCDrawNode::create(); circle->drawCircle({0, 0}, 18.f * scale, {0, 0, 0, 0}, 2.f, border, 48); node->addChild(circle);
        auto label = CCLabelBMFont::create(fmt::format("{} F", m.width).c_str(), "bigFont.fnt");
        label->setScale(.28f * scale); label->setColor({color.r, color.g, color.b}); label->setPosition({0, 27.f * scale}); node->addChild(label);
    }
    if (m.sound && Mod::get()->getSettingValue<bool>("sounds")) {
        auto sound = m.width > 20 ? "frame-wide.wav" : fmt::format("frame-{}.wav", m.width);
        FMODAudioEngine::get()->playEffect(fmt::format("{}/{}", Mod::get()->getID(), sound));
    }
}
void marker(size_t index) {
    auto const e = s.recording.inputs[index];
    if (e.button == 1 && e.down) {
        if (s.lastPressTick >= 0) s.lastIntervalMs = (e.tick - s.lastPressTick) * 1000.f / 240.f;
        s.lastPressTick = e.tick;
    }
    if (s.visual.enabled) return;
    if (!cf::counted(e)) return;
    auto w = s.recording.windows[index];
    if (!w.measured) return;
    if (auto row = cf::rowOf(s.rows, w); row >= 0 && static_cast<size_t>(row) < s.tally.size()) ++s.tally[row];
    if (Mod::get()->getSettingValue<bool>("markers") && s.owner->m_objectLayer) {
        auto root = s.owner->m_objectLayer->getChildByID("cf-markers");
        if (!root) {
            root = CCNode::create(); root->setID("cf-markers"); s.owner->m_objectLayer->addChild(root, 1000);
        }
        auto node = CCNode::create(); node->setPosition({e.x, e.y});
        float scale = static_cast<float>(Mod::get()->getSettingValue<double>("marker-scale"));
        auto rgb = cf::colorFor(w.width()); ccColor3B color3{rgb.r, rgb.g, rgb.b};
        ccColor4F border{color3.r / 255.f, color3.g / 255.f, color3.b / 255.f, 1.f};
        ccColor4F fill{0, 0, 0, 0};
        auto circle = CCDrawNode::create(); circle->drawCircle({0, 0}, 18.f * scale, fill, 2.f, border, 48);
        node->addChild(circle);
        auto label = CCLabelBMFont::create(fmt::format("{}{} F", w.width(), w.lowerBound ? "+" : "").c_str(), "bigFont.fnt");
        label->setAlignment(kCCTextAlignmentCenter); label->setScale(.28f * scale);
        label->setColor(color3); label->setPosition({0, 27.f * scale}); node->addChild(label, 1);
        root->addChild(node);
    }
    if (Mod::get()->getSettingValue<bool>("sounds")) {
        auto sound = w.width() > 20 ? "frame-wide.wav" : fmt::format("frame-{}.wav", w.width());
        FMODAudioEngine::get()->playEffect(fmt::format("{}/{}", Mod::get()->getID(), sound));
    }
}
std::string inputBackend() {
    if (auto cbf = Loader::get()->getLoadedMod("syzzi.click_between_frames")) {
#ifdef GEODE_IS_WINDOWS
        if (cbf->getSettingValue<bool>("physics-bypass")) return "unsupported-bypass";
#endif
        if (cbf->getSettingValue<bool>("soft-toggle")) return "vanilla240";
        return cbf->getSettingValue<bool>("click-on-steps") ? "cos240" : "unsupported-cbf";
    }
    if (Loader::get()->isModLoaded("zmx.cbf-lite")) return "unsupported-cbf-lite";
    return "vanilla240";
}
void predictionPreview() {
    if (!s.owner->m_objectLayer) return;
    int selected = -1;
    if (!s.visual.enabled && Mod::get()->getSettingValue<bool>("trajectory-preview")) {
        for (auto const& [index, branches] : s.predictions) {
            if (index >= s.recording.inputs.size()) continue;
            int delta = s.recording.inputs[index].tick - tick();
            if (delta >= -240 && delta <= 240) { selected = static_cast<int>(index); break; }
        }
    }
    if (selected == s.drawnPreview) return;
    if (auto old = s.owner->m_objectLayer->getChildByID("cf-prediction")) old->removeFromParent();
    s.drawnPreview = selected;
    if (selected < 0) return;
    auto draw = CCDrawNode::create(); draw->setID("cf-prediction");
    s.owner->m_objectLayer->addChild(draw, 999);
    bool p1 = s.recording.inputs[selected].p1;
    for (auto const& branch : s.predictions[selected]) {
        ccColor4F color = branch.pass ? ccColor4F{.2f, 1.f, .5f, .7f} : ccColor4F{1.f, .25f, .2f, .7f};
        for (size_t i = 1; i < branch.points.size(); ++i) {
            auto a = branch.points[i - 1], b = branch.points[i];
            draw->drawSegment(p1 ? CCPoint{a.x, a.y} : CCPoint{a.x2, a.y2},
                p1 ? CCPoint{b.x, b.y} : CCPoint{b.x2, b.y2}, 1.f, color);
        }
        if (!branch.pass && !branch.points.empty() && branch.endTick - branch.points.back().tick <= 4) {
            auto last = branch.points.back(); CCPoint p = p1 ? CCPoint{last.x, last.y} : CCPoint{last.x2, last.y2};
            draw->drawSegment(p + CCPoint{-4, -4}, p + CCPoint{4, 4}, 1.5f, color);
            draw->drawSegment(p + CCPoint{-4, 4}, p + CCPoint{4, -4}, 1.5f, color);
        }
    }
}
bool supported() {
    if (!hasLiveOwner()) { s.message = "No active level. Reopen Frames from pause."; return false; }
    if (s.owner->m_isPlatformer) { s.message = "Platformer is not supported"; return false; }
    if (s.owner->m_isIgnoreDamageEnabled) { s.message = "Disable ignore damage before recording or analysis"; return false; }
    auto backend = inputBackend();
    if (backend != "vanilla240" && backend != "cos240") {
        s.message = "Use vanilla or CBF Click on Steps; fractional CBF and physics bypass have no precise replay adapter"; return false;
    }
    return true;
}
void beginAnalysis() {
    if (!s.recording.complete || s.recording.inputs.empty() || !supported()) return;
    int radius = static_cast<int>(Mod::get()->getSettingValue<int64_t>("scan-radius"));
    int policy = Mod::get()->getSettingValue<bool>("same-jump") ? 1 : 0;
    bool havePreview = std::any_of(s.predictions.begin(), s.predictions.end(), [](auto const& pair) {
        return std::any_of(pair.second.begin(), pair.second.end(), [](auto const& b) { return !b.points.empty(); });
    });
    bool regenerate = s.recording.ready() && !havePreview && Mod::get()->getSettingValue<bool>("trajectory-preview");
    bool changed = s.recording.radius != radius || s.recording.analysisPolicy != policy || regenerate;
    if (changed) { s.recording.windows.clear(); s.predictions.clear(); }
    s.recording.analysisPolicy = policy;
    if (s.recording.backend != inputBackend()) { s.message = "Input mode changed. Restore the recording's vanilla/CoS mode first."; return; }
    s.snapshot = nullptr; s.snapshotChecked = false;
    s.preparingSnapshot = false; s.validatingSnapshot = false;
    s.referenceLandings.assign(s.recording.inputs.size(), {});
    s.recording.radius = radius;
    s.recording.windows.resize(s.recording.inputs.size()); s.recording.verified = false;
    s.state = State::Verify; s.pending = Pending::None; s.tests = 0;
    s.trial = s.recording.inputs; s.shiftIndex = 0; s.noRetry = false; s.firstFail.clear(); s.alert.clear();
    log::info("[CF] verify start: {} inputs, end tick {}", s.recording.inputs.size(), s.recording.endTick);
    s.message = "Verifying original run (240 TPS)"; resetTrial();
}
void nextEvent() {
    while (s.target < s.recording.inputs.size() &&
        (!cf::counted(s.recording.inputs[s.target]) || s.recording.windows[s.target].measured)) ++s.target;
    if (s.target == s.recording.inputs.size()) {
        stop("Analysis complete. Pause > Frames > Play");
        queueReturnToPause(); return;
    }
    s.search.reset(s.recording.radius); s.offset = s.search.offset;
    s.snapshot = nullptr; s.snapshotChecked = false; s.snapshotTrace.clear();
    s.fullRestartRetry = false;
    s.preparingSnapshot = Mod::get()->getSettingValue<bool>("native-states") &&
        s.recording.inputs[s.target].tick > s.recording.radius + 24;
    s.validatingSnapshot = false;
    if (s.preparingSnapshot) s.offset = 0;
}
void startCandidate() {
    if (s.state != State::Analyze) return;
    if (s.search.done && !s.preparingSnapshot && !s.validatingSnapshot) {
        s.recording.windows[s.target] = s.search.window();
        save(); ++s.target; nextEvent();
        if (s.state != State::Analyze) return;
    }
    s.candidateTrace.clear();
    if (s.preparingSnapshot || s.validatingSnapshot) {
        s.trial = s.recording.inputs;
        s.message = s.preparingSnapshot ? "Capturing native state (baseline)" : "Validating native-state round trip";
        resetTrial(); return;
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
                s.predictions.clear();
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
        if (s.preparingSnapshot) {
            s.preparingSnapshot = false;
            if (result == Pending::Fail) { stop("Baseline changed while capturing state; analyze again without native states"); queueReturnToPause(); return; }
            s.snapshotEndTick = tick();
            if (s.snapshot) { s.validatingSnapshot = true; startCandidate(); return; }
        } else if (s.validatingSnapshot) {
            s.validatingSnapshot = false;
            s.snapshotChecked = s.snapshot && result == Pending::Pass && tick() == s.snapshotEndTick && s.snapshotCompared > 0;
            if (!s.snapshotChecked) { s.snapshot = nullptr; log::warn("[CF] native-state validation failed; full restart fallback"); }
        } else {
            bool pass = result == Pending::Pass;
            if (!pass && s.snapshotChecked && !s.fullRestartRetry) {
                s.fullRestartRetry = true; startCandidate(); return;
            }
            if (s.fullRestartRetry && pass) { s.snapshot = nullptr; s.snapshotChecked = false; }
            s.fullRestartRetry = false;
            ++s.tests;
            auto& branches = s.predictions[s.target];
            bool exists = std::any_of(branches.begin(), branches.end(), [](auto const& b) { return b.pass; });
            if (!pass) exists = std::any_of(branches.begin(), branches.end(), [](auto const& b) { return !b.pass && (b.offset < 0) == (s.offset < 0); });
            size_t points = 0;
            for (auto const& [event, values] : s.predictions) for (auto const& b : values) points += b.points.size();
            if (!exists && !s.candidateTrace.empty() && points + s.candidateTrace.size() <= cf::MAX_PREDICTION_POINTS)
                branches.push_back({s.offset, tick(), pass, s.candidateTrace});
            s.search.accept(pass);
        }
        s.offset = s.search.offset; startCandidate();
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
                int mode = !player ? -1 : player->m_isShip ? 1 : player->m_isBall ? 2 : player->m_isBird ? 3 :
                    player->m_isDart ? 4 : player->m_isRobot ? 5 : player->m_isSpider ? 6 : player->m_isSwing ? 7 : 0;
                s.recording.inputs.push_back({tick(), down, button, p1, position.x, position.y, mode});
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
                if ((s.state == State::Verify || s.state == State::Analyze) && e.button == 1 && e.down) {
                    auto player = e.p1 ? s.owner->m_player1 : s.owner->m_player2;
                    int p = e.p1 ? 0 : 1;
                    bool jumpMode = player && !player->m_isShip && !player->m_isBall && !player->m_isBird &&
                        !player->m_isDart && !player->m_isSpider && !player->m_isSwing;
                    auto identity = e.sourceIndex == size_t(-1) ? index : e.sourceIndex;
                    s.activeJump[p] = jumpMode ? identity : size_t(-1); s.airborne[p] = false;
                }
                handleButton(e.down, e.button, e.p1);
                if (s.state == State::Playback) marker(index);
            }
            if (s.state == State::Playback && s.visual.enabled) {
                while (s.nextVisual < s.visual.markers.size() && s.visual.markers[s.nextVisual].tick <= tick())
                    visualMarker(s.visual.markers[s.nextVisual++]);
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
        // Android's gd::vector has an explicit default constructor; use value
        // initialization rather than aggregate copy-list initialization.
        s = Session(); s.owner = layer;
        auto level = layer->m_level;
        s.folder = fmt::format("{}-{}", cf::slug(std::string(level->m_levelName)), level->m_levelID.value());
        s.recording.key = cf::fingerprint(fmt::format("{}:{}:{}", level->m_levelID.value(), std::string(level->m_levelName), std::string(level->m_levelString)));
        migrateOldRun();
        auto recordingPath = path();
        std::ifstream file(recordingPath); cf::Recording loaded;
        if (cf::read(file, s.recording.key, loaded)) {
            s.recording = std::move(loaded); s.state = s.recording.ready() ? State::Ready : State::Idle;
            int policy = Mod::get()->getSettingValue<bool>("same-jump") ? 1 : 0;
            if (s.recording.analysisPolicy != policy) {
                s.recording.windows.clear(); s.recording.verified = false; s.state = State::Idle;
            }
            s.message = s.recording.ready() ? "Saved showcase available" : "Saved run: Analyze to continue";
            std::ifstream predictions(path().parent_path() / "prediction-trails.txt");
            cf::readPredictions(predictions, cf::fingerprint(readAll(path())), s.recording.inputs.size(), s.predictions);
            if (!s.recording.trace.empty()) {
                std::istringstream edits(readAll(path().parent_path() / "showcase-edits.json"));
                cf::readEdits(edits, s.recording.key, cf::fingerprint(cf::traceCsv(s.recording)),
                    s.recording.trace.front().tick, s.recording.trace.back().tick, s.visual);
            }
            if (s.visual.enabled && s.recording.complete) s.message = "Edited showcase available (visual values)";
            save(); // Regenerate derived files on opening imported or migrated captures.
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
            if (inputBackend() != s.recording.backend) {
                stop("Input mode changed during replay. Restore vanilla/CoS mode and retry."); queueReturnToPause(); return;
            }
            if (s.watchdogTick == tick()) s.stalledSeconds += dt;
            else { s.watchdogTick = tick(); s.stalledSeconds = 0; }
            if (s.stalledSeconds > 10) { s.message = "Replay stalled for 10 seconds"; s.pending = Pending::Fail; }
        }
        if (s.pending != Pending::None) consume();
        hud();
        if (s.state == State::Playback && s.lastIntervalMs >= 0) showMs(s.lastIntervalMs);
        if (s.state == State::Playback) predictionPreview();
    }
    void pauseGame(bool unfocused) {
        if (s.owner == this) { s.physicalHeld.fill(false); applyBackground(false); }
        PlayLayer::pauseGame(unfocused);
    }
    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        if (s.owner != this || s.resetting || s.pending != Pending::None || !m_player1 || !m_player2) return;
        int now = tick();
        if (s.state == State::Verify || s.state == State::Analyze) {
            auto point = cf::Sample{now, m_player1->getPositionX(), m_player1->getPositionY(), m_player2->getPositionX(), m_player2->getPositionY()};
            if (s.state == State::Analyze) {
                int eventTick = s.recording.inputs[s.target].tick;
                if (!s.preparingSnapshot && !s.validatingSnapshot && now >= eventTick - 240 && now <= eventTick + 480 &&
                    s.candidateTrace.size() < 256 && (s.candidateTrace.empty() || now > s.candidateTrace.back().tick))
                    s.candidateTrace.push_back(point);
                int earliest = eventTick - s.recording.radius;
                if (s.preparingSnapshot && !s.snapshot && now < earliest && now >= earliest - 24) {
                    auto checkpoint = createCheckpoint();
                    if (checkpoint) {
                        s.snapshot = checkpoint; s.snapshotTick = now; s.snapshotNext = s.next; s.snapshotHeld = s.injectedHeld;
                        s.snapshotButtons = m_queuedButtons;
                        s.snapshotRecordedButtons = m_queuedRecordedButtons; s.snapshotReplayButtons = m_queuedReplayButtons;
                        s.snapshotRandomSeed = m_randomSeed; s.snapshotReplaySeed = m_replayRandSeed;
                        s.snapshotExtraDelta = m_extraDelta; s.snapshotStep = m_currentStep;
                        s.snapshotRecordedSize = m_queuedRecordedButtonsSize; s.snapshotResumeTimer = m_resumeTimer;
                    }
                }
                if (s.preparingSnapshot && s.snapshot && s.snapshotTrace.size() < 8192) s.snapshotTrace[now] = point;
                if (s.validatingSnapshot) {
                    auto it = s.snapshotTrace.find(now);
                    if (it != s.snapshotTrace.end()) {
                        ++s.snapshotCompared;
                        auto reference = it->second;
                        if (std::abs(reference.x - point.x) > .5f || std::abs(reference.y - point.y) > .5f ||
                            std::abs(reference.x2 - point.x2) > .5f || std::abs(reference.y2 - point.y2) > .5f) {
                            s.message = "Native state diverged; using full restart"; s.pending = Pending::Fail;
                        }
                    }
                }
            }
            for (int p = 0; p < 2; ++p) {
                auto index = s.activeJump[p];
                if (index >= s.referenceLandings.size()) continue;
                auto player = p == 0 ? m_player1 : m_player2;
                if (!player->m_isOnGround) { s.airborne[p] = true; continue; }
                if (!s.airborne[p]) continue;
                cf::Landing landing{true, now, reinterpret_cast<uintptr_t>(player->m_lastGroundObject), player->getPositionX(), player->getPositionY()};
                if (s.state == State::Verify) s.referenceLandings[index] = landing;
                else if (!s.preparingSnapshot && !s.validatingSnapshot && index == s.target && s.recording.analysisPolicy == 1 &&
                         s.referenceLandings[index].known) {
                    s.localLandingPassed = cf::sameLanding(s.referenceLandings[index], landing, 60.f);
                    if (!s.localLandingPassed) { s.message = "Candidate landed outside the reference platform/corridor"; s.pending = Pending::Fail; }
                }
                s.activeJump[p] = size_t(-1);
            }
        }
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
        if (s.owner == this && running() && !s.internalCheckpoint) return;
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
                if (s.state == State::Analyze && !s.preparingSnapshot && !s.validatingSnapshot && s.recording.analysisPolicy == 1 &&
                    s.target < s.referenceLandings.size() && s.referenceLandings[s.target].known)
                    valid = valid && s.localLandingPassed;
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

class CFEditor : public Popup {
    cf::EditHistory m_history;
    cf::VisualMarker m_brush;
    PlayLayer* m_owner = nullptr;
    std::string m_key, m_traceKey;
    int m_cursor = 0, m_first = 0, m_last = 0, m_selected = -1, m_chainStart = 0, m_span = 480;
    float m_spacing = 20.f;
    bool m_sync = false;
    Slider* m_timeline = nullptr;
    CCNode* m_plot = nullptr;
    CCLabelBMFont *m_time = nullptr, *m_selection = nullptr, *m_color = nullptr,
        *m_count = nullptr, *m_sound = nullptr, *m_player = nullptr, *m_enabled = nullptr, *m_step = nullptr;
    bool valid() const {
        return hasLiveOwner() && s.owner == m_owner && s.recording.key == m_key && s.owner->m_isPaused &&
            !running() && s.state != State::Record && !s.recording.trace.empty();
    }
    CCLabelBMFont* label(std::string const& text, CCPoint pos, float scale = .5f) {
        auto l = CCLabelBMFont::create(text.c_str(), "chatFont.fnt"); l->setPosition(pos); l->setScale(scale);
        m_mainLayer->addChild(l); return l;
    }
    CCLabelBMFont* button(char const* text, CCPoint pos, float width, std::function<void()> action) {
        auto card = CCScale9Sprite::create("square02b_001.png"); card->setContentSize({width, 20}); card->setColor({35, 65, 85});
        auto l = CCLabelBMFont::create(text, "chatFont.fnt"); l->limitLabelWidth(width - 6, .48f, .25f);
        l->setPosition({width / 2, 10}); card->addChild(l);
        auto item = CCMenuItemExt::createSpriteExtra(card, [this, action](auto) {
            auto invoke = action;
            if (!valid()) { onClose(nullptr); return; }
            invoke();
        });
        item->m_scaleMultiplier = 1.f; m_buttonMenu->addChild(item);
        item->setPosition(m_buttonMenu->convertToNodeSpace(m_mainLayer->convertToWorldSpace(pos)));
        return l;
    }
    void changeStyle(std::function<void(cf::VisualMarker&)> const& fn) {
        if (m_selected >= 0 && size_t(m_selected) < m_history.current.markers.size()) {
            m_history.change([&](auto& doc) { fn(doc.markers[m_selected]); });
            m_brush = m_history.current.markers[m_selected];
        } else fn(m_brush);
        refresh();
    }
    void select(int direction) {
        auto const& markers = m_history.current.markers;
        if (markers.empty()) { m_selected = -1; refresh(); return; }
        std::vector<size_t> order;
        for (size_t i = 0; i < markers.size(); ++i) order.push_back(i);
        std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return markers[a].tick < markers[b].tick; });
        auto it = std::find(order.begin(), order.end(), size_t(m_selected));
        int index = it == order.end() ? (direction > 0 ? -1 : 0) : int(it - order.begin());
        index = (index + direction + int(order.size())) % int(order.size());
        m_selected = int(order[index]); m_brush = markers[m_selected]; m_cursor = m_brush.tick; refresh();
    }
    void add() {
        if (m_history.current.markers.size() >= cf::MAX_VISUAL_MARKERS) { FLAlertLayer::create("Limite", "Maximo 5000 circulos por presentacion.", "OK")->show(); return; }
        m_brush.tick = m_cursor;
        m_history.change([&](auto& doc) { doc.markers.push_back(m_brush); doc.enabled = true; });
        m_selected = int(m_history.current.markers.size()) - 1; refresh();
    }
    void chain() {
        std::vector<cf::VisualMarker> markers;
        int from = std::min(m_chainStart, m_cursor), to = std::max(m_chainStart, m_cursor);
        if (!cf::chainMarkers(s.recording.trace, from, to, m_spacing, m_brush,
            cf::MAX_VISUAL_MARKERS - m_history.current.markers.size(), markers)) {
            FLAlertLayer::create("Cadena", "No se pudo crear la cadena: limite de circulos o tramo no valido.", "OK")->show(); return;
        }
        m_history.change([&](auto& doc) { doc.markers.insert(doc.markers.end(), markers.begin(), markers.end()); doc.enabled = true; });
        m_selected = int(m_history.current.markers.size()) - 1; m_brush = m_history.current.markers.back(); refresh();
    }
    void saveEdits() {
        if (cf::fingerprint(cf::traceCsv(s.recording)) != m_traceKey) {
            FLAlertLayer::create("Trayectoria distinta", "La captura cambio. Cierra y vuelve a abrir el editor.", "OK")->show(); return;
        }
        auto doc = m_history.current;
        std::stable_sort(doc.markers.begin(), doc.markers.end(), [](auto const& a, auto const& b) { return a.tick < b.tick; });
        std::ostringstream text; cf::writeEdits(text, m_key, m_traceKey, doc);
        if (!writeSafe(path().parent_path() / "showcase-edits.json", text.str())) {
            FLAlertLayer::create("Error", "No se pudo guardar la presentacion. La captura original no se ha cambiado.", "OK")->show(); return;
        }
        s.visual = std::move(doc); s.message = s.visual.enabled ? "Edited showcase saved. Frames > Play" : "Original measured showcase selected";
        onClose(nullptr);
    }
    void refresh() {
        if (!valid()) { onClose(nullptr); return; }
        auto const& markers = m_history.current.markers;
        if (m_selected >= 0 && size_t(m_selected) >= markers.size()) m_selected = -1;
        if (m_selected >= 0) m_brush = markers[m_selected];
        m_cursor = std::clamp(m_cursor, m_first, m_last);
        m_sync = true; m_timeline->setValue(float(m_cursor - m_first) / std::max(1, m_last - m_first)); m_sync = false;
        m_time->setString(fmt::format("Tick {} | {:.2f}s | {} circulos", m_cursor, m_cursor / 240.f, markers.size()).c_str());
        m_time->limitLabelWidth(190, .48f, .25f);
        m_selection->setString(fmt::format("{} | {} F", m_selected < 0 ? "Pincel" : fmt::format("Circulo {}", m_selected + 1), m_brush.width).c_str());
        static char const* colors[] = {"Auto", "Rojo", "Amarillo", "Cian", "Verde", "Blanco"};
        m_color->setString(fmt::format("Color: {}", colors[m_brush.color]).c_str()); m_color->limitLabelWidth(110, .48f, .25f);
        m_count->setString(m_brush.count ? "Cuenta: SI" : "Cuenta: NO");
        m_sound->setString(m_brush.sound ? "Audio: SI" : "Audio: NO");
        m_player->setString(fmt::format("Jugador {} | Tam {:.2f}", m_brush.p1 ? 1 : 2, m_brush.scale).c_str());
        m_enabled->setString(m_history.current.enabled ? "Editado: SI" : "Editado: NO");
        m_step->setString(fmt::format("Paso: {:.0f}u", m_spacing).c_str());
        m_plot->removeAllChildren();
        int from = m_span ? std::max(m_first, m_cursor - m_span / 2) : m_first;
        int to = m_span ? std::min(m_last, m_cursor + m_span / 2) : m_last;
        std::vector<std::pair<int, cf::TracePoint>> points;
        points.push_back({from, cf::positionAt(s.recording.trace, from, m_brush.p1)});
        auto it = std::lower_bound(s.recording.trace.begin(), s.recording.trace.end(), from, [](auto const& p, int tick) { return p.tick < tick; });
        auto end = std::upper_bound(s.recording.trace.begin(), s.recording.trace.end(), to, [](int tick, auto const& p) { return tick < p.tick; });
        size_t stride = std::max<size_t>(1, size_t(end - it) / 1024);
        for (size_t i = 0; it != end; ++it, ++i) if (i % stride == 0) points.push_back({it->tick, {m_brush.p1 ? it->x : it->x2, m_brush.p1 ? it->y : it->y2}});
        points.push_back({to, cf::positionAt(s.recording.trace, to, m_brush.p1)});
        float minX = points[0].second.x, maxX = minX, minY = points[0].second.y, maxY = minY;
        for (auto const& p : points) { minX = std::min(minX, p.second.x); maxX = std::max(maxX, p.second.x); minY = std::min(minY, p.second.y); maxY = std::max(maxY, p.second.y); }
        float scale = std::min(246.f / std::max(24.f, maxX - minX), 76.f / std::max(24.f, maxY - minY));
        auto screen = [&](cf::TracePoint p) { return CCPoint{148.f + (p.x - (minX + maxX) / 2) * scale, 158.f + (p.y - (minY + maxY) / 2) * scale}; };
        auto draw = CCDrawNode::create(); m_plot->addChild(draw);
        for (size_t i = 1; i < points.size(); ++i) {
            auto a = points[i - 1].second, b = points[i].second;
            if (std::hypot(b.x - a.x, b.y - a.y) <= 256.f) draw->drawSegment(screen(a), screen(b), .7f, {.4f, .65f, .8f, .8f});
        }
        for (size_t i = 0; i < markers.size(); ++i) {
            auto const& m = markers[i]; if (m.tick < from || m.tick > to || m.p1 != m_brush.p1) continue;
            auto p = screen(cf::positionAt(s.recording.trace, m.tick, m.p1)); auto c = visualColor(m);
            draw->drawCircle(p, 5.f * m.scale, {0, 0, 0, 0}, int(i) == m_selected ? 2.f : 1.f, {c.r / 255.f, c.g / 255.f, c.b / 255.f, 1}, 32);
            if (markers.size() < 200 || int(i) == m_selected) {
                auto text = CCLabelBMFont::create(fmt::format("{} F", m.width).c_str(), "chatFont.fnt"); text->setScale(.36f); text->setColor({c.r, c.g, c.b}); text->setPosition(p + CCPoint{0, 11}); m_plot->addChild(text);
            }
        }
        draw->drawCircle(screen(cf::positionAt(s.recording.trace, m_cursor, m_brush.p1)), 2.5f, {.3f, 1.f, .9f, 1.f}, 1.f, {1, 1, 1, 1}, 20);
    }
    void onSeek(CCObject*) {
        if (m_sync || !valid()) return;
        auto tick = int64_t(m_first) + std::llround(double(m_timeline->getValue()) * (int64_t(m_last) - m_first));
        m_cursor = static_cast<int>(std::clamp<int64_t>(tick, m_first, m_last)); refresh();
    }
    bool init() {
        if (!hasLiveOwner() || running() || s.state == State::Record || s.recording.trace.empty() || !s.owner->m_isPaused) return false;
        if (!Popup::init(430, 270)) return false;
        setTitle("EDITOR VISUAL"); m_owner = s.owner; m_key = s.recording.key; m_traceKey = cf::fingerprint(cf::traceCsv(s.recording));
        m_history.current = s.visual; m_first = s.recording.trace.front().tick; m_last = s.recording.trace.back().tick; m_cursor = m_chainStart = m_first;
        m_time = label("", {110, 229});
        m_step = button("Paso: 20u", {244, 229}, 74, [this] { m_spacing = m_spacing >= 80 ? 10.f : m_spacing * 2; refresh(); });
        label("Trayectoria grabada | VISUAL, no medido", {148, 214}, .4f);
        auto panel = CCScale9Sprite::create("square02b_001.png"); panel->setContentSize({266, 100}); panel->setPosition({148, 158}); panel->setColor({18, 34, 48}); m_mainLayer->addChild(panel);
        m_plot = CCNode::create(); m_mainLayer->addChild(m_plot);
        m_timeline = Slider::create(this, menu_selector(CFEditor::onSeek), .9f); m_timeline->setPosition({148, 97}); m_mainLayer->addChild(m_timeline);
        auto shift = [this](int by) { m_cursor = static_cast<int>(std::clamp<int64_t>(int64_t(m_cursor) + by, m_first, m_last)); refresh(); };
        button("-24", {40, 76}, 45, [shift] { shift(-24); }); button("-1", {92, 76}, 45, [shift] { shift(-1); });
        button("+1", {144, 76}, 45, [shift] { shift(1); }); button("+24", {196, 76}, 45, [shift] { shift(24); });
        button("Zoom", {251, 76}, 50, [this] { m_span = m_span == 480 ? 240 : m_span == 240 ? 960 : m_span == 960 ? 0 : 480; refresh(); });
        button("Anterior", {43, 52}, 54, [this] { select(-1); }); button("Siguiente", {105, 52}, 60, [this] { select(1); });
        button("Inicio", {174, 52}, 58, [this] { m_chainStart = m_cursor; FLAlertLayer::create("Inicio de cadena", "Mueve la barra al final del tramo y pulsa Cadena. Paso controla la separacion sobre la trayectoria.", "OK")->show(); });
        button("Cadena", {247, 52}, 64, [this] { chain(); });
        button("Deshacer", {58, 28}, 80, [this] { m_history.undo(); m_selected = -1; refresh(); });
        button("Rehacer", {149, 28}, 80, [this] { m_history.redo(); m_selected = -1; refresh(); });
        button("Guardar", {243, 28}, 80, [this] { saveEdits(); });
        m_selection = label("", {352, 229});
        label("Ventana de presentacion", {352, 209}, .42f);
        button("F -", {320, 188}, 56, [this] { changeStyle([](auto& m) { m.width = std::max(1, m.width - 1); }); });
        button("F +", {386, 188}, 56, [this] { changeStyle([](auto& m) { m.width = std::min(120, m.width + 1); }); });
        m_color = button("", {352, 164}, 120, [this] { changeStyle([](auto& m) { m.color = (m.color + 1) % 6; }); });
        button("Tam -", {320, 140}, 56, [this] { changeStyle([](auto& m) { m.scale = std::max(.5f, m.scale - .25f); }); });
        button("Tam +", {386, 140}, 56, [this] { changeStyle([](auto& m) { m.scale = std::min(2.f, m.scale + .25f); }); });
        m_count = button("", {320, 116}, 60, [this] { changeStyle([](auto& m) { m.count = !m.count; }); });
        m_sound = button("", {386, 116}, 60, [this] { changeStyle([](auto& m) { m.sound = !m.sound; }); });
        m_player = button("", {352, 92}, 120, [this] { changeStyle([](auto& m) { m.p1 = !m.p1; }); });
        button("Anadir", {320, 68}, 56, [this] { add(); });
        button("Mover", {386, 68}, 56, [this] {
            if (m_selected < 0) return;
            m_history.change([&](auto& doc) { doc.markers[m_selected].tick = m_cursor; }); refresh();
        });
        button("Eliminar", {352, 44}, 120, [this] {
            if (m_selected < 0) return;
            m_history.change([&](auto& doc) { doc.markers.erase(doc.markers.begin() + m_selected); }); m_selected = -1; refresh();
        });
        m_enabled = button("", {352, 20}, 120, [this] { m_history.change([](auto& doc) { doc.enabled = !doc.enabled; }); refresh(); });
        label("Barra: elegir punto | Mover: aplicar | X: descartar sin guardar", {148, 10}, .29f);
        refresh(); return true;
    }
public:
    static CFEditor* create() { auto p = new CFEditor; if (p->init()) { p->autorelease(); return p; } delete p; return nullptr; }
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
        label("PRACTICE  >  ANALYZE  >  SHOWCASE", {148, 229}, .5f, {111, 219, 235});
        auto status = label((s.visual.enabled ? "EDITADO | " : "") + s.message, {148, 211}, .5f, {220, 229, 244});
        status->limitLabelWidth(264, .5f, .25f);
        bool active = s.state == State::Record || running();
        auto add = [this](char const* title, char const* hint, CCPoint pos, bool enabled,
                          std::function<void()> action, float width = 128, float height = 48) {
            auto card = CCScale9Sprite::create("square02b_001.png");
            card->setContentSize({width, height});
            card->setColor(enabled ? ccColor3B{34, 57, 83} : ccColor3B{35, 39, 49});
            card->setOpacity(245);
            auto heading = CCLabelBMFont::create(title, "bigFont.fnt");
            heading->limitLabelWidth(width - 12, .34f, .2f); heading->setPosition({width / 2, height * .67f});
            heading->setColor(enabled ? ccColor3B{235, 246, 255} : ccColor3B{115, 124, 139});
            card->addChild(heading);
            auto subtitle = CCLabelBMFont::create(hint, "chatFont.fnt");
            subtitle->limitLabelWidth(width - 10, .43f, .2f); subtitle->setPosition({width / 2, height * .27f});
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
        add("EDITAR", "Solo visual", {351, 211}, !active && !s.recording.trace.empty(), [this] {
            onClose(nullptr); if (auto editor = CFEditor::create()) editor->show();
        }, 118, 28);
        add("1. RECORD", "New practice run", {79, 171}, !active, [this] {
            if (!ensureSession()) return;
            if (!supported()) { FLAlertLayer::create("Unavailable", s.message, "OK")->show(); return; }
            auto key = s.recording.key; s.recording = {}; s.recording.key = key;
            s.recording.backend = inputBackend(); s.predictions.clear(); s.snapshot = nullptr;
            s.visual = {};
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
        add("3. PLAY", s.visual.enabled ? "Presentacion editada" : "Circles + sounds", {79, 115},
            !active && (s.recording.ready() || (s.visual.enabled && s.recording.complete)), [this] {
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
        label(s.visual.enabled ? "VISUAL WINDOWS" : "FRAME WINDOWS", {351, 181}, .48f, {111, 219, 235});
        cf::Recording visual;
        if (s.visual.enabled) visual = cf::visualCounter(s.visual);
        auto const& panelRecording = s.visual.enabled ? visual : s.recording;
        auto panelRows = cf::makeRows(panelRecording, static_cast<int>(Mod::get()->getSettingValue<int64_t>("max-rows")));
        std::vector<int> totals(panelRows.size(), 0);
        size_t measured = 0;
        for (size_t i = 0; i < panelRecording.windows.size() && i < panelRecording.inputs.size(); ++i) {
            if (!cf::counted(panelRecording.inputs[i])) continue;
            auto const& window = panelRecording.windows[i];
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
        label(s.visual.enabled ? fmt::format("{} visuales", measured) : fmt::format("{} / {} measured", measured, s.recording.inputs.size()), {351, 58}, .43f, {175, 192, 211});
        label("240 TPS | GD 2.2081 | Geode 5.11.2 | v0.9.0", {215, 18}, .43f, {165, 183, 204});
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
