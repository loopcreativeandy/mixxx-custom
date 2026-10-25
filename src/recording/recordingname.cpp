#include "recording/recordingname.h"

#include <QRegularExpression>

namespace recordingname {

QString camelCaseTitle(const QString& title) {
    static const QRegularExpression kBracketed(QStringLiteral("[\\(\\[\\{][^\\)\\]\\}]*[\\)\\]\\}]"));
    static const QRegularExpression kFeat(
            QStringLiteral("\\s(feat\\.?|ft\\.|featuring)\\s.*$"),
            QRegularExpression::CaseInsensitiveOption);
    // "Song - Radio Edit", "Song - 2011 Remaster", "Song - Kizomba Remix"
    static const QRegularExpression kVersionSuffix(
            QStringLiteral("\\s-\\s[^-]*\\b(mix|edit|remix|version|remaster(ed)?|"
                           "rmx|bootleg|cover)\\b[^-]*$"),
            QRegularExpression::CaseInsensitiveOption);
    QString cleaned = title;
    cleaned.remove(kBracketed);
    cleaned.remove(kFeat);
    cleaned.remove(kVersionSuffix);
    // Strip accents: decompose, then drop the combining marks.
    cleaned = cleaned.normalized(QString::NormalizationForm_KD);

    QString result;
    bool startOfWord = true;
    for (const QChar ch : std::as_const(cleaned)) {
        if (ch.isMark()) {
            continue;
        }
        if (ch == QChar('\'') || ch == QChar(0x2019)) {
            // "Don't" -> "Dont", not "DonT"
            continue;
        }
        if (!ch.isLetterOrNumber()) {
            startOfWord = true;
            continue;
        }
        result.append(startOfWord ? ch.toUpper() : ch);
        startOfWord = false;
    }
    return result;
}

QString buildBaseName(const QString& dateTimeBase,
        bool zoukMode,
        const QStringList& trackTitles,
        int maxLength) {
    QString name = dateTimeBase +
            (zoukMode ? QStringLiteral("_Zouk_X") : QStringLiteral("_Fusion_X"));
    bool first = true;
    for (const QString& title : trackTitles) {
        const QString part = camelCaseTitle(title);
        if (part.isEmpty()) {
            continue;
        }
        const QString sep = first ? QStringLiteral("__") : QStringLiteral("_");
        if (name.size() + sep.size() + part.size() > maxLength) {
            break;
        }
        name += sep + part;
        first = false;
    }
    return name;
}

} // namespace recordingname
