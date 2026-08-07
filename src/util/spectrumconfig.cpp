#include "util/spectrumconfig.h"

#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QString>
#include <QTextStream>

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
    config.peakFallGravity = 6.0;
    // CP13: Andy prefers hard LED steps — the faded top LED read as a
    // rendering glitch ("half colors"), not as smoothing.
    config.smoothTopSegment = false;
    config.frameIntervalMs = 16;
    // CP18: 32 bars ~= 1/3.5 octave over 40 Hz-16 kHz, i.e. the classic
    // 1/3-octave RTA layout every hardware analyzer uses. 16 was only ever a
    // bar-width compromise for the narrow presenter column.
    config.bands = 32;
    // Keep the live-editing loop on by default: that is what the file is for.
    config.hotReload = true;
    return config;
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
        << "# Changes are picked up within ~2 seconds while Mixxx runs, as "
           "long as\n"
        << "# hot_reload is 1 (see below).\n"
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
        << "# White peak markers: how long they hang before sliding down (ms),\n"
        << "# how fast they start sliding (full meters per second) and how "
           "hard\n"
        << "# they accelerate on the way down. Same ballistics as the bars:\n"
        << "# peak_fall_gravity=0 is a linear slide, higher values drop like "
           "a\n"
        << "# stone the longer they fall.\n"
        << "peak_hold_ms=" << config.peakHoldMs << "\n"
        << "peak_fall_speed=" << config.peakFallSpeed << "\n"
        << "peak_fall_gravity=" << config.peakFallGravity << "\n"
        << "#\n"
        << "# smooth_top_segment=1 fades the topmost LED by its fractional "
           "level\n"
        << "# so the motion looks continuous instead of stepping LED by LED.\n"
        << "# Default 0: the half-lit cap tends to read as a color glitch.\n"
        << "smooth_top_segment=" << (config.smoothTopSegment ? 1 : 0) << "\n"
        << "#\n"
        << "# Animation frame interval in ms (16 = 60 fps).\n"
        << "frame_interval_ms=" << config.frameIntervalMs << "\n"
        << "#\n"
        << "# hot_reload=1 re-reads this file every ~2 s while Mixxx runs, so "
           "you can\n"
        << "# tune the values above with Mixxx open. Set it to 0 once you are "
           "happy:\n"
        << "# the file is then read once at startup and never checked again.\n"
        << "# NOTE: the 'Spectrum Live Reload' tick box in the skin settings "
           "(wrench\n"
        << "# panel, right under 'Spectrum Analyzer') overrides this line "
           "whenever a\n"
        << "# skin that has it is loaded - which is the easier place to flip "
           "it. This\n"
        << "# key is the fallback for skins without the tick box.\n"
        << "hot_reload=" << (config.hotReload ? 1 : 0) << "\n"
        << "#\n"
        << "# Number of bars, spread logarithmically over 40 Hz - 16 kHz "
           "(range "
        << SpectrumConfig::kMinBands << ".." << SpectrumConfig::kMaxBands
        << ").\n"
        << "# The filter width follows automatically, so the bands always "
           "cover the\n"
        << "# spectrum without gaps or overlap. 32 = the classic 1/3-octave "
           "RTA look,\n"
        << "# 16 = wider bars for a narrow column, 10 = one bar per octave.\n"
        << "# NOTE: unlike the settings above this one only applies after a "
           "RESTART.\n"
        << "bands=" << config.bands << "\n";
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
        } else if (key == QLatin1String("peak_fall_gravity")) {
            config.peakFallGravity = qBound(0.0, number, 1000.0);
        } else if (key == QLatin1String("smooth_top_segment")) {
            config.smoothTopSegment = number != 0;
        } else if (key == QLatin1String("frame_interval_ms")) {
            config.frameIntervalMs =
                    static_cast<int>(qBound(8.0, number, 200.0));
        } else if (key == QLatin1String("hot_reload")) {
            config.hotReload = number != 0;
        } else if (key == QLatin1String("bands")) {
            config.bands = static_cast<int>(
                    qBound(static_cast<double>(SpectrumConfig::kMinBands),
                            number,
                            static_cast<double>(SpectrumConfig::kMaxBands)));
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
    bool checkedOnce = false;
    /// Empty until initialize() has run. While empty, current() does no
    /// filesystem access whatsoever.
    QString filePath;
    /// Set once a skin with the "Spectrum Live Reload" tick box is loaded;
    /// overrides the file's hot_reload key from then on.
    bool hotReloadOverride = true;
    bool hasHotReloadOverride = false;
};

Q_GLOBAL_STATIC(Cache, s_cache)

} // anonymous namespace

// static
void SpectrumConfig::initialize(const QString& settingsPath) {
    if (settingsPath.isEmpty()) {
        return;
    }
    Cache* pCache = s_cache();
    const QMutexLocker locker(&pCache->mutex);
    pCache->filePath = QDir(settingsPath).filePath(kConfigFileName);
    if (!QFileInfo::exists(pCache->filePath)) {
        writeTemplateFile(pCache->filePath);
    }
    // Make the next current() actually read the file instead of returning the
    // defaults it may already have handed out before this point.
    pCache->checkedOnce = false;
}

// static
void SpectrumConfig::setHotReloadOverride(bool enabled) {
    Cache* pCache = s_cache();
    const QMutexLocker locker(&pCache->mutex);
    pCache->hotReloadOverride = enabled;
    pCache->hasHotReloadOverride = true;
    if (enabled) {
        // Re-enabled from the tick box: make the very next current() look at
        // the file again instead of waiting out a stale throttle window.
        pCache->checkedOnce = false;
    }
}

// static
SpectrumConfig SpectrumConfig::current() {
    Cache* pCache = s_cache();
    const QMutexLocker locker(&pCache->mutex);
    if (pCache->filePath.isEmpty()) {
        // initialize() has not run: unit tests, or startup before the settings
        // path is final. Built-in defaults, no file I/O.
        return pCache->config;
    }
    if (pCache->checkedOnce) {
        // Hot reload off: read once at startup, then never look at the file
        // again — no stat() from the paint path at all. The skin's tick box
        // wins over the file's hot_reload key once a skin has set it.
        const bool hotReload = pCache->hasHotReloadOverride
                ? pCache->hotReloadOverride
                : pCache->config.hotReload;
        if (!hotReload) {
            return pCache->config;
        }
        if (pCache->sinceCheck.elapsed() < kRecheckIntervalMs) {
            return pCache->config;
        }
    }
    pCache->checkedOnce = true;
    pCache->sinceCheck.restart();
    const QFileInfo fileInfo(pCache->filePath);
    if (!fileInfo.exists()) {
        // Deleted while Mixxx runs: fall back to the built-in defaults. The
        // file is deliberately not recreated here — initialize() owns that.
        pCache->lastModified = QDateTime();
        pCache->config = defaultConfig();
        return pCache->config;
    }
    const QDateTime lastModified = fileInfo.lastModified();
    if (lastModified != pCache->lastModified) {
        pCache->lastModified = lastModified;
        pCache->config = parseConfigFile(pCache->filePath);
        kLogger.info() << "Loaded spectrum settings from" << pCache->filePath;
    }
    return pCache->config;
}

} // namespace mixxx
