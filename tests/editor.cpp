#include "../src/Editor.hpp"
#include <cassert>
#include <iostream>

int main() {
    std::vector<cf::Sample> trace{{0, 0, 0, 100, 100}, {10, 100, 0, 200, 100}, {20, 100, 100, 200, 200}};
    assert(cf::positionAt(trace, 5, true).x == 50);
    assert(cf::positionAt(trace, 15, true).y == 50);
    assert(cf::positionAt(trace, -1, false).x == 100);
    assert(cf::positionAt(trace, 30, false).y == 200);
    std::vector<cf::Sample> teleport{{0, 0, 0, 0, 0}, {10, 1000, 0, 0, 0}};
    assert(cf::positionAt(teleport, 4, true).x == 0 && cf::positionAt(teleport, 6, true).x == 1000);
    cf::VisualMarker brush; brush.color = 1;
    std::vector<cf::VisualMarker> chain;
    assert(cf::chainMarkers(trace, 0, 20, 50, brush, 10, chain) && chain.size() == 5);
    assert(chain[0].tick == 0 && chain[1].tick == 5 && chain[2].tick == 10 && chain[3].tick == 15 && chain[4].tick == 20);
    assert(!cf::chainMarkers(trace, 0, 20, 50, brush, 4, chain) && chain.size() == 5);
    assert(!cf::chainMarkers(trace, 20, 0, 50, brush, 10, chain));
    assert(!cf::chainMarkers(trace, 0, 20, 0, brush, 10, chain));
    assert(cf::chainMarkers(teleport, 0, 10, 10, brush, 10, chain) && chain.size() == 1);
    cf::VisualEdits edits; edits.enabled = true;
    brush.tick = 15; edits.markers.push_back(brush);
    brush.tick = 5; brush.width = 3; brush.sound = true; brush.p1 = false; edits.markers.push_back(brush);
    std::stringstream file; cf::writeEdits(file, "123", "456", edits);
    cf::VisualEdits loaded;
    assert(cf::readEdits(file, "123", "456", 0, 20, loaded));
    assert(loaded.enabled && loaded.markers[0].tick == 5 && loaded.markers[0].width == 3 && !loaded.markers[0].p1 && loaded.markers[0].sound);
    std::stringstream wrong; cf::writeEdits(wrong, "123", "456", edits);
    assert(!cf::readEdits(wrong, "123", "other", 0, 20, loaded) && loaded.markers.size() == 2);
    edits.markers[0].tick = 99;
    std::stringstream outside; cf::writeEdits(outside, "123", "456", edits);
    assert(!cf::readEdits(outside, "123", "456", 0, 20, loaded));
    edits.markers[0].tick = 1; edits.markers[0].width = 0;
    std::stringstream width; cf::writeEdits(width, "123", "456", edits);
    assert(!cf::readEdits(width, "123", "456", 0, 20, loaded));
    cf::VisualEdits empty; std::stringstream blank; cf::writeEdits(blank, "123", "456", empty);
    assert(cf::readEdits(blank, "123", "456", 0, 20, loaded) && loaded.markers.empty() && !loaded.enabled);
    cf::VisualEdits counter; counter.markers = {{5, 1, 1}, {10, 5, 4}}; counter.markers[1].count = false;
    auto recording = cf::visualCounter(counter); assert(recording.inputs.size() == 1 && recording.windows[0].width() == 1);
    cf::EditHistory history;
    for (int i = 0; i < 40; ++i) history.change([i](auto& doc) { doc.markers = {{i, 1, 1}}; });
    for (int i = 0; i < 32; ++i) assert(history.undo());
    assert(!history.undo() && history.current.markers[0].tick == 7);
    for (int i = 0; i < 32; ++i) assert(history.redo());
    assert(!history.redo() && history.current.markers[0].tick == 39);
    assert(history.undo()); history.change([](auto& doc) { doc.enabled = true; }); assert(!history.redo());
    std::cout << "Editor tests passed\n";
}
