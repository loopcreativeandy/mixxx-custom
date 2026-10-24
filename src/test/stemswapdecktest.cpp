// andy-custom (CP97): end-to-end check of the stem swap through real decks and
// the real engine - the part the pure stemswap math tests cannot see.

#include <QCoreApplication>

#include "control/controlobject.h"
#include "engine/enginebuffer.h"
#include "library/stemswap.h"
#include "test/signalpathtest.h"

namespace {

class StemSwapDeckTest : public BaseSignalPathTest {
  protected:
    TrackPointer newSineTrack() {
        return Track::newTemporary(getTestDir().filePath(QStringLiteral("sine-30.wav")));
    }

    void settle() {
        for (int i = 0; i < 4; ++i) {
            QCoreApplication::processEvents();
            ProcessBuffer();
        }
    }

    EngineBuffer* buffer(const std::unique_ptr<Deck>& pDeck) {
        return pDeck->getEngineDeck()->getEngineBuffer();
    }

    bool waitForTrack(const std::unique_ptr<Deck>& pDeck, const TrackPointer& pTrack) {
        for (int i = 0; i < 2000; ++i) {
            // The engine's track, not the deck's: the deck reports the new
            // track as soon as the load starts.
            if (buffer(pDeck)->getLoadedTrack() == pTrack && buffer(pDeck)->isTrackLoaded()) {
                settle();
                return true;
            }
            QCoreApplication::processEvents();
            ProcessBuffer();
            QTest::qSleep(1);
        }
        return false;
    }
};

TEST_F(StemSwapDeckTest, CounterpartLandsAtTheOffsetPositionWithTheSameTempo) {
    loadTrack(m_pMixerDeck1.get(), newSineTrack());
    const double sampleRate = ControlObject::get(ConfigKey(m_sGroup1, "track_samplerate"));
    ASSERT_GT(sampleRate, 0.0);

    // Deck 1 is pitched and playing ten seconds in.
    ControlObject::set(ConfigKey(m_sGroup1, "rate_ratio"), 1.05);
    buffer(m_pMixerDeck1)->seekExact(mixxx::audio::FramePos(10.0 * sampleRate));
    ControlObject::set(ConfigKey(m_sGroup1, "play"), 1.0);
    settle();

    constexpr double kOffsetSeconds = 0.25;
    m_pMixerDeck2->loadCounterpartAligned(newSineTrack(), m_sGroup1, kOffsetSeconds);
    for (int i = 0; i < 2000 && !buffer(m_pMixerDeck2)->isTrackLoaded(); ++i) {
        ProcessBuffer();
        QTest::qSleep(1);
    }
    ASSERT_TRUE(buffer(m_pMixerDeck2)->isTrackLoaded());
    settle();

    // Same file on both decks, so the only difference must be the offset -
    // both decks then advance by the same amount per callback.
    const double pos1 = buffer(m_pMixerDeck1)->getExactPlayPos().value();
    const double pos2 = buffer(m_pMixerDeck2)->getExactPlayPos().value();
    EXPECT_GT(pos1, 10.0 * sampleRate);
    EXPECT_NEAR(kOffsetSeconds * sampleRate, pos2 - pos1, 2.0);

    // Clone semantics: tempo and play state follow the playing deck.
    EXPECT_DOUBLE_EQ(1.05, ControlObject::get(ConfigKey(m_sGroup2, "rate_ratio")));
    EXPECT_DOUBLE_EQ(1.0, ControlObject::get(ConfigKey(m_sGroup2, "play")));
}

/// The time-domain request is consumed by its one seek: an ordinary Clone Deck
/// afterwards must copy the position unshifted, like upstream.
TEST_F(StemSwapDeckTest, PlainCloneAfterASwapIsUnshifted) {
    loadTrack(m_pMixerDeck1.get(), newSineTrack());
    const double sampleRate = ControlObject::get(ConfigKey(m_sGroup1, "track_samplerate"));
    ASSERT_GT(sampleRate, 0.0);
    buffer(m_pMixerDeck1)->seekExact(mixxx::audio::FramePos(5.0 * sampleRate));
    ControlObject::set(ConfigKey(m_sGroup1, "play"), 1.0);
    settle();

    m_pMixerDeck2->loadCounterpartAligned(newSineTrack(), m_sGroup1, 0.25);
    for (int i = 0; i < 2000 && !buffer(m_pMixerDeck2)->isTrackLoaded(); ++i) {
        ProcessBuffer();
        QTest::qSleep(1);
    }
    settle();

    m_pMixerDeck3->slotCloneFromGroup(m_sGroup1);
    for (int i = 0; i < 2000 && !buffer(m_pMixerDeck3)->isTrackLoaded(); ++i) {
        ProcessBuffer();
        QTest::qSleep(1);
    }
    settle();

    const double pos1 = buffer(m_pMixerDeck1)->getExactPlayPos().value();
    const double pos3 = buffer(m_pMixerDeck3)->getExactPlayPos().value();
    EXPECT_GT(pos1, 5.0 * sampleRate);
    EXPECT_NEAR(0.0, pos3 - pos1, 2.0);
}

/// Andy (2026-10-24): a stopped deck gets the counterpart in place - same
/// deck, same moment shifted by the offset, tempo kept, still stopped.
TEST_F(StemSwapDeckTest, StoppedDeckIsReplacedInPlace) {
    loadTrack(m_pMixerDeck1.get(), newSineTrack());
    const double sampleRate = ControlObject::get(ConfigKey(m_sGroup1, "track_samplerate"));
    ASSERT_GT(sampleRate, 0.0);
    ControlObject::set(ConfigKey(m_sGroup1, "rate_ratio"), 1.05);
    buffer(m_pMixerDeck1)->seekExact(mixxx::audio::FramePos(10.0 * sampleRate));
    settle();

    constexpr double kOffsetSeconds = 0.25;
    const TrackPointer pCounterpart = newSineTrack();
    m_pMixerDeck1->loadCounterpartAligned(pCounterpart, m_sGroup1, kOffsetSeconds);
    ASSERT_TRUE(waitForTrack(m_pMixerDeck1, pCounterpart));

    EXPECT_NEAR((10.0 + kOffsetSeconds) * sampleRate,
            buffer(m_pMixerDeck1)->getExactPlayPos().value(),
            2.0);
    EXPECT_DOUBLE_EQ(1.05, ControlObject::get(ConfigKey(m_sGroup1, "rate_ratio")));
    EXPECT_DOUBLE_EQ(0.0, ControlObject::get(ConfigKey(m_sGroup1, "play")));
}

TEST_F(StemSwapDeckTest, PlayingDeckIsNeverReplacedInPlace) {
    const TrackPointer pTrack = newSineTrack();
    loadTrack(m_pMixerDeck1.get(), pTrack);
    ControlObject::set(ConfigKey(m_sGroup1, "play"), 1.0);
    settle();

    m_pMixerDeck1->loadCounterpartAligned(newSineTrack(), m_sGroup1, 0.25);
    settle();
    EXPECT_EQ(pTrack, m_pMixerDeck1->getLoadedTrack());
}

/// The in-place request is consumed by its load: the next ordinary load
/// starts at the beginning again.
TEST_F(StemSwapDeckTest, PlainLoadAfterAnInPlaceSwapStartsAtTheTop) {
    loadTrack(m_pMixerDeck1.get(), newSineTrack());
    const double sampleRate = ControlObject::get(ConfigKey(m_sGroup1, "track_samplerate"));
    ASSERT_GT(sampleRate, 0.0);
    buffer(m_pMixerDeck1)->seekExact(mixxx::audio::FramePos(10.0 * sampleRate));
    settle();
    const TrackPointer pCounterpart = newSineTrack();
    m_pMixerDeck1->loadCounterpartAligned(pCounterpart, m_sGroup1, 0.0);
    ASSERT_TRUE(waitForTrack(m_pMixerDeck1, pCounterpart));
    ASSERT_GT(buffer(m_pMixerDeck1)->getExactPlayPos().value(), 9.0 * sampleRate);

    const TrackPointer pNext = newSineTrack();
    m_pMixerDeck1->slotLoadTrack(pNext,
#ifdef __STEM__
            mixxx::StemChannelSelection(),
#endif
            false);
    ASSERT_TRUE(waitForTrack(m_pMixerDeck1, pNext));
    EXPECT_LT(buffer(m_pMixerDeck1)->getExactPlayPos().value(), sampleRate);
}

} // namespace
