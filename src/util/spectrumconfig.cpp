#include "util/spectrumconfig.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QString>
#include <QTextStream>

#include "util/cmdlineargs.h"
#include "util/logger.h"

namespace mixxx {

namespace {

const Logger kLogger("SpectrumConfig");

const QString kConfigFileName = QStringLiteral("andys_spectrum.ini");

constexpr qint64 kRecheckIntervalMs = 2000;

SpectrumConfig defaultConfig() {
    SpectrumConfig config;
    // CP12: the fall starts at speed (no ease-in) and only accelerates
    // mildly, so it reads as a fast, near-linear drop: a full-scale bar is
    // down in ~0.3 s.
    config.fallInitialSpeed = 2.2;
    config.fallGravity = 6.0;
    config.attack = 1.0;
    // CP13: Andy's tested values.
    config.peakHoldMs = 200;
    config.peakFallSpeed = 1.0;
    // CP13: Andy prefers hard LED steps — the faded top LED read as a
    // rendering glitch ("half colors"), not as smoothing.
    config.smoothTopSegment = false;
    config.frameIntervalMs = 16;
    return config;
}

QString configFilePath() {
    const QString settingsPath = CmdlineArgs::Instance().getSettingsPath();
    if (settingsPath.isEmpty()) {
        return QString();
    }
    return settingsPath + QChar('/') + kConfigFileName;
}

void writeTemplateFile(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        kLogger.warning() << "Cannot create" << filePath;
        return;
    }
    const SpectrumConfig config = defaultConfig();
    QTextStream out(&file);
    out << "# Andy's Mixxx spectrum analyzer - edit freely, no recompile "
           "needed.\n"
        << "# Changes are picked up within ~2 seconds while Mixxx runs.\n"
        << "# Delete a line (or this whole file) to fall back to the built-in "
           "default.\n"
        << "#\n"
        << "# How fast the bars drop. Levels are 0..1 (full meter), so a speed "
           "of\n"
        << "# 2.2 means 'a full bar falls in ~0.45 s' before gravity is added "
           "on top.\n"
        << "# fall_gravity=0 gives a perfectly linear fall; higher values make "
           "the\n"
        << "# drop accelerate (heavier, more 'physical').\n"
        << "fall_initial_speed=" << config.fallInitialSpeed << "\n"
        << "fall_gravity=" << config.fallGravity << "\n"
        << "#\n"
        << "# How fast the bars rise: 1.0 = instantly follow the signal,\n"
        << "# 0.5 = close half the gap per frame (calmer, less jittery).\n"
        << "attack=" << config.attack << "\n"
        << "#\n"
        << "# White peak markers: how long they hang before sliding down (ms)\n"
        << "# and how fast they slide (full meters per second).\n"
        << "peak_hold_ms=" << config.peakHoldMs << "\n"
        << "peak_fall_speed=" << config.peakFallSpeed << "\n"
        << "#\n"
        << "# smooth_top_segment=1 fades the topmost LED by its fractional "
           "level\n"
        << "# so the motion looks continuous instead of stepping LED by LED.\n"
        << "# Default 0: the half-lit cap tends to read as a color glitch.\n"
        << "smooth_top_segment=" << (config.smoothTopSegment ? 1 : 0) << "\n"
        << "#\n"
        << "# Animation frame interval in ms (16 = 60 fps).\n"
        << "frame_interval_ms=" << config.frameIntervalMs << "\n";
}

SpectrumConfig parseConfigFile(const QString& filePath) {
    SpectrumConfig config = defaultConfig();
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        kLogger.warning() << "Cannot read" << filePath << "- using defaults";
        return config;
    }
    QTextStream in(&file);
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith(QChar('#'))) {
            continue;
        }
        const int sep = line.indexOf(QChar('='));
        if (sep < 0) {
            continue;
        }
        const QString key = line.left(sep).trimmed();
        const QString value = line.mid(sep + 1).trimmed();
        bool ok = false;
        const double number = value.toDouble(&ok);
        if (!ok) {
            kLogger.warning() << "Invalid number" << value << "for" << key;
            continue;
        }
        if (key == QLatin1String("fall_initial_speed")) {
            config.fallInitialSpeed = qBound(0.0, number, 100.0);
        } else if (key == QLatin1String("fall_gravity")) {
            config.fallGravity = qBound(0.0, number, 1000.0);
        } else if (key == QLatin1String("attack")) {
            config.attack = qBound(0.01, number, 1.0);
        } else if (key == QLatin1String("peak_hold_ms")) {
            config.peakHoldMs = qBound(0.0, number, 60000.0);
        } else if (key == QLatin1String("peak_fall_speed")) {
            config.peakFallSpeed = qBound(0.0, number, 100.0);
        } else if (key == QLatin1String("smooth_top_segment")) {
            config.smoothTopSegment = number != 0;
        } else if (key == QLatin1String("frame_interval_ms")) {
            config.frameIntervalMs =
                    static_cast<int>(qBound(8.0, number, 200.0));
        } else {
            kLogger.warning() << "Unknown key" << key;
        }
    }
    return config;
}

struct Cache {
    QMutex mutex;
    QElapsedTimer sinceCheck;
    QDateTime lastModified;
    SpectrumConfig config = defaultConfig();
    bool initialized = false;
};

Q_GLOBAL_STATIC(Cache, s_cache)

} // anonymous namespace

// static
SpectrumConfig SpectrumConfig::current() {
    Cache* pCache = s_cache();
    const QMutexLocker locker(&pCache->mutex);
    if (pCache->initialized &&
            pCache->sinceCheck.elapsed() < kRecheckIntervalMs) {
        return pCache->config;
    }
    pCache->initialized = true;
    pCache->sinceCheck.restart();
    const QString filePath = configFilePath();
    if (filePath.isEmpty()) {
        return pCache->config;
    }
    const QFileInfo fileInfo(filePath);
    if (!fileInfo.exists()) {
        writeTemplateFile(filePath);
        pCache->lastModified = QDateTime();
        pCache->config = defaultConfig();
        return pCache->config;
    }
    const QDateTime lastModified = fileInfo.lastModified();
    if (lastModified != pCache->lastModified) {
        pCache->lastModified = lastModified;
        pCache->config = parseConfigFile(filePath);
        kLogger.info() << "Loaded spectrum settings from" << filePath;
    }
    return pCache->config;
}

} // namespace mixxx
