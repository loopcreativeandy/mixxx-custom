#pragma once

#include <QString>
#include <QStringList>

/// Andy's set-recording file names (CP124):
///   <date>_<time>_<Zouk|Fusion>_X__<TrackOne>_<TrackTwo>...
/// e.g. 2025-10-24_01h08m47s_Zouk_X__Glue_Deixa_ArabicDream_Breathe.mp3
/// X = not rated yet; he replaces it (S/A/B/C/D) and adds a set title by hand.
namespace recordingname {

/// Short recordings with at most this many tracks get renamed.
constexpr int kMaxTracksForAutoName = 6;

/// "Deixa Eu Falar Pra Você (feat. X) [Remix]" -> "DeixaEuFalarPraVoce".
/// Drops (...) / [...] parts and anything from " feat." / " ft." on,
/// strips accents, keeps letters and digits only, capitalises each word
/// (the rest of a word stays as it was, so "JUMANJI" stays "JUMANJI").
QString camelCaseTitle(const QString& title);

/// Base name without extension. Track titles are camel-cased; empty ones are
/// skipped. The track part is cut at a whole title so the base name stays
/// within maxLength (Windows paths).
QString buildBaseName(const QString& dateTimeBase,
        bool zoukMode,
        const QStringList& trackTitles,
        int maxLength = 180);

} // namespace recordingname
