#include "library/crowdrating/crowdratingsession.h"

#include <QDir>
#include <QFileInfo>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLabel>
#include <QMessageBox>
#include <QRegularExpression>
#include <QWidget>

#include "control/controlobject.h"
#include "control/controlpushbutton.h"
#include "engine/channels/enginechannel.h"
#include "engine/enginexfader.h"
#include "mixer/playerinfo.h"
#include "mixer/playermanager.h"
#include "moc_crowdratingsession.cpp"
#include "track/track.h"
#include "util/defs.h"

namespace {

CrowdRatingSession* s_pInstance = nullptr;

const QString kGroup = QStringLiteral("[CrowdRating]");
const QString kConfigGroup = QStringLiteral("[CrowdRating]");
const ConfigKey kLastFileKey(kConfigGroup, QStringLiteral("last_file"));
const ConfigKey kLastEventKey(kConfigGroup, QStringLiteral("last_event"));
const ConfigKey kLastStyleKey(kConfigGroup, QStringLiteral("last_style"));

// A track counts as played once it was audible on the main mix this long.
constexpr qint64 kPlayedMillis = 30 * 1000;
// Below this main-mix gain (volume fader x crossfader) a deck is not audible.
constexpr double kAudibleGain = 0.1;
// Same pregain cut-off PlayerInfo uses for the "currently playing" deck.
constexpr double kMinPregain = 0.25;
constexpr int kTickMillis = 1000;
constexpr int kToastMillis = 2500;
constexpr qint64 kResumeWindowSecs = 6 * 60 * 60;

const QColor kColorPlus(0x35, 0xD0, 0x7A);
const QColor kColorMinus(0xFF, 0x45, 0x45);
const QColor kColorNeutral(0xFF, 0xD6, 0x00);
const QColor kColorInfo(0x9E, 0x9E, 0x9E);

QString timestamp(const QDateTime& dt) {
    return dt.toOffsetFromUtc(dt.offsetFromUtc()).toString(Qt::ISODateWithMs);
}

QString fileSlug(const QString& eventName) {
    static const QRegularExpression kNonWord(QStringLiteral("[^\\w]+"));
    static const QRegularExpression kEdgeDashes(QStringLiteral("^-+|-+$"));
    QString slug = eventName.toLower();
    slug.replace(kNonWord, QStringLiteral("-"));
    slug.remove(kEdgeDashes);
    return slug.isEmpty() ? QStringLiteral("session") : slug;
}

QString trackLabel(const TrackPointer& pTrack) {
    const QString artist = pTrack->getArtist();
    const QString title = pTrack->getTitle();
    if (title.isEmpty()) {
        return pTrack->getInfo();
    }
    return artist.isEmpty() ? title : artist + QStringLiteral(" - ") + title;
}

} // namespace

CrowdRatingSession::CrowdRatingSession(UserSettingsPointer pConfig, QWidget* pMainWindow)
        : m_pConfig(pConfig),
          m_pMainWindow(pMainWindow),
          m_pRatePlus(std::make_unique<ControlPushButton>(
                  ConfigKey(kGroup, QStringLiteral("rate_plus")))),
          m_pRateMinus(std::make_unique<ControlPushButton>(
                  ConfigKey(kGroup, QStringLiteral("rate_minus")))),
          m_pRateNeutral(std::make_unique<ControlPushButton>(
                  ConfigKey(kGroup, QStringLiteral("rate_neutral")))),
          m_pSessionActive(std::make_unique<ControlObject>(
                  ConfigKey(kGroup, QStringLiteral("session_active")))),
          m_xfader(QStringLiteral("[Master]"), QStringLiteral("crossfader")),
          m_active(false),
          m_pToast(nullptr) {
    DEBUG_ASSERT(!s_pInstance);
    s_pInstance = this;

    m_pSessionActive->setReadOnly();

    connect(m_pRatePlus.get(), &ControlObject::valueChanged, this, [this](double v) {
        if (v > 0) {
            rateAudible(Kind::Plus);
        }
    });
    connect(m_pRateMinus.get(), &ControlObject::valueChanged, this, [this](double v) {
        if (v > 0) {
            rateAudible(Kind::Minus);
        }
    });
    connect(m_pRateNeutral.get(), &ControlObject::valueChanged, this, [this](double v) {
        if (v > 0) {
            rateAudible(Kind::Neutral);
        }
    });

    // One tint CO per possible deck, created up front so skins can bind to
    // them before the deck count changes.
    m_decks.resize(kMaxNumberOfDecks);
    for (int i = 0; i < kMaxNumberOfDecks; ++i) {
        DeckState& deck = m_decks[i];
        deck.group = PlayerManager::groupForDeck(i);
        deck.pRatingCO = std::make_unique<ControlObject>(
                ConfigKey(deck.group, QStringLiteral("crowd_rating")));
        deck.pRatingCO->setReadOnly();
    }

    connect(&PlayerInfo::instance(),
            &PlayerInfo::trackChanged,
            this,
            &CrowdRatingSession::slotTrackChanged);

    m_tickTimer.setInterval(kTickMillis);
    connect(&m_tickTimer, &QTimer::timeout, this, &CrowdRatingSession::slotTick);

    m_toastTimer.setSingleShot(true);
    m_toastTimer.setInterval(kToastMillis);
    connect(&m_toastTimer, &QTimer::timeout, this, [this]() {
        if (m_pToast) {
            m_pToast->hide();
        }
    });
}

CrowdRatingSession::~CrowdRatingSession() {
    // Mixxx is closing: that ends the session.
    endSession();
    s_pInstance = nullptr;
}

// static
CrowdRatingSession* CrowdRatingSession::instance() {
    return s_pInstance;
}

// static
QString CrowdRatingSession::kindName(Kind kind) {
    switch (kind) {
    case Kind::Plus:
        return QStringLiteral("plus");
    case Kind::Minus:
        return QStringLiteral("minus");
    case Kind::Neutral:
        return QStringLiteral("neutral");
    }
    return QString();
}

// static
double CrowdRatingSession::tintValue(Kind kind) {
    switch (kind) {
    case Kind::Plus:
        return 1;
    case Kind::Minus:
        return 2;
    case Kind::Neutral:
        return 3;
    }
    return 0;
}

void CrowdRatingSession::slotStartOrEndSession() {
    if (m_active) {
        const auto answer = QMessageBox::question(m_pMainWindow,
                tr("End Live Session"),
                tr("End the live session \"%1\"?").arg(m_eventName));
        if (answer == QMessageBox::Yes) {
            const QString name = m_eventName;
            endSession();
            showToast(tr("Live session \"%1\" ended").arg(name), kColorInfo);
        }
        return;
    }

    // Event name (only for the log) + style (what goes into the tags later).
    QDialog dialog(m_pMainWindow);
    dialog.setWindowTitle(tr("Start Live DJing"));
    auto* pLayout = new QFormLayout(&dialog);
    auto* pEventEdit = new QLineEdit(m_pConfig->getValueString(kLastEventKey), &dialog);
    pEventEdit->setPlaceholderText(tr("e.g. Fusion Waves"));
    pEventEdit->selectAll();
    auto* pStyleBox = new QComboBox(&dialog);
    pStyleBox->setEditable(true);
    pStyleBox->setInsertPolicy(QComboBox::NoInsert);
    pStyleBox->addItems({QStringLiteral("Fusion"), QStringLiteral("Zouk")});
    pStyleBox->lineEdit()->setPlaceholderText(tr("Fusion, Zouk or type another"));
    const QString lastStyle = m_pConfig->getValueString(kLastStyleKey);
    pStyleBox->setCurrentText(lastStyle.isEmpty() ? QStringLiteral("Fusion") : lastStyle);
    pLayout->addRow(tr("Event / session:"), pEventEdit);
    pLayout->addRow(tr("Style:"), pStyleBox);
    auto* pButtons = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    pButtons->button(QDialogButtonBox::Ok)->setText(tr("Start"));
    pLayout->addRow(pButtons);
    connect(pButtons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(pButtons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    const auto updateOk = [pButtons, pEventEdit, pStyleBox]() {
        pButtons->button(QDialogButtonBox::Ok)
                ->setEnabled(!pEventEdit->text().trimmed().isEmpty() &&
                        !pStyleBox->currentText().trimmed().isEmpty());
    };
    connect(pEventEdit, &QLineEdit::textChanged, &dialog, updateOk);
    connect(pStyleBox, &QComboBox::currentTextChanged, &dialog, updateOk);
    updateOk();
    pEventEdit->setFocus();
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const QString eventName = pEventEdit->text().trimmed();
    const QString style = pStyleBox->currentText().trimmed();
    if (eventName.isEmpty() || style.isEmpty()) {
        return;
    }
    const QString dirPath = QDir(m_pConfig->getSettingsPath())
                                    .filePath(QStringLiteral("live_sessions"));
    QDir().mkpath(dirPath);
    // The date is the day the session started, also for tracks after midnight.
    const QString fileName = QDateTime::currentDateTime().toString(QStringLiteral("yyMMdd")) +
            QChar('_') + fileSlug(eventName) + QStringLiteral(".jsonl");
    startSession(eventName, style, QDir(dirPath).filePath(fileName), false);
}

void CrowdRatingSession::offerResume() {
    if (m_active) {
        return;
    }
    const QString filePath = m_pConfig->getValueString(kLastFileKey);
    const QString eventName = m_pConfig->getValueString(kLastEventKey);
    const QString style = m_pConfig->getValueString(kLastStyleKey);
    // The log's own modification time: mixxx.cfg is only written on a clean
    // exit, so after a crash it would not know about the last entries.
    const QDateTime lastActivity = QFileInfo(filePath).lastModified();
    if (filePath.isEmpty() || eventName.isEmpty() || !lastActivity.isValid() ||
            !QFile::exists(filePath) ||
            lastActivity.secsTo(QDateTime::currentDateTime()) > kResumeWindowSecs) {
        return;
    }
    const auto answer = QMessageBox::question(m_pMainWindow,
            tr("Continue Live Session"),
            tr("Continue the live session \"%1\" (%2)?\n\nLast entry: %3")
                    .arg(eventName,
                            style,
                            QLocale().toString(lastActivity, QLocale::ShortFormat)));
    if (answer == QMessageBox::Yes) {
        startSession(eventName, style, filePath, true);
    }
}

bool CrowdRatingSession::startSession(const QString& eventName,
        const QString& style,
        const QString& filePath,
        bool resumed) {
    if (m_active) {
        endSession();
    }
    m_file.setFileName(filePath);
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        qWarning() << "CrowdRatingSession: cannot open" << filePath << m_file.errorString();
        QMessageBox::warning(m_pMainWindow,
                tr("Start Live DJing"),
                tr("Could not open the session log:\n%1\n\n%2")
                        .arg(filePath, m_file.errorString()));
        return false;
    }
    m_active = true;
    m_eventName = eventName;
    m_style = style;
    m_latestRating.clear();
    m_pConfig->setValue(kLastFileKey, filePath);
    m_pConfig->setValue(kLastEventKey, eventName);
    m_pConfig->setValue(kLastStyleKey, style);
    // Persist now (one small write, only here): after a crash mid-gig the
    // next start must still find this session to offer continuing it.
    m_pConfig->save();

    if (resumed) {
        // Restore the latest rating per track so the deck tints are right again.
        QFile in(filePath);
        if (in.open(QIODevice::ReadOnly | QIODevice::Text)) {
            while (!in.atEnd()) {
                const QJsonObject entry = QJsonDocument::fromJson(in.readLine()).object();
                const QString kind = entry.value(QStringLiteral("kind")).toString();
                if (!entry.contains(QStringLiteral("track_id"))) {
                    continue; // session_start / _resume / _end
                }
                const TrackId id(entry.value(QStringLiteral("track_id")).toVariant());
                if (!id.isValid()) {
                    continue;
                }
                if (kind == kindName(Kind::Plus)) {
                    m_latestRating.insert(id, Kind::Plus);
                } else if (kind == kindName(Kind::Minus)) {
                    m_latestRating.insert(id, Kind::Minus);
                } else if (kind == kindName(Kind::Neutral)) {
                    m_latestRating.insert(id, Kind::Neutral);
                }
            }
        }
    }

    QJsonObject entry;
    entry.insert(QStringLiteral("kind"),
            resumed ? QStringLiteral("session_resume") : QStringLiteral("session_start"));
    writeEntry(entry);

    // A track that is already playing gets its 30 s counted from now.
    for (DeckState& deck : m_decks) {
        const TrackPointer pTrack = PlayerInfo::instance().getTrackInfo(deck.group);
        deck.trackId = pTrack ? pTrack->getId() : TrackId();
        deck.audibleMillis = 0;
        deck.audibleSince = QDateTime();
        deck.playLogged = false;
        deck.pRatingCO->forceSet(pTrack && m_latestRating.contains(deck.trackId)
                        ? tintValue(m_latestRating.value(deck.trackId))
                        : 0);
    }
    m_lastTick = QDateTime::currentDateTime();
    m_tickTimer.start();
    m_pSessionActive->forceSet(1);
    emit activeChanged(true, m_eventName);

    showToast(resumed ? tr("Live session \"%1\" (%2) continued").arg(eventName, style)
                      : tr("Live session \"%1\" (%2) started").arg(eventName, style),
            kColorInfo);
    return true;
}

void CrowdRatingSession::endSession() {
    if (!m_active) {
        return;
    }
    QJsonObject entry;
    entry.insert(QStringLiteral("kind"), QStringLiteral("session_end"));
    writeEntry(entry);
    m_file.close();
    m_tickTimer.stop();
    m_active = false;
    m_latestRating.clear();
    clearDeckTints();
    m_pSessionActive->forceSet(0);
    emit activeChanged(false, QString());
}

void CrowdRatingSession::writeEntry(const QJsonObject& entry) {
    if (!m_file.isOpen()) {
        return;
    }
    const QDateTime now = QDateTime::currentDateTime();
    QJsonObject line = entry;
    line.insert(QStringLiteral("t"), timestamp(now));
    line.insert(QStringLiteral("event"), m_eventName);
    line.insert(QStringLiteral("style"), m_style);
    m_file.write(QJsonDocument(line).toJson(QJsonDocument::Compact));
    m_file.write("\n");
    // Flush every line: a crash mid-gig must not lose the night.
    m_file.flush();
}

QJsonObject CrowdRatingSession::trackEntry(const QString& kind, const TrackPointer& pTrack) const {
    QJsonObject entry;
    entry.insert(QStringLiteral("kind"), kind);
    entry.insert(QStringLiteral("track_id"), QJsonValue::fromVariant(pTrack->getId().toVariant()));
    entry.insert(QStringLiteral("artist"), pTrack->getArtist());
    entry.insert(QStringLiteral("title"), pTrack->getTitle());
    entry.insert(QStringLiteral("path"), pTrack->getLocation());
    return entry;
}

// static
void CrowdRatingSession::ensureProxies(DeckState* pDeck) {
    if (pDeck->pPlay) {
        return;
    }
    const QString& group = pDeck->group;
    pDeck->pPlay = std::make_unique<PollingControlProxy>(group, QStringLiteral("play"));
    pDeck->pVolume = std::make_unique<PollingControlProxy>(group, QStringLiteral("volume"));
    pDeck->pPregain = std::make_unique<PollingControlProxy>(group, QStringLiteral("pregain"));
    pDeck->pOrientation = std::make_unique<PollingControlProxy>(
            group, QStringLiteral("orientation"));
    pDeck->pPlayPosition = std::make_unique<PollingControlProxy>(
            group, QStringLiteral("playposition"));
    pDeck->pDuration = std::make_unique<PollingControlProxy>(group, QStringLiteral("duration"));
}

double CrowdRatingSession::deckGain(DeckState* pDeck) {
    ensureProxies(pDeck);
    const DeckState& deck = *pDeck;
    if (deck.pPlay->get() == 0.0 || deck.pPregain->get() <= kMinPregain) {
        return 0;
    }
    CSAMPLE_GAIN xfl, xfr;
    // Same simplification as PlayerInfo::updateCurrentPlayingDeck().
    EngineXfader::getXfadeGains(m_xfader.get(), 1.0, 0.0, MIXXX_XFADER_ADDITIVE, false, &xfl, &xfr);
    const auto orient = static_cast<int>(deck.pOrientation->get());
    double xfvol = 1.0;
    if (orient == EngineChannel::LEFT) {
        xfvol = xfl;
    } else if (orient == EngineChannel::RIGHT) {
        xfvol = xfr;
    }
    return deck.pVolume->get() * xfvol;
}

int CrowdRatingSession::loudestDeck() {
    const int numDecks = std::min(PlayerInfo::instance().numDecks(),
            static_cast<int>(m_decks.size()));
    int best = -1;
    double bestGain = kAudibleGain;
    for (int i = 0; i < numDecks; ++i) {
        if (!PlayerInfo::instance().getTrackInfo(m_decks[i].group)) {
            continue;
        }
        const double gain = deckGain(&m_decks[i]);
        if (gain >= bestGain) {
            best = i;
            bestGain = gain;
        }
    }
    return best;
}

void CrowdRatingSession::slotTick() {
    const QDateTime now = QDateTime::currentDateTime();
    // Real elapsed time, the timer may fire late under load.
    const qint64 elapsed = std::clamp<qint64>(m_lastTick.msecsTo(now), 0, 5 * kTickMillis);
    m_lastTick = now;
    advance(elapsed);
}

void CrowdRatingSession::advance(qint64 elapsed) {
    if (!m_active) {
        return;
    }
    const QDateTime now = QDateTime::currentDateTime();

    const int numDecks = std::min(PlayerInfo::instance().numDecks(),
            static_cast<int>(m_decks.size()));
    for (int i = 0; i < numDecks; ++i) {
        DeckState& deck = m_decks[i];
        if (deck.playLogged || deckGain(&deck) < kAudibleGain) {
            continue;
        }
        const TrackPointer pTrack = PlayerInfo::instance().getTrackInfo(deck.group);
        if (!pTrack) {
            continue;
        }
        if (pTrack->getId() != deck.trackId) {
            // Missed a trackChanged (should not happen), start over.
            deck.trackId = pTrack->getId();
            deck.audibleMillis = 0;
            deck.audibleSince = QDateTime();
        }
        if (!deck.audibleSince.isValid()) {
            deck.audibleSince = now;
        }
        deck.audibleMillis += elapsed;
        if (deck.audibleMillis >= kPlayedMillis) {
            QJsonObject entry = trackEntry(QStringLiteral("play"), pTrack);
            entry.insert(QStringLiteral("deck"), i + 1);
            entry.insert(QStringLiteral("audible_since"), timestamp(deck.audibleSince));
            writeEntry(entry);
            deck.playLogged = true;
        }
    }
}

void CrowdRatingSession::slotTrackChanged(
        const QString& group, TrackPointer pNewTrack, TrackPointer pOldTrack) {
    Q_UNUSED(pOldTrack);
    int deckNumber = 0;
    if (!PlayerManager::isDeckGroup(group, &deckNumber) || deckNumber < 1 ||
            deckNumber > static_cast<int>(m_decks.size())) {
        return;
    }
    DeckState& deck = m_decks[deckNumber - 1];
    // Every load starts a new "played" count, also a reload of the same track.
    deck.trackId = pNewTrack ? pNewTrack->getId() : TrackId();
    deck.audibleMillis = 0;
    deck.audibleSince = QDateTime();
    deck.playLogged = false;
    double tint = 0;
    if (m_active && pNewTrack && m_latestRating.contains(deck.trackId)) {
        tint = tintValue(m_latestRating.value(deck.trackId));
    }
    deck.pRatingCO->forceSet(tint);
}

void CrowdRatingSession::slotRateAudible(int kind) {
    rateAudible(static_cast<Kind>(kind));
}

void CrowdRatingSession::rateAudible(Kind kind) {
    if (!m_active) {
        showToast(tr("No live session - start one in Options > Start Live DJing"), kColorInfo);
        return;
    }
    const int deckIndex = loudestDeck();
    if (deckIndex < 0) {
        showToast(tr("Nothing audible to rate"), kColorInfo);
        return;
    }
    DeckState& deck = m_decks[deckIndex];
    ensureProxies(&deck);
    const TrackPointer pTrack = PlayerInfo::instance().getTrackInfo(deck.group);
    if (!pTrack) {
        return;
    }
    QJsonObject entry = trackEntry(kindName(kind), pTrack);
    entry.insert(QStringLiteral("source"), QStringLiteral("control"));
    entry.insert(QStringLiteral("deck"), deckIndex + 1);
    const double duration = deck.pDuration->get();
    if (duration > 0) {
        entry.insert(QStringLiteral("pos_s"),
                qRound(deck.pPlayPosition->get() * duration * 10) / 10.0);
    }
    writeEntry(entry);
    m_latestRating.insert(pTrack->getId(), kind);
    updateDeckTints(pTrack->getId());

    QString text;
    switch (kind) {
    case Kind::Plus:
        text = tr("%1 marked as working well");
        break;
    case Kind::Minus:
        text = tr("%1 marked as not working");
        break;
    case Kind::Neutral:
        text = tr("%1 marked as neutral");
        break;
    }
    showToast(QStringLiteral("Deck %1: ").arg(deckIndex + 1) + text.arg(trackLabel(pTrack)),
            kind);
}

void CrowdRatingSession::rateTracks(const TrackPointerList& tracks, Kind kind) {
    if (!m_active || tracks.isEmpty()) {
        return;
    }
    for (const TrackPointer& pTrack : tracks) {
        if (!pTrack) {
            continue;
        }
        QJsonObject entry = trackEntry(kindName(kind), pTrack);
        entry.insert(QStringLiteral("source"), QStringLiteral("menu"));
        writeEntry(entry);
        m_latestRating.insert(pTrack->getId(), kind);
        updateDeckTints(pTrack->getId());
    }

    const QString label = tracks.size() == 1 && tracks.first()
            ? trackLabel(tracks.first())
            : tr("%n tracks", "", static_cast<int>(tracks.size()));
    QString text;
    switch (kind) {
    case Kind::Plus:
        text = tr("%1 marked as working well");
        break;
    case Kind::Minus:
        text = tr("%1 marked as not working");
        break;
    case Kind::Neutral:
        text = tr("%1 marked as neutral");
        break;
    }
    showToast(text.arg(label), kind);
}

void CrowdRatingSession::updateDeckTints(const TrackId& trackId) {
    const double tint = m_latestRating.contains(trackId)
            ? tintValue(m_latestRating.value(trackId))
            : 0;
    for (DeckState& deck : m_decks) {
        const TrackPointer pTrack = PlayerInfo::instance().getTrackInfo(deck.group);
        if (pTrack && pTrack->getId() == trackId) {
            deck.pRatingCO->forceSet(tint);
        }
    }
}

void CrowdRatingSession::clearDeckTints() {
    for (DeckState& deck : m_decks) {
        deck.pRatingCO->forceSet(0);
    }
}

void CrowdRatingSession::showToast(const QString& text, Kind kind) {
    switch (kind) {
    case Kind::Plus:
        showToast(text, kColorPlus);
        return;
    case Kind::Minus:
        showToast(text, kColorMinus);
        return;
    case Kind::Neutral:
        showToast(text, kColorNeutral);
        return;
    }
}

void CrowdRatingSession::showToast(const QString& text, const QColor& accent) {
    if (!m_pMainWindow) {
        return;
    }
    if (!m_pToast) {
        // Owned by the main window; floats over the skin, ignores the mouse.
        m_pToast = new QLabel(m_pMainWindow);
        m_pToast->setObjectName(QStringLiteral("CrowdRatingToast"));
        m_pToast->setAttribute(Qt::WA_TransparentForMouseEvents);
        m_pToast->setFocusPolicy(Qt::NoFocus);
        m_pToast->setAlignment(Qt::AlignCenter);
        m_pToast->setTextFormat(Qt::PlainText);
    }
    m_pToast->setStyleSheet(QStringLiteral(
            "#CrowdRatingToast { background-color: rgba(20, 20, 20, 230);"
            " color: #FFFFFF; font-size: 16px; font-weight: bold;"
            " border: 2px solid %1; border-radius: 6px; padding: 10px 18px; }")
                    .arg(accent.name()));
    m_pToast->setWordWrap(false);
    m_pToast->setMinimumWidth(0);
    m_pToast->setMaximumWidth(QWIDGETSIZE_MAX);
    m_pToast->setText(text);
    m_pToast->adjustSize();
    const QSize size = m_pToast->size();
    const int maxWidth = m_pMainWindow->width() - 40;
    if (size.width() > maxWidth) {
        m_pToast->setWordWrap(true);
        m_pToast->setFixedWidth(maxWidth);
        m_pToast->adjustSize();
    }
    m_pToast->move((m_pMainWindow->width() - m_pToast->width()) / 2,
            m_pMainWindow->height() / 3 - m_pToast->height() / 2);
    m_pToast->raise();
    m_pToast->show();
    m_toastTimer.start();
}
