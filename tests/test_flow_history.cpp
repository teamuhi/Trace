#include <doctest/doctest.h>

#include <cstddef>
#include <memory>
#include <string>

#include "core/image.hpp"
#include "flow/flow_document.hpp"
#include "flow/flow_history.hpp"

using namespace trace::flow;

namespace {

NodeId addBox(FlowDocument& doc, float x) {
    return doc.addNode(NodeKind::Rectangle, Rect{x, 0, 40, 20}, "n");
}

// Adds a node and commits it, the way the editor does after a one-shot edit.
NodeId addAndCommit(FlowDocument& doc, FlowHistory& h, float x) {
    const NodeId id = addBox(doc, x);
    REQUIRE(h.commit(doc));
    return id;
}

// Grows the document by `n` nodes without committing.
void grow(FlowDocument& doc, int n) {
    for (int i = 0; i < n; ++i) addBox(doc, static_cast<float>(i));
}

} // namespace

TEST_CASE("A new history has nothing to undo or redo and is unmodified") {
    FlowHistory h;
    CHECK_FALSE(h.canUndo());
    CHECK_FALSE(h.canRedo());
    CHECK(h.undoCount() == 0);
    CHECK(h.redoCount() == 0);
    CHECK(h.undo() == nullptr);
    CHECK(h.redo() == nullptr);
    CHECK_FALSE(h.isModified());
    CHECK(h.bytes() > 0);
}

TEST_CASE("commit records a step only when the revision changed") {
    FlowDocument doc;
    FlowHistory h;
    CHECK_FALSE(h.commit(doc));
    const NodeId a = addBox(doc, 0);
    CHECK(h.commit(doc));
    CHECK_FALSE(h.commit(doc)); // same revision again
    CHECK(h.undoCount() == 1);

    // A refused edit does not bump the revision, so it is not a step either.
    CHECK_FALSE(doc.setText(9999, "x"));
    CHECK_FALSE(h.commit(doc));
    CHECK(h.undoCount() == 1);
    CHECK(doc.node(a) != nullptr);
}

TEST_CASE("undo and redo round trip through several steps") {
    FlowDocument doc;
    FlowHistory h;
    const NodeId a = addAndCommit(doc, h, 0);
    const NodeId b = addAndCommit(doc, h, 100);
    const NodeId ids[] = {a};
    REQUIRE(doc.moveNodes(ids, 5.0f, 7.0f));
    REQUIRE(h.commit(doc));
    CHECK(h.undoCount() == 3);

    const FlowDocument* s = h.undo();
    REQUIRE(s != nullptr);
    CHECK(s->nodes().size() == 2);
    CHECK(s->node(a)->rect.x == 0.0f);
    doc = *s;

    s = h.undo();
    REQUIRE(s != nullptr);
    CHECK(s->nodes().size() == 1);
    CHECK(s->node(b) == nullptr);

    s = h.undo();
    REQUIRE(s != nullptr);
    CHECK(s->empty());
    CHECK_FALSE(h.canUndo());
    CHECK(h.undo() == nullptr); // at the start
    CHECK(h.redoCount() == 3);

    s = h.redo();
    REQUIRE(s != nullptr);
    CHECK(s->nodes().size() == 1);
    h.redo();
    s = h.redo();
    REQUIRE(s != nullptr);
    CHECK(s->node(a)->rect.x == 5.0f);
    CHECK(s->node(a)->rect.y == 7.0f);
    CHECK(h.redo() == nullptr); // at the end
    CHECK_FALSE(h.canRedo());
    CHECK(h.committed().nodes().size() == 2);
}

TEST_CASE("Committing after an undo discards the redo steps") {
    FlowDocument doc;
    FlowHistory h;
    addAndCommit(doc, h, 0);
    addAndCommit(doc, h, 10);
    doc = *h.undo();
    REQUIRE(h.canRedo());

    addAndCommit(doc, h, 20);
    CHECK_FALSE(h.canRedo());
    CHECK(h.redoCount() == 0);
    CHECK(h.undoCount() == 2);
    CHECK(h.redo() == nullptr);
    CHECK(h.committed().nodes().size() == 2);
}

TEST_CASE("A new commit after undo is detected even when its revision equals a discarded one") {
    FlowDocument doc;
    FlowHistory h;
    addAndCommit(doc, h, 0);
    addAndCommit(doc, h, 10); // revision 2
    doc = *h.undo();          // back to revision 1
    addBox(doc, 50);          // revision 2 again, different content
    CHECK(h.commit(doc));
    CHECK(h.committed().nodes().back().rect.x == 50.0f);
}

TEST_CASE("A gesture records one step however many edits it holds") {
    FlowDocument doc;
    FlowHistory h;
    const NodeId a = addAndCommit(doc, h, 0);
    const NodeId ids[] = {a};

    h.beginGesture();
    CHECK(h.gestureActive());
    for (int i = 0; i < 10; ++i) {
        REQUIRE(doc.moveNodes(ids, 1.0f, 0.0f));
        CHECK_FALSE(h.commit(doc)); // deferred
    }
    CHECK(h.undoCount() == 1);
    CHECK(h.endGesture(doc));
    CHECK_FALSE(h.gestureActive());
    CHECK(h.undoCount() == 2);

    const FlowDocument* s = h.undo();
    REQUIRE(s != nullptr);
    CHECK(s->node(a)->rect.x == 0.0f);
}

TEST_CASE("Nested gestures commit at the outermost end only") {
    FlowDocument doc;
    FlowHistory h;
    const NodeId a = addAndCommit(doc, h, 0);
    const NodeId ids[] = {a};

    h.beginGesture();
    h.beginGesture();
    REQUIRE(doc.moveNodes(ids, 3.0f, 0.0f));
    CHECK_FALSE(h.endGesture(doc)); // inner end
    CHECK(h.gestureActive());
    CHECK(h.undoCount() == 1);
    REQUIRE(doc.moveNodes(ids, 3.0f, 0.0f));
    CHECK(h.endGesture(doc));
    CHECK(h.undoCount() == 2);
    CHECK(h.committed().node(a)->rect.x == 6.0f);
}

TEST_CASE("A gesture with no net change records nothing") {
    FlowDocument doc;
    FlowHistory h;
    addAndCommit(doc, h, 0);
    h.beginGesture();
    CHECK_FALSE(h.endGesture(doc));
    CHECK(h.undoCount() == 1);
    CHECK_FALSE(h.gestureActive());
}

TEST_CASE("endGesture without beginGesture does nothing") {
    FlowDocument doc;
    FlowHistory h;
    addBox(doc, 0); // uncommitted edit
    CHECK_FALSE(h.endGesture(doc));
    CHECK(h.undoCount() == 0);
    CHECK_FALSE(h.gestureActive());
    CHECK(h.commit(doc)); // still committable normally
}

TEST_CASE("undo and redo are refused while a gesture is open") {
    FlowDocument doc;
    FlowHistory h;
    addAndCommit(doc, h, 0);
    addAndCommit(doc, h, 10);
    doc = *h.undo();
    h.beginGesture();
    CHECK_FALSE(h.canUndo());
    CHECK_FALSE(h.canRedo());
    CHECK(h.undo() == nullptr);
    CHECK(h.redo() == nullptr);
    h.endGesture(doc);
    CHECK(h.canUndo());
    CHECK(h.canRedo());
}

TEST_CASE("cancelGesture drops the gesture and returns the committed state") {
    FlowDocument doc;
    FlowHistory h;
    const NodeId a = addAndCommit(doc, h, 0);
    CHECK(h.cancelGesture() == nullptr); // none open

    const NodeId ids[] = {a};
    h.beginGesture();
    h.beginGesture();
    REQUIRE(doc.moveNodes(ids, 9.0f, 0.0f));
    const FlowDocument* base = h.cancelGesture();
    REQUIRE(base != nullptr);
    CHECK(base->node(a)->rect.x == 0.0f);
    CHECK_FALSE(h.gestureActive());
    CHECK(h.undoCount() == 1);
    doc = *base;
    CHECK_FALSE(h.commit(doc));
}

TEST_CASE("An edit left uncommitted by a cancelled gesture is committed normally later") {
    FlowDocument doc;
    FlowHistory h;
    const NodeId a = addAndCommit(doc, h, 0);
    const NodeId ids[] = {a};
    h.beginGesture();
    REQUIRE(doc.moveNodes(ids, 4.0f, 0.0f));
    h.cancelGesture();
    CHECK(h.commit(doc)); // the caller chose to keep it
    CHECK(h.undoCount() == 2);
}

TEST_CASE("Saved-state tracking follows undo, redo and new edits") {
    FlowDocument doc;
    FlowHistory h;
    addAndCommit(doc, h, 0);
    CHECK(h.isModified());
    h.markSaved();
    CHECK_FALSE(h.isModified());
    CHECK(h.atSavedState());

    addAndCommit(doc, h, 10);
    CHECK(h.isModified());
    doc = *h.undo();
    CHECK_FALSE(h.isModified()); // back at the saved point
    doc = *h.redo();
    CHECK(h.isModified());
    doc = *h.undo();
    CHECK_FALSE(h.isModified());

    // A new edit discards the redo branch; the saved state (the cursor) is still reachable by undo.
    addAndCommit(doc, h, 20);
    CHECK(h.isModified());
    doc = *h.undo();
    CHECK_FALSE(h.isModified());
}

TEST_CASE("Saving on a state that a new commit discards makes the document stay modified") {
    FlowDocument doc;
    FlowHistory h;
    addAndCommit(doc, h, 0);
    addAndCommit(doc, h, 10);
    h.markSaved(); // saved at step 2
    doc = *h.undo();
    doc = *h.undo(); // at step 0
    addAndCommit(doc, h, 99); // discards the saved state
    CHECK(h.isModified());
    doc = *h.undo();
    CHECK(h.isModified()); // never matches the file again
    CHECK_FALSE(h.atSavedState());
}

TEST_CASE("isModified(current) also reports uncommitted edits") {
    FlowDocument doc;
    FlowHistory h;
    h.markSaved();
    CHECK_FALSE(h.isModified(doc));
    addBox(doc, 0);
    CHECK_FALSE(h.isModified()); // not committed yet
    CHECK(h.isModified(doc));
    h.commit(doc);
    CHECK(h.isModified(doc));
}

TEST_CASE("reset starts over from the given document") {
    FlowDocument doc;
    FlowHistory h;
    addAndCommit(doc, h, 0);
    addAndCommit(doc, h, 10);
    h.beginGesture();

    FlowDocument loaded;
    addBox(loaded, 5);
    addBox(loaded, 6);
    h.reset(loaded);
    CHECK_FALSE(h.gestureActive());
    CHECK_FALSE(h.canUndo());
    CHECK_FALSE(h.canRedo());
    CHECK_FALSE(h.isModified());
    CHECK(h.committed().nodes().size() == 2);
    CHECK_FALSE(h.commit(loaded));

    h.clear();
    CHECK(h.committed().empty());
    CHECK_FALSE(h.isModified());
}

TEST_CASE("The step cap drops the oldest steps") {
    FlowDocument doc;
    FlowHistory h(FlowHistory::kDefaultMaxBytes, 3);
    for (int i = 0; i < 10; ++i) addAndCommit(doc, h, static_cast<float>(i));
    CHECK(h.undoCount() == 3);

    int undone = 0;
    const FlowDocument* s = nullptr;
    while (const FlowDocument* p = h.undo()) {
        s = p;
        ++undone;
    }
    CHECK(undone == 3);
    REQUIRE(s != nullptr);
    CHECK(s->nodes().size() == 7); // the oldest reachable state

    // Lowering the cap trims immediately and keeps redo steps.
    h.redo();
    h.redo();
    h.redo();
    h.setMaxSteps(1);
    CHECK(h.maxSteps() == 1);
    CHECK(h.undoCount() == 1);
    h.setMaxSteps(0); // treated as 1
    CHECK(h.maxSteps() == 1);
    CHECK(h.undoCount() == 1);
}

TEST_CASE("The byte limit drops the oldest steps but keeps the newest") {
    FlowDocument doc;
    FlowHistory h;
    grow(doc, 20);
    h.reset(doc);
    for (int i = 0; i < 20; ++i) {
        addBox(doc, static_cast<float>(i));
        REQUIRE(h.commit(doc));
    }
    CHECK(h.undoCount() == 20);
    const std::size_t full = h.bytes();

    h.setMaxBytes(full / 2);
    CHECK(h.maxBytes() == full / 2);
    CHECK(h.undoCount() < 20);
    CHECK(h.undoCount() >= 1);
    CHECK(h.bytes() <= full / 2);

    // The current state is intact.
    CHECK(h.committed().nodes().size() == 40);

    // An impossible limit still keeps the newest step and the current state.
    h.setMaxBytes(1);
    CHECK(h.undoCount() == 1);
    CHECK(h.committed().nodes().size() == 40);
    const FlowDocument* s = h.undo();
    REQUIRE(s != nullptr);
    CHECK(s->nodes().size() == 39);

    // New commits under the tiny limit keep exactly one step.
    doc = *s;
    addBox(doc, 1);
    CHECK(h.commit(doc));
    CHECK(h.undoCount() == 1);
}

TEST_CASE("Byte-limit eviction keeps redo steps and never drops the current state") {
    FlowDocument doc;
    FlowHistory h(1); // everything is over the limit
    for (int i = 0; i < 5; ++i) addAndCommit(doc, h, static_cast<float>(i));
    CHECK(h.undoCount() == 1);

    doc = *h.undo(); // parked at the oldest retained state
    CHECK(h.undoCount() == 0);
    CHECK(h.redoCount() == 1);
    h.setMaxBytes(1);
    CHECK(h.redoCount() == 1);
    CHECK(h.committed().nodes().size() == 4);
}

TEST_CASE("Evicting the saved state makes the document stay modified") {
    FlowDocument doc;
    FlowHistory h(FlowHistory::kDefaultMaxBytes, 2);
    addAndCommit(doc, h, 0);
    h.markSaved(); // saved at one node
    CHECK_FALSE(h.isModified());

    for (int i = 1; i < 5; ++i) addAndCommit(doc, h, static_cast<float>(i));
    CHECK(h.undoCount() == 2);
    while (const FlowDocument* s = h.undo()) doc = *s;
    CHECK(h.isModified()); // the saved state was evicted: no state matches the file
    CHECK_FALSE(h.atSavedState());
    while (h.redo()) {}
    CHECK(h.isModified());
}

TEST_CASE("Eviction shifts the saved state when it survives") {
    FlowDocument doc;
    FlowHistory h(FlowHistory::kDefaultMaxBytes, 3);
    for (int i = 0; i < 3; ++i) addAndCommit(doc, h, static_cast<float>(i));
    h.markSaved(); // three nodes
    addAndCommit(doc, h, 3);
    addAndCommit(doc, h, 4); // drops the two oldest states; the saved one is the oldest left but one
    CHECK(h.undoCount() == 3);
    CHECK(h.isModified());
    doc = *h.undo();
    doc = *h.undo();
    CHECK(doc.nodes().size() == 3);
    CHECK_FALSE(h.isModified());
}

TEST_CASE("Images stay shared between snapshots") {
    FlowDocument doc;
    FlowHistory h;
    auto img = std::make_shared<const trace::Image>(4, 4);
    const ImageId id = doc.addImage(img);
    REQUIRE(id != kNoImage);
    REQUIRE(h.commit(doc));
    const FlowDocument* s = h.undo();
    REQUIRE(s != nullptr);
    CHECK(s->image(id).get() == nullptr); // the image was added by the step
    s = h.redo();
    REQUIRE(s != nullptr);
    CHECK(s->image(id).get() == img.get());
}

TEST_CASE("Shaping a connector by hand is one undoable step, and so is resetting it") {
    FlowDocument doc;
    FlowHistory h;
    const NodeId a = addAndCommit(doc, h, 0);
    const NodeId b = addAndCommit(doc, h, 300);
    const EdgeId e = doc.connect(End{a, Face::Auto, {}}, End{b, Face::Auto, {}});
    REQUIRE(h.commit(doc));

    // A drag: many offsets, one gesture, one step.
    h.beginGesture();
    for (int i = 1; i <= 10; ++i) {
        doc.setEdgeVia(e, Pt{static_cast<float>(i), static_cast<float>(-i)});
        CHECK_FALSE(h.commit(doc)); // refused while the gesture is open
    }
    CHECK(h.endGesture(doc));
    CHECK(doc.edge(e)->via == std::optional<Pt>(Pt{10, -10}));

    const FlowDocument* before = h.undo();
    REQUIRE(before != nullptr);
    CHECK_FALSE(before->edge(e)->via.has_value());
    const FlowDocument* after = h.redo();
    REQUIRE(after != nullptr);
    CHECK(after->edge(e)->via == std::optional<Pt>(Pt{10, -10}));

    doc = *after;
    REQUIRE(doc.setEdgeVia(e, std::nullopt));
    CHECK(h.commit(doc));
    const FlowDocument* bent = h.undo(); // back to the bent route
    REQUIRE(bent != nullptr);
    CHECK(bent->edge(e)->via.has_value());
    const FlowDocument* reset = h.redo();
    REQUIRE(reset != nullptr);
    CHECK_FALSE(reset->edge(e)->via.has_value());
}
