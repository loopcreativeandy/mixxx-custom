#include <gtest/gtest.h>

#include <QTest>
#include <gsl/pointers>

#include "control/controlindicatortimer.h"
#include "database/mixxxdb.h"
#include "effects/effectsmanager.h"
#include "engine/channels/enginedeck.h"
#include "engine/enginebuffer.h"
#include "engine/enginemixer.h"
#include "library/coverartcache.h"
#include "library/library.h"
#include "library/trackcollectionmanager.h"
#include "mixer/basetrackplayer.h"
#include "mixer/deck.h"
#include "mixer/playerinfo.h"
#include "mixer/playermanager.h"
#include "sources/soundsourceproxy.h"
#include "test/mixxxdbtest.h"
#include "test/soundsourceproviderregistration.h"
#include "track/track.h"
#include "util/cmdlineargs.h"
#ifdef __RUBBERBAND__
#include "engine/bufferscalers/rubberbandworkerpool.h"
#endif

namespace {

const QString kTrackLocationTest1 = QStringLiteral("id3-test-data/cover-test-øé~ł€˚-png.mp3");
const QString kTrackLocationTest2 = QStringLiteral("id3-test-data/cover-test-øé~ł€˚-vbr.mp3");

void deleteTrack(Track* pTrack) {
    // Delete track objects directly in unit tests with
    // no main event loop
    delete pTrack;
};

void waitForTrackToBeLoaded(Deck* pDeck) {
    while (!pDeck->getEngineDeck()->getEngineBuffer()->isTrackLoaded()) {
        QTest::qSleep(100); // millis
    }
}

} // namespace

// We can't inherit from LibraryTest because that creates a key_notation control object that is also
// created by the Library object itself. The duplicated CO creation causes a debug assert.
class PlayerManagerTest : public MixxxDbTest, SoundSourceProviderRegistration {
  public:
    PlayerManagerTest()
            : MixxxDbTest(true) {
    }

    void SetUp() override {
        // This setup mirrors coreservices -- it would be nice if we could use coreservices instead
        // but it does a lot of local disk / settings setup.
        auto pChannelHandleFactory = std::make_shared<ChannelHandleFactory>();
        m_pEffectsManager = std::make_shared<EffectsManager>(m_pConfig, pChannelHandleFactory);
        m_pEngine = std::make_shared<EngineMixer>(
                m_pConfig,
                "[Master]",
                m_pEffectsManager.get(),
                pChannelHandleFactory,
                true);
        m_pSoundManager = std::make_shared<SoundManager>(m_pConfig, m_pEngine.get());
        m_pControlIndicatorTimer = std::make_shared<mixxx::ControlIndicatorTimer>(nullptr);
        m_pEngine->registerNonEngineChannelSoundIO(gsl::make_not_null(m_pSoundManager.get()));

        CoverArtCache::createInstance();

        m_pPlayerManager = std::make_shared<PlayerManager>(m_pConfig,
                m_pSoundManager.get(),
                m_pEffectsManager.get(),
                m_pEngine.get());

        m_pPlayerManager->addConfiguredDecks();
        m_pPlayerManager->addSampler();
        PlayerInfo::create();
        m_pEffectsManager->setup();

        const auto dbConnection = mixxx::DbConnectionPooled(dbConnectionPooler());
        if (!MixxxDb::initDatabaseSchema(dbConnection)) {
            exit(1);
        }
        m_pTrackCollectionManager = std::make_unique<TrackCollectionManager>(
                nullptr,
                m_pConfig,
                dbConnectionPooler(),
                deleteTrack);

        m_pRecordingManager = std::make_shared<RecordingManager>(m_pConfig, m_pEngine.get());
        m_pLibrary = std::make_shared<Library>(
                nullptr,
                m_pConfig,
                dbConnectionPooler(),
                m_pTrackCollectionManager.get(),
                m_pPlayerManager.get(),
                m_pRecordingManager.get());

        m_pPlayerManager->bindToLibrary(m_pLibrary.get());
        RubberBandWorkerPool::createInstance();
    }

    void TearDown() override {
        CoverArtCache::destroy();
#ifdef __RUBBERBAND__
        RubberBandWorkerPool::destroy();
#endif
    }

    ~PlayerManagerTest() {
        m_pSoundManager.reset();
        m_pPlayerManager.reset();
        PlayerInfo::destroy();
        m_pLibrary.reset();
        m_pRecordingManager.reset();
        m_pEngine.reset();
        m_pEffectsManager.reset();
        m_pTrackCollectionManager.reset();
        m_pControlIndicatorTimer.reset();
    }

  protected:
    TrackPointer getOrAddTrackByLocation(
            const QString& trackLocation) const {
        return m_pTrackCollectionManager->getOrAddTrack(
                TrackRef::fromFilePath(trackLocation));
    }

    std::shared_ptr<EffectsManager> m_pEffectsManager;
    std::shared_ptr<mixxx::ControlIndicatorTimer> m_pControlIndicatorTimer;
    std::shared_ptr<EngineMixer> m_pEngine;
    std::shared_ptr<SoundManager> m_pSoundManager;
    std::shared_ptr<PlayerManager> m_pPlayerManager;
    std::unique_ptr<TrackCollectionManager> m_pTrackCollectionManager;
    std::shared_ptr<RecordingManager> m_pRecordingManager;
    std::shared_ptr<Library> m_pLibrary;
};

TEST_F(PlayerManagerTest, UnEjectTest) {
    // Ejecting an empty deck with no previously-recorded ejected track has no effect.
    auto deck1 = m_pPlayerManager->getDeck(0);
    deck1->slotEjectTrack(1.0);
    ASSERT_EQ(nullptr, deck1->getLoadedTrack());

    // Load a track and eject it
    TrackPointer pTrack1 = getOrAddTrackByLocation(getTestDir().filePath(kTrackLocationTest1));
    ASSERT_NE(nullptr, pTrack1);
    TrackId testId1 = pTrack1->getId();
    ASSERT_TRUE(testId1.isValid());
    deck1->slotLoadTrack(pTrack1,
#ifdef __STEM__
            mixxx::StemChannelSelection(),
#endif
            false);
    ASSERT_NE(nullptr, deck1->getLoadedTrack());

    m_pEngine->process(1024);
    waitForTrackToBeLoaded(deck1);
    // make sure eject does not trigger 'unreplace':
    // sleep for longer than 500 ms 'unreplace' period so this is not registered as double-click
    QTest::qSleep(kUnreplaceDelay); // millis
    deck1->slotEjectTrack(1.0);

    // Load another track.
    TrackPointer pTrack2 = getOrAddTrackByLocation(getTestDir().filePath(kTrackLocationTest2));
    ASSERT_NE(nullptr, pTrack2);
    deck1->slotLoadTrack(pTrack2,
#ifdef __STEM__
            mixxx::StemChannelSelection(),
#endif
            false);

    // Ejecting in an empty deck loads the last-ejected track.
    auto deck2 = m_pPlayerManager->getDeck(1);
    ASSERT_EQ(nullptr, deck2->getLoadedTrack());
    // make sure eject does not trigger 'unreplace'
    QTest::qSleep(kUnreplaceDelay); // millis
    deck2->slotEjectTrack(2.0);
    ASSERT_NE(nullptr, deck2->getLoadedTrack());
    ASSERT_EQ(testId1, deck2->getLoadedTrack()->getId());
}

// Loading a new track in a deck causes the old one to be ejected.
// That old track can be unejected into a different deck.
TEST_F(PlayerManagerTest, UnEjectReplaceTrackTest) {
    auto deck1 = m_pPlayerManager->getDeck(0);
    // Load a track and the load another one
    TrackPointer pTrack1 = getOrAddTrackByLocation(getTestDir().filePath(kTrackLocationTest1));
    ASSERT_NE(nullptr, pTrack1);
    TrackId testId1 = pTrack1->getId();
    ASSERT_TRUE(testId1.isValid());
    deck1->slotLoadTrack(pTrack1,
#ifdef __STEM__
            mixxx::StemChannelSelection(),
#endif
            false);
    ASSERT_NE(nullptr, deck1->getLoadedTrack());

    m_pEngine->process(1024);
    waitForTrackToBeLoaded(deck1);

    // Load another track, replacing the first, causing it to be unloaded.
    TrackPointer pTrack2 = getOrAddTrackByLocation(getTestDir().filePath(kTrackLocationTest2));
    ASSERT_NE(nullptr, pTrack2);
    deck1->slotLoadTrack(pTrack2,
#ifdef __STEM__
            mixxx::StemChannelSelection(),
#endif
            false);
    m_pEngine->process(1024);
    waitForTrackToBeLoaded(deck1);

    // Ejecting in an empty deck loads the last-ejected track.
    auto deck2 = m_pPlayerManager->getDeck(1);
    ASSERT_EQ(nullptr, deck2->getLoadedTrack());
    // make sure eject does not trigger 'unreplace'
    QTest::qSleep(kUnreplaceDelay);
    deck2->slotEjectTrack(1.0);
    ASSERT_NE(nullptr, deck2->getLoadedTrack());
    ASSERT_EQ(testId1, deck2->getLoadedTrack()->getId());
}

TEST_F(PlayerManagerTest, UnEjectInvalidTrackIdTest) {
    // Save an invalid trackid in playermanager.
    auto pTrack = Track::newDummy(
            getTestDir().filePath(kTrackLocationTest1), TrackId(QVariant(10)));
    ASSERT_NE(nullptr, pTrack);
    m_pPlayerManager->slotSaveEjectedTrack(pTrack);
    auto deck1 = m_pPlayerManager->getDeck(0);
    // Does nothing -- no crash.
    // make sure eject does not trigger 'unreplace'
    QTest::qSleep(kUnreplaceDelay);
    deck1->slotEjectTrack(1.0);
    ASSERT_EQ(nullptr, deck1->getLoadedTrack());
}

TEST_F(PlayerManagerTest, UnReplaceTest) {
    // Trigger eject twice within 500 ms to undo track replacement
    auto deck1 = m_pPlayerManager->getDeck(0);
    // Load a track
    TrackPointer pTrack1 = getOrAddTrackByLocation(getTestDir().filePath(kTrackLocationTest1));
    ASSERT_NE(nullptr, pTrack1);
    TrackId testId1 = pTrack1->getId();
    ASSERT_TRUE(testId1.isValid());
    deck1->slotLoadTrack(pTrack1,
#ifdef __STEM__
            mixxx::StemChannelSelection(),
#endif
            false);
    m_pEngine->process(1024);
    waitForTrackToBeLoaded(deck1);
    ASSERT_NE(nullptr, deck1->getLoadedTrack());

    // Load another track.
    TrackPointer pTrack2 = getOrAddTrackByLocation(getTestDir().filePath(kTrackLocationTest2));
    ASSERT_NE(nullptr, pTrack2);
    deck1->slotLoadTrack(pTrack2,
#ifdef __STEM__
            mixxx::StemChannelSelection(),
#endif
            false);
    m_pEngine->process(1024);
    waitForTrackToBeLoaded(deck1);
    ASSERT_NE(nullptr, deck1->getLoadedTrack());

    // Eject. Make sure eject does not trigger 'unreplace':
    // sleep for longer than 500 ms 'unreplace' period so this is not registered as double-click
    QTest::qSleep(kUnreplaceDelay); // millis
    deck1->slotEjectTrack(1.0);
    ASSERT_EQ(nullptr, deck1->getLoadedTrack());

    // Eject again, assume this is reached faster than 500 ms after first eject
    deck1->slotEjectTrack(1.0);
    // First track should be reloaded
    ASSERT_NE(nullptr, deck1->getLoadedTrack());
    ASSERT_EQ(testId1, deck1->getLoadedTrack()->getId());
}

namespace {

/// `PlayerManager::chooseLoadTargetDeck()` — Andy's load-target rule (CP94).
/// Pure, so no PlayerManager instance and no engine are needed.
constexpr bool kPlaying = true;
constexpr bool kStopped = false;
/// A loaded, fully audible deck.
const std::optional<double> kAudible = 1.0;
/// A loaded deck that is playing but cannot be heard (fader/EQs/stems down).
const std::optional<double> kSilent = 0.0;
/// No track in the deck.
const std::optional<double> kEmpty = std::nullopt;

int chooseTarget(const std::vector<bool>& playing,
        const std::vector<std::optional<double>>& loudness,
        int lastTarget) {
    return PlayerManager::chooseLoadTargetDeck(playing, loudness, lastTarget);
}

TEST(LoadTargetDeckTest, FirstLoadGoesToDeckOne) {
    EXPECT_EQ(0, chooseTarget({kStopped, kStopped}, {kEmpty, kEmpty}, -1));
}

TEST(LoadTargetDeckTest, RepeatedLoadsAlternate) {
    // Andy's ask: 1, 2, 1, 2, … while nothing plays — not 1, 2, 1, 1, …
    int last = -1;
    std::vector<int> targets;
    for (int i = 0; i < 5; ++i) {
        last = chooseTarget({kStopped, kStopped}, {kEmpty, kAudible}, last);
        targets.push_back(last);
    }
    EXPECT_EQ(std::vector<int>({0, 1, 0, 1, 0}), targets);
}

TEST(LoadTargetDeckTest, FourDecksRotate) {
    int last = -1;
    std::vector<int> targets;
    for (int i = 0; i < 5; ++i) {
        last = chooseTarget({kStopped, kStopped, kStopped, kStopped},
                {kEmpty, kEmpty, kEmpty, kEmpty},
                last);
        targets.push_back(last);
    }
    EXPECT_EQ(std::vector<int>({0, 1, 2, 3, 0}), targets);
}

TEST(LoadTargetDeckTest, PlayingDecksAreSkipped) {
    // Deck 1 is live: every load has to go to deck 2, over and over.
    int last = -1;
    for (int i = 0; i < 3; ++i) {
        last = chooseTarget({kPlaying, kStopped}, {kAudible, kAudible}, last);
        EXPECT_EQ(1, last);
    }
}

TEST(LoadTargetDeckTest, RotationSkipsPastAPlayingDeck) {
    // 4 decks, deck 3 live, last load went to deck 2 -> next free is deck 4.
    EXPECT_EQ(3,
            chooseTarget({kStopped, kStopped, kPlaying, kStopped},
                    {kAudible, kAudible, kAudible, kAudible},
                    1));
}

TEST(LoadTargetDeckTest, AllPlayingAndAudibleRefusesToLoad) {
    EXPECT_EQ(-1, chooseTarget({kPlaying, kPlaying}, {kAudible, kAudible}, 0));
}

TEST(LoadTargetDeckTest, AllPlayingReplacesTheMutedDeck) {
    // The whole point of the second half of the rule: no need to stop the
    // muted deck by hand first (and risk stopping the wrong one).
    EXPECT_EQ(1, chooseTarget({kPlaying, kPlaying}, {kAudible, kSilent}, 0));
    EXPECT_EQ(0, chooseTarget({kPlaying, kPlaying}, {kSilent, kAudible}, 0));
}

TEST(LoadTargetDeckTest, AlmostSilentStillCountsAsAudible) {
    // Only a deck at/below kMutedLoudness may be loaded over — a deck that is
    // merely quiet is still someone's mix.
    EXPECT_EQ(-1,
            chooseTarget({kPlaying, kPlaying},
                    {kAudible, PlayerManager::kMutedLoudness * 10.0},
                    0));
    EXPECT_EQ(1,
            chooseTarget({kPlaying, kPlaying},
                    {kAudible, PlayerManager::kMutedLoudness},
                    0));
}

TEST(LoadTargetDeckTest, TwoMutedDecksTakeTheQuieterOne) {
    EXPECT_EQ(2,
            chooseTarget({kPlaying, kPlaying, kPlaying},
                    {kAudible, PlayerManager::kMutedLoudness, 0.0},
                    0));
}

TEST(LoadTargetDeckTest, StoppedDeckWinsOverAMutedPlayingOne) {
    // Pass 1 always runs first: nothing gets replaced while a deck is free.
    EXPECT_EQ(1, chooseTarget({kPlaying, kStopped}, {kSilent, kAudible}, 0));
}

TEST(LoadTargetDeckTest, StaleOrEmptyRotationStateIsSafe) {
    // Deck count shrank (4 -> 2) with the rotation still pointing at deck 4.
    EXPECT_EQ(0, chooseTarget({kStopped, kStopped}, {kEmpty, kEmpty}, 3));
    // No decks at all.
    EXPECT_EQ(-1, chooseTarget({}, {}, -1));
}

} // namespace
