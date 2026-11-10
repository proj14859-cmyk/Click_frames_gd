#include "../src/Bundle.hpp"
#include "../src/Editor.hpp"
#include "../src/Pack.hpp"
#include <cassert>
#include <chrono>
#include <iostream>

int main(int argc, char** argv) {
    namespace fs = std::filesystem;
    auto root = fs::temp_directory_path() / ("click-frames-bundle-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    assert(fs::create_directory(root));
    cf::Recording r; r.key = "123"; r.complete = r.verified = true; r.endTick = 20; r.analysisPolicy = 1;
    r.inputs = {{10, true, 1, true, 10, 105, 0}, {15, false, 1, true, 15, 110, 0}};
    r.trace = {{0, 0, 105, 0, 0}, {20, 20, 105, 0, 0}};
    r.windows = {{2, 1, true, false}, {0, 0, false, false}};
    auto dir = root / "level-1" / r.key; std::string digest;
    assert(cf::saveBundle(dir, r, true, digest));
    cf::LoadedBundle loaded; assert(cf::loadBundle(dir, r.key, loaded));
    assert(loaded.analysisLoaded && loaded.recording.ready() && loaded.recording.windows[0].width() == 4);
    auto immutable = cf::readFile(dir / "capture.flr");
    cf::Recording work = r; work.inputs.resize(1); work.inputs[0].tick = 11; work.windows.resize(1);
    work.windows[0] = {5, 0, true, false}; work.endTick = 21; work.analysisPolicy = 2;
    int captureWrites = 0;
    auto writer = [&](fs::path const& file, std::string const& bytes) {
        if (file.filename() == "capture.flr") ++captureWrites;
        return cf::replaceChanged(file, bytes);
    };
    assert(cf::saveBundle(dir, work, false, digest, writer));
    assert(!captureWrites && cf::readFile(dir / "capture.flr") == immutable);
    assert(cf::loadBundle(dir, r.key, loaded));
    assert(loaded.recording.inputs.size() == 1 && loaded.recording.inputs[0].tick == 11 && loaded.recording.endTick == 21);
    assert(loaded.recording.windows[0].width() == 6 && loaded.recording.analysisPolicy == 2 && loaded.recording.trace.size() == 2);
    cf::Recording original;
    std::istringstream originalFile(immutable); assert(cf::readCapture(originalFile, r.key, original));
    assert(original.inputs.size() == 2 && original.inputs[0].tick == 10 && original.endTick == 20 && !original.verified);
    assert(cf::replaceFile(dir / "playback.flp", "CFPLAY 1 wrong-digest 1 21 12 2 vanilla240 0\n"));
    assert(cf::loadBundle(dir, r.key, loaded) && loaded.backupAnalysis && loaded.recording.inputs[0].tick == 11);
    auto atomic = root / "atomic"; std::string atomicDigest;
    assert(cf::saveBundle(atomic, r, true, atomicDigest)); auto oldDigest = atomicDigest;
    auto changed = r; changed.inputs[0].tick = 9;
    auto failCommit = [](fs::path const& file, std::string const& bytes) {
        return file.filename() != "capture.flr" && cf::replaceFile(file, bytes);
    };
    assert(!cf::saveBundle(atomic, changed, true, atomicDigest, failCommit) && atomicDigest == oldDigest);
    assert(cf::loadBundle(atomic, r.key, loaded) && loaded.backupAnalysis && loaded.recording.inputs[0].tick == 10);
    std::ostringstream legacy; cf::write(legacy, r); auto oldPath = root / "capture.cfr";
    assert(cf::replaceFile(oldPath, legacy.str()));
    auto migrated = root / "migrated";
    assert(cf::migrateLegacy(migrated, r.key, {root / "missing.cfr", oldPath}));
    assert(cf::readFile(oldPath) == legacy.str() && cf::loadBundle(migrated, r.key, loaded) && loaded.recording.ready());
    assert(!cf::migrateLegacy(migrated, r.key, {oldPath}));
    assert(!cf::migrateLegacy(root / "bad", "wrong-key", {oldPath}));
    auto traceHash = cf::fingerprint(cf::traceCsv(r)); cf::VisualEdits edits; edits.enabled = true; edits.markers = {{12, 1, 1}};
    std::ostringstream json; cf::writeEdits(json, r.key, traceHash, edits);
    assert(cf::replaceFile(migrated / "showcase-edits.json", json.str()));
    std::istringstream oldEdits(cf::readFile(migrated / "showcase-edits.json")); cf::VisualEdits visual;
    assert(cf::readEdits(oldEdits, r.key, traceHash, 0, 20, visual));
    std::ostringstream tsv; cf::writeEditsTsv(tsv, r.key, traceHash, visual);
    assert(cf::replaceFile(migrated / ("edits-" + traceHash + ".tsv"), tsv.str()));
    assert(cf::readFile(migrated / "showcase-edits.json") == json.str());
    std::istringstream visualFile(tsv.str()); assert(cf::readEditsTsv(visualFile, r.key, traceHash, 0, 20, visual) && visual.enabled && visual.markers[0].tick == 12);
    std::ostringstream portable; cf::write(portable, work);
    cf::Pack pack; pack.version = 2; pack.folder = "level-1";
    pack.files = {{"run-123.cfr", portable.str()}, {"edits-123-" + traceHash + ".tsv", tsv.str()}};
    std::stringstream wire; cf::packWrite(wire, pack); cf::Pack imported;
    assert(cf::packRead(wire, imported) && imported.version == 2);
    std::istringstream run(imported.files[0].data); cf::Recording importedRun;
    assert(cf::read(run, "123", importedRun) && importedRun.analysisPolicy == 2);
    std::istringstream importedEdits(imported.files[1].data);
    assert(cf::readEditsTsv(importedEdits, "123", cf::fingerprint(cf::traceCsv(importedRun)), 0, 20, visual) && visual.markers[0].width == 1);
    std::istringstream corrupt("CFCAP 1 \"123\" 1 20 \"vanilla240\" 999999999 0\n");
    assert(!cf::readCapture(corrupt, "123", original));
    for (int i = 1; i < argc; ++i) {
        fs::path source(argv[i]); auto name = source.filename().string(); assert(cf::runName(name));
        auto key = name.substr(4, name.size() - 8); auto before = cf::readFile(source);
        std::istringstream input(before); cf::Recording fixture; assert(cf::read(input, key, fixture));
        auto target = root / ("fixture-" + std::to_string(i));
        assert(cf::migrateLegacy(target, key, {source}) && cf::loadBundle(target, key, loaded));
        std::ostringstream expected, actual; cf::write(expected, fixture); cf::write(actual, loaded.recording);
        assert(expected.str() == actual.str() && cf::readFile(source) == before);
        std::cout << name << ": full payload " << expected.str().size() << " bytes; playback checkpoint "
            << cf::playbackBytes(loaded.captureDigest, loaded.recording).size() << " bytes\n";
    }
    fs::remove_all(root);
    std::cout << "Bundle/migration tests passed\n";
}
