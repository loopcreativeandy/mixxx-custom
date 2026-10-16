#include "library/crowdrating/crowdratingsession.h"

#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

#include "control/controlobject.h"
#include "mixer/playerinfo.h"
#include "test/mixxxtest.h"
#include "track/track.h"

namespace {

const QString kDeck1 = QStringLiteral("[Channel1]");
const QString kDeck2 = QStringLiteral("[Channel2]");

} // namespace

class CrowdRatingSessionTest : public MixxxTest {
  protected:
    void SetUp() override {
        for (const char* item : {"num_decks", "num_samplers", "num_preview_decks"}) {
            m_controls.push_back(std::make_unique<ControlObject>(
                    ConfigKey(QStringLiteral("[App]"), QString::fromLatin1(item))));
        }
        ControlObject::set(ConfigKey(QStringLiteral("[App]"), QStringLiteral("num_decks")), 2);
        m_controls.push_back(std::make_unique<ControlObject>(
                ConfigKey(QStringLiteral("[Master]"), QStringLiteral("crossfader"))));
        for (const QString& group : {kDeck1, kDeck2}) {
            for (const char* item :
                    {"play", "volume", "pregain", "orientation", "playposition", "duration"}) {
                m_controls.push_back(std::make_unique<ControlObject>(
                        ConfigKey(group, QString::fromLatin1(item))));
            }
            ControlObject::set(ConfigKey(group, "pregain"), 1.0);
            ControlObject::set(ConfigKey(group, "orientation"), 1.0); // centre
            ControlObject::set(ConfigKey(group, "duration"), 200.0);
        }
        PlayerInfo::create();
        m_pSession = std::make_unique<CrowdRatingSession>(config(), nullptr);

        m_pTrackA = Track::newDummy(QStringLiteral("/music/a.mp3"), TrackId(QVariant(11)));
        m_pTrackA->setArtist(QStringLiteral("Artist A"));
        m_pTrackA->setTitle(QStringLiteral("Title A"));
        m_pTrackB = Track::newDummy(QStringLiteral("/music/b.mp3"), TrackId(QVariant(22)));
        m_pTrackB->setTitle(QStringLiteral("Title B"));

        m_filePath = QDir(config()->getSettingsPath()).filePath("crowd_test.jsonl");
        QFile::remove(m_filePath);
    }

    void TearDown() override {
        m_pSession.reset();
        PlayerInfo::destroy();
        m_controls.clear();
    }

    void play(const QString& group, double volume) {
        ControlObject::set(ConfigKey(group, "play"), volume > 0 ? 1.0 : 0.0);
        ControlObject::set(ConfigKey(group, "volume"), volume);
    }

    void press(const char* item) {
        const ConfigKey key(QStringLiteral("[CrowdRating]"), QString::fromLatin1(item));
        ControlObject::set(key, 1.0);
        ControlObject::set(key, 0.0);
    }

    double tint(const QString& group) {
        return ControlObject::get(ConfigKey(group, "crowd_rating"));
    }

    QList<QJsonObject> entries() {
        QList<QJsonObject> result;
        QFile file(m_filePath);
        EXPECT_TRUE(file.open(QIODevice::ReadOnly | QIODevice::Text));
        while (!file.atEnd()) {
            result.append(QJsonDocument::fromJson(file.readLine()).object());
        }
        return result;
    }

    std::vector<std::unique_ptr<ControlObject>> m_controls;
    std::unique_ptr<CrowdRatingSession> m_pSession;
    TrackPointer m_pTrackA;
    TrackPointer m_pTrackB;
    QString m_filePath;
};

TEST_F(CrowdRatingSessionTest, RatesLoudestDeckAndTintsIt) {
    ASSERT_TRUE(m_pSession->startSession(QStringLiteral("Fusion Waves"),
            QStringLiteral("Fusion"), m_filePath, false));
    PlayerInfo::instance().setTrackInfo(kDeck1, m_pTrackA);
    PlayerInfo::instance().setTrackInfo(kDeck2, m_pTrackB);
    play(kDeck1, 0.3);
    play(kDeck2, 1.0);
    ControlObject::set(ConfigKey(kDeck2, "playposition"), 0.5);

    press("rate_plus");
    EXPECT_EQ(0.0, tint(kDeck1));
    EXPECT_EQ(1.0, tint(kDeck2));

    // Contradicting press: logged too, the tint follows the latest one.
    press("rate_minus");
    EXPECT_EQ(2.0, tint(kDeck2));

    const auto log = entries();
    ASSERT_EQ(3, log.size());
    EXPECT_EQ("session_start", log[0]["kind"].toString());
    EXPECT_EQ("Fusion Waves", log[0]["event"].toString());
    EXPECT_EQ("Fusion", log[0]["style"].toString());
    EXPECT_EQ("Fusion", log[1]["style"].toString());
    EXPECT_FALSE(log[0]["t"].toString().isEmpty());
    EXPECT_EQ("plus", log[1]["kind"].toString());
    EXPECT_EQ(2, log[1]["deck"].toInt());
    EXPECT_EQ(22, log[1]["track_id"].toInt());
    EXPECT_EQ("Title B", log[1]["title"].toString());
    EXPECT_EQ("/music/b.mp3", log[1]["path"].toString());
    EXPECT_EQ(100.0, log[1]["pos_s"].toDouble());
    EXPECT_EQ("control", log[1]["source"].toString());
    EXPECT_EQ("minus", log[2]["kind"].toString());
}

TEST_F(CrowdRatingSessionTest, NothingAudibleOrNoSessionWritesNothing) {
    PlayerInfo::instance().setTrackInfo(kDeck1, m_pTrackA);
    play(kDeck1, 1.0);
    press("rate_plus"); // no session yet
    ASSERT_TRUE(m_pSession->startSession(QStringLiteral("Test"), QStringLiteral("Zouk"), m_filePath, false));
    play(kDeck1, 0.0);
    press("rate_plus"); // nothing audible
    EXPECT_EQ(1, entries().size());
    EXPECT_EQ(0.0, tint(kDeck1));
}

TEST_F(CrowdRatingSessionTest, LogsPlayAfterThirtyAudibleSeconds) {
    ASSERT_TRUE(m_pSession->startSession(QStringLiteral("Test"), QStringLiteral("Zouk"), m_filePath, false));
    PlayerInfo::instance().setTrackInfo(kDeck1, m_pTrackA);
    play(kDeck1, 1.0);
    m_pSession->advance(29 * 1000);
    EXPECT_EQ(1, entries().size());
    // Faded out: does not count.
    play(kDeck1, 0.05);
    m_pSession->advance(10 * 1000);
    EXPECT_EQ(1, entries().size());
    play(kDeck1, 1.0);
    m_pSession->advance(1000);
    auto log = entries();
    ASSERT_EQ(2, log.size());
    EXPECT_EQ("play", log[1]["kind"].toString());
    EXPECT_EQ(1, log[1]["deck"].toInt());
    EXPECT_EQ("Artist A", log[1]["artist"].toString());
    EXPECT_FALSE(log[1]["audible_since"].toString().isEmpty());
    // Only once per load.
    m_pSession->advance(60 * 1000);
    EXPECT_EQ(2, entries().size());
    // A reload counts again.
    PlayerInfo::instance().setTrackInfo(kDeck1, m_pTrackA);
    m_pSession->advance(30 * 1000);
    EXPECT_EQ(3, entries().size());
}

TEST_F(CrowdRatingSessionTest, MenuRatingAndResumeRestoresTint) {
    ASSERT_TRUE(m_pSession->startSession(QStringLiteral("Test"), QStringLiteral("Zouk"), m_filePath, false));
    m_pSession->rateTracks({m_pTrackA}, CrowdRatingSession::Kind::Neutral);
    m_pSession->endSession();
    auto log = entries();
    ASSERT_EQ(3, log.size());
    EXPECT_EQ("neutral", log[1]["kind"].toString());
    EXPECT_EQ("menu", log[1]["source"].toString());
    EXPECT_EQ("session_end", log[2]["kind"].toString());

    // Loading the rated track tints the deck only while a session runs.
    PlayerInfo::instance().setTrackInfo(kDeck1, m_pTrackA);
    EXPECT_EQ(0.0, tint(kDeck1));
    ASSERT_TRUE(m_pSession->startSession(QStringLiteral("Test"), QStringLiteral("Zouk"), m_filePath, true));
    EXPECT_EQ(3.0, tint(kDeck1));
    EXPECT_EQ("session_resume", entries().last()["kind"].toString());
    PlayerInfo::instance().setTrackInfo(kDeck1, m_pTrackB);
    EXPECT_EQ(0.0, tint(kDeck1));
}
