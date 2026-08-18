#include "widget/wkey.h"

#include <cmath>

#include "library/library_prefs.h"
#include "moc_wkey.cpp"
#include "skin/legacy/skincontext.h"
#include "track/keyutils.h"

namespace {

// Camelot wheel number (1-12) for a key; relative major/minor share a number.
int camelotNumber(mixxx::track::io::key::ChromaticKey key) {
    int tonic = KeyUtils::keyToTonic(key);
    if (!KeyUtils::keyIsMajor(key)) {
        // Relative major is three semitones up.
        tonic = (tonic + 3) % 12;
    }
    // Each fifth (+7 semitones) advances the wheel by one; C major = 8B.
    return ((tonic * 7) % 12 + 7) % 12 + 1;
}

int camelotDistance(mixxx::track::io::key::ChromaticKey a,
        mixxx::track::io::key::ChromaticKey b) {
    const int diff = qAbs(camelotNumber(a) - camelotNumber(b));
    return qMin(diff, 12 - diff);
}

constexpr int kMaxCompatibleCamelotDistance = 2;

} // anonymous namespace

WKey::WKey(const QString& group, QWidget* pParent)
        : WLabel(pParent),
          m_group(group),
          m_dOldValue(0),
          m_keyClash(false),
          m_keyNotation(mixxx::library::prefs::kKeyNotationConfigKey, this),
          m_engineKeyDistance(group,
                  "visual_key_distance",
                  this,
                  ControlFlag::AllowMissingOrInvalid),
          m_pitch(group, "pitch", this, ControlFlag::AllowMissingOrInvalid),
          m_zoukMode("[Controls]",
                  "zouk_mode",
                  this,
                  ControlFlag::AllowMissingOrInvalid),
          m_playSelf(group, "play", this, ControlFlag::AllowMissingOrInvalid) {
    setValue(m_dOldValue);
    m_keyNotation.connectValueChanged(this, &WKey::keyNotationChanged);
    m_engineKeyDistance.connectValueChanged(this, &WKey::setCents);
    m_pitch.connectValueChanged(this, &WKey::setCents);

    // Zouk mode key-clash warning: watch every other deck's effective key
    // and play state. Missing decks resolve to invalid keys and are skipped.
    m_zoukMode.connectValueChanged(this, &WKey::updateKeyClash);
    m_playSelf.connectValueChanged(this, &WKey::updateKeyClash);
    for (int i = 1; i <= 4; ++i) {
        const QString otherGroup = QStringLiteral("[Channel%1]").arg(i);
        if (otherGroup == m_group) {
            continue;
        }
        OtherDeck deck;
        deck.group = otherGroup;
        deck.pKey = std::make_unique<ControlProxy>(
                otherGroup, "key", this, ControlFlag::AllowMissingOrInvalid);
        deck.pPlay = std::make_unique<ControlProxy>(
                otherGroup, "play", this, ControlFlag::AllowMissingOrInvalid);
        deck.pKey->connectValueChanged(this, &WKey::updateKeyClash);
        deck.pPlay->connectValueChanged(this, &WKey::updateKeyClash);
        m_otherDecks.push_back(std::move(deck));
    }
}

void WKey::onConnectedControlChanged(double dParameter, double dValue) {
    Q_UNUSED(dParameter);
    // Enums are not currently represented using parameter space so it doesn't
    // make sense to use the parameter here yet.
    setValue(dValue);
}

void WKey::setup(const QDomNode& node, const SkinContext& context) {
    WLabel::setup(node, context);
    m_displayCents = context.selectBool(node, "DisplayCents", false);
    m_displayKey = context.selectBool(node, "DisplayKey", true);
    m_displayOffset = context.selectBool(node, "DisplayOffset", false);
}

void WKey::setValue(double dValue) {
    m_dOldValue = dValue;
    mixxx::track::io::key::ChromaticKey key =
            KeyUtils::keyFromNumericValue(dValue);
    if (key != mixxx::track::io::key::INVALID) {
        // Render this key with the user-provided notation.
        QString keyStr = "";
        if (m_displayKey) {
            keyStr = KeyUtils::keyToString(key);
        }
        if (m_displayCents) {
            double diff_cents = m_engineKeyDistance.get();
            int cents_to_display = static_cast<int>(diff_cents * 100);
            char sign = ' ';
            if (diff_cents < 0) {
                sign = '-';
            } else if (diff_cents > 0) {
                sign = '+';
            }
            keyStr.append(QString(" %1%2c").arg(sign).arg(qAbs(cents_to_display)));
        }
        if (m_displayOffset) {
            // Semitones the displayed key sits above/below the file key.
            // "pitch" is the total offset; visual_key_distance is the
            // sub-semitone remainder, so the difference is a whole number.
            const int offset = static_cast<int>(
                    std::lround(m_pitch.get() - m_engineKeyDistance.get()));
            if (offset != 0) {
                keyStr.append(QString(" %1%2")
                                      .arg(offset > 0 ? QLatin1Char('+')
                                                      : QLatin1Char('-'))
                                      .arg(qAbs(offset)));
            }
        }
        setText(keyStr);
    } else {
        setText("");
    }
    updateKeyClash();
}

void WKey::updateKeyClash() {
    bool clash = false;
    if (m_zoukMode.toBool() && m_playSelf.toBool()) {
        const mixxx::track::io::key::ChromaticKey ownKey =
                KeyUtils::keyFromNumericValue(m_dOldValue);
        if (ownKey != mixxx::track::io::key::INVALID) {
            for (const OtherDeck& deck : m_otherDecks) {
                if (!deck.pPlay->toBool()) {
                    continue;
                }
                const mixxx::track::io::key::ChromaticKey otherKey =
                        KeyUtils::keyFromNumericValue(deck.pKey->get());
                if (otherKey == mixxx::track::io::key::INVALID) {
                    continue;
                }
                if (camelotDistance(ownKey, otherKey) >
                        kMaxCompatibleCamelotDistance) {
                    clash = true;
                    break;
                }
            }
        }
    }
    if (clash != m_keyClash) {
        m_keyClash = clash;
        // Plain red override beats the skin color; cleared when compatible.
        setStyleSheet(clash ? QStringLiteral("color: #E53935;") : QString());
    }
}

void WKey::setCents() {
    setValue(m_dOldValue);
}

void WKey::keyNotationChanged(double dKeyNotationValue) {
    Q_UNUSED(dKeyNotationValue);
    // NOTE: dKeyNotationValue is the index of the key notation type, NOT the
    // key itself, so we intentionally set the old value again to update the UI.
    setValue(m_dOldValue);
}
