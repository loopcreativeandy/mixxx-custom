#include <gtest/gtest.h>

#include <memory>

#include "control/controlproxy.h"
#include "mixer/loopundo.h"
#include "test/mockedenginebackendtest.h"
#include "track/track.h"

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
        // In the app PlayerInfo::trackChanged does this.
        m_pLoopUndo->setTrack(0, m_pTrack1);
        m_pLoopUndo->flushPending();
    }

    CuePointer addHotcue(int index, double framePos) {
        CuePointer pCue = m_pTrack1->createAndAddCue(mixxx::CueType::HotCue,
                index,
                mixxx::audio::FramePos(framePos),
                mixxx::audio::kInvalidFramePos);
        m_pLoopUndo->flushPending();
        return pCue;
    }

    /// Engine sample position of the CueControl CO, -1 when not set.
    double co(const QString& item) {
        return ControlProxy(m_sGroup1, item).get();
    }

    void press(const QString& item) {
        ControlProxy proxy(m_sGroup1, item);
        proxy.set(1);
        proxy.set(0);
        m_pLoopUndo->flushPending();
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

// CP129: hotcues and intro/outro markers.

TEST_F(LoopUndoTest, UndoOfANewHotcueRemovesIt) {
    addHotcue(0, 1000);
    EXPECT_DOUBLE_EQ(2000, co("hotcue_1_position"));

    pressUndo();
    EXPECT_FALSE(m_pTrack1->findHotcueByIndex(0));
    EXPECT_DOUBLE_EQ(-1, co("hotcue_1_position"));
}

TEST_F(LoopUndoTest, DeletedHotcueComesBackWithLabelAndColor) {
    CuePointer pCue = addHotcue(2, 5000);
    pCue->setLabel(QStringLiteral("drop"));
    pCue->setColor(mixxx::RgbColor(0x00FF00));
    m_pLoopUndo->flushPending();

    press(QStringLiteral("hotcue_3_clear")); // the accidental delete
    EXPECT_FALSE(m_pTrack1->findHotcueByIndex(2));

    pressUndo();
    CuePointer pBack = m_pTrack1->findHotcueByIndex(2);
    ASSERT_TRUE(pBack);
    EXPECT_EQ(mixxx::audio::FramePos(5000), pBack->getPosition());
    EXPECT_EQ(QStringLiteral("drop"), pBack->getLabel());
    EXPECT_EQ(mixxx::RgbColor(0x00FF00), pBack->getColor());
    EXPECT_DOUBLE_EQ(10000, co("hotcue_3_position"));
}

TEST_F(LoopUndoTest, MovedHotcueGoesBack) {
    CuePointer pCue = addHotcue(0, 1000);
    pCue->setStartPosition(mixxx::audio::FramePos(8000));
    m_pLoopUndo->flushPending();

    pressUndo();
    EXPECT_EQ(mixxx::audio::FramePos(1000), pCue->getPosition());
    EXPECT_DOUBLE_EQ(2000, co("hotcue_1_position"));
}

TEST_F(LoopUndoTest, ClearedOutroEndComesBack) {
    m_pTrack1->createAndAddCue(mixxx::CueType::Outro,
            Cue::kNoHotCue,
            mixxx::audio::FramePos(40000),
            mixxx::audio::FramePos(60000));
    m_pLoopUndo->flushPending();
    EXPECT_DOUBLE_EQ(120000, co("outro_end_position"));

    press(QStringLiteral("outro_end_clear"));
    EXPECT_DOUBLE_EQ(-1, co("outro_end_position"));

    pressUndo();
    EXPECT_DOUBLE_EQ(80000, co("outro_start_position"));
    EXPECT_DOUBLE_EQ(120000, co("outro_end_position"));
}

TEST_F(LoopUndoTest, ClearedIntroStartWithoutEndComesBack) {
    m_pTrack1->createAndAddCue(mixxx::CueType::Intro,
            Cue::kNoHotCue,
            mixxx::audio::FramePos(3000),
            mixxx::audio::kInvalidFramePos);
    m_pLoopUndo->flushPending();

    press(QStringLiteral("intro_start_clear")); // removes the whole cue
    EXPECT_FALSE(m_pTrack1->findCueByType(mixxx::CueType::Intro));

    pressUndo();
    CuePointer pIntro = m_pTrack1->findCueByType(mixxx::CueType::Intro);
    ASSERT_TRUE(pIntro);
    EXPECT_EQ(mixxx::audio::FramePos(3000), pIntro->getPosition());
    EXPECT_DOUBLE_EQ(6000, co("intro_start_position"));
}

TEST_F(LoopUndoTest, CueAndLoopStepsShareOneHistory) {
    setLoop1(10000, 50000);
    addHotcue(0, 1000);

    pressUndo(); // the hotcue, it changed last
    EXPECT_FALSE(m_pTrack1->findHotcueByIndex(0));
    expectLoop1(10000, 50000);
    pressUndo(); // then the loop
    expectLoop1(-1, -1);
}

TEST_F(LoopUndoTest, MainCueIsNotAStep) {
    const int steps = m_pLoopUndo->stepCount();
    m_pTrack1->setMainCuePosition(mixxx::audio::FramePos(7000));
    m_pLoopUndo->flushPending();
    EXPECT_EQ(steps, m_pLoopUndo->stepCount());
}

TEST_F(LoopUndoTest, UndoOfACueIsNotAStep) {
    addHotcue(0, 1000);
    press(QStringLiteral("hotcue_1_clear"));
    const int steps = m_pLoopUndo->stepCount();

    pressUndo();
    m_pLoopUndo->flushPending();
    EXPECT_EQ(steps - 1, m_pLoopUndo->stepCount());
    EXPECT_TRUE(m_pTrack1->findHotcueByIndex(0));
}

TEST_F(LoopUndoTest, NewTrackDropsItsDecksCueHistory) {
    addHotcue(0, 1000);
    EXPECT_LT(0, m_pLoopUndo->stepCount());

    m_pTrack1 = m_pMixerDeck1->loadFakeTrack(false, 0.0);
    m_pLoopUndo->setTrack(0, m_pTrack1);
    m_pLoopUndo->flushPending();
    EXPECT_EQ(0, m_pLoopUndo->stepCount());
}

TEST_F(LoopUndoTest, NothingToUndoChangesNothing) {
    pressUndo();
    expectLoop1(-1, -1);
    EXPECT_EQ(0, m_pLoopUndo->stepCount());
}

} // namespace
