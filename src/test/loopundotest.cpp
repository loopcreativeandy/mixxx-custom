#include <gtest/gtest.h>

#include <memory>

#include "control/controlproxy.h"
#include "mixer/loopundo.h"
#include "test/mockedenginebackendtest.h"

namespace {

// Long enough that the timer never fires on its own; flushPending() stands in
// for "the change has settled".
constexpr int kNeverSettles = 60 * 60 * 1000;

/// andy-custom: LoopUndo against the real LoopingControl of the test decks.
class LoopUndoTest : public MockedEngineBackendTest {
  protected:
    void SetUp() override {
        MockedEngineBackendTest::SetUp();
        m_pStart1 = std::make_unique<ControlProxy>(m_sGroup1, "loop_start_position");
        m_pEnd1 = std::make_unique<ControlProxy>(m_sGroup1, "loop_end_position");
        m_pStart2 = std::make_unique<ControlProxy>(m_sGroup2, "loop_start_position");
        m_pEnd2 = std::make_unique<ControlProxy>(m_sGroup2, "loop_end_position");
        // LoopUndo creates [LoopUndo],undo, so it has to exist first.
        m_pLoopUndo = std::make_unique<LoopUndo>(nullptr, kNeverSettles);
        m_pUndoButton = std::make_unique<ControlProxy>("[LoopUndo]", "undo");
    }

    void TearDown() override {
        m_pLoopUndo.reset();
        MockedEngineBackendTest::TearDown();
    }

    void setLoop1(double start, double end) {
        m_pStart1->set(start);
        m_pEnd1->set(end);
        m_pLoopUndo->flushPending();
    }

    void expectLoop1(double start, double end) {
        EXPECT_DOUBLE_EQ(start, m_pStart1->get());
        EXPECT_DOUBLE_EQ(end, m_pEnd1->get());
    }

    void pressUndo() {
        m_pUndoButton->set(1);
        m_pUndoButton->set(0);
    }

    std::unique_ptr<ControlProxy> m_pStart1;
    std::unique_ptr<ControlProxy> m_pEnd1;
    std::unique_ptr<ControlProxy> m_pStart2;
    std::unique_ptr<ControlProxy> m_pEnd2;
    std::unique_ptr<ControlProxy> m_pUndoButton;
    std::unique_ptr<LoopUndo> m_pLoopUndo;
};

TEST_F(LoopUndoTest, UndoPutsBackTheMovedLoopIn) {
    setLoop1(10000, 50000);
    m_pStart1->set(20000); // the accidental move
    m_pLoopUndo->flushPending();
    expectLoop1(20000, 50000);

    pressUndo();
    expectLoop1(10000, 50000);
}

TEST_F(LoopUndoTest, ChangesWithinTheSettleTimeAreOneStep) {
    setLoop1(10000, 50000);
    const int stepsBefore = m_pLoopUndo->stepCount();
    m_pStart1->set(20000);
    m_pEnd1->set(60000);
    m_pLoopUndo->flushPending();
    EXPECT_EQ(stepsBefore + 1, m_pLoopUndo->stepCount());

    pressUndo();
    expectLoop1(10000, 50000);
}

TEST_F(LoopUndoTest, UndoWalksFurtherBack) {
    setLoop1(10000, 50000);
    setLoop1(20000, 50000);
    setLoop1(20000, 70000);

    pressUndo();
    expectLoop1(20000, 50000);
    pressUndo();
    expectLoop1(10000, 50000);
}

TEST_F(LoopUndoTest, RestoresALoopThatWasLaterThanTheCurrentOne) {
    // The old start is after the current end: writing it first would make
    // LoopingControl drop the end.
    setLoop1(300000, 400000);
    setLoop1(100000, 200000);

    pressUndo();
    expectLoop1(300000, 400000);
}

TEST_F(LoopUndoTest, RestoresALoopThatWasEarlierThanTheCurrentOne) {
    setLoop1(100000, 200000);
    setLoop1(300000, 400000);

    pressUndo();
    expectLoop1(100000, 200000);
}

TEST_F(LoopUndoTest, UndoOfANewLoopClearsIt) {
    expectLoop1(-1, -1);
    setLoop1(10000, 50000);

    pressUndo();
    expectLoop1(-1, -1);
}

TEST_F(LoopUndoTest, TheUndoItselfIsNotAStep) {
    setLoop1(10000, 50000);
    setLoop1(20000, 50000);
    const int steps = m_pLoopUndo->stepCount();

    pressUndo();
    m_pLoopUndo->flushPending();
    EXPECT_EQ(steps - 1, m_pLoopUndo->stepCount());
}

TEST_F(LoopUndoTest, UndoesTheDeckThatChangedLast) {
    setLoop1(10000, 50000);
    m_pStart2->set(30000);
    m_pEnd2->set(90000);
    m_pLoopUndo->flushPending();

    pressUndo();
    EXPECT_DOUBLE_EQ(-1, m_pStart2->get());
    expectLoop1(10000, 50000);
}

TEST_F(LoopUndoTest, LoadingATrackDropsThatDecksHistory) {
    setLoop1(10000, 50000);
    setLoop1(20000, 50000);
    EXPECT_LT(0, m_pLoopUndo->stepCount());

    m_pTrack1 = m_pMixerDeck1->loadFakeTrack(false, 0.0);
    m_pLoopUndo->flushPending();
    EXPECT_EQ(0, m_pLoopUndo->stepCount());
}

TEST_F(LoopUndoTest, NothingToUndoChangesNothing) {
    pressUndo();
    expectLoop1(-1, -1);
    EXPECT_EQ(0, m_pLoopUndo->stepCount());
}

} // namespace
