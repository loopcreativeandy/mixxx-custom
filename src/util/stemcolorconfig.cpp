#include "util/stemcolorconfig.h"

#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QTextStream>

#include "util/logger.h"

namespace mixxx {

namespace {

const Logger kLogger("StemColorConfig");

const QString kConfigFileName = QStringLiteral("andys_stem_colors.ini");

constexpr qint64 kRecheckIntervalMs = 2000;

StemColorConfig defaultConfig() {
    StemColorConfig config;
    // Fixed stem palette v2: hues follow the RGB-waveform convention
    // (low band = red): bass red, drums amber, vocals pink, other cyan.
    config.drums = QColor(0xFF, 0xA6, 0x30);
    config.bass = QColor(0xFF, 0x45, 0x45);
    config.other = QColor(0x45, 0xC8, 0xE8);
    config.vocals = QColor(0xFF, 0x5C, 0xA8);
    // Subtle violet library-row tint, ~13% opacity.
    config.rowTint = QColor(0x8A, 0x5C, 0xFF, 0x21);
    return config;
}

void writeTemplateFile(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        kLogger.warning() << "Cannot create" << filePath;
        return;
    }
    const StemColorConfig config = defaultConfig();
    QTextStream out(&file);
    out << "# Andy's Mixxx stem colors - edit freely, no recompile needed.\n"
        << "# Stem waveform/mixer colors apply when the next track is loaded;\n"
        << "# the library row tint refreshes within ~2 seconds.\n"
        << "# Colors are #RRGGBB hex; stem_row_tint_alpha is opacity 0.0-1.0.\n"
        << "# Delete a line (or this whole file) to fall back to the built-in default.\n"
        << "stem_drums=" << config.drums.name() << "\n"
        << "stem_bass=" << config.bass.name() << "\n"
        << "stem_other=" << config.other.name() << "\n"
        << "stem_vocals=" << config.vocals.name() << "\n"
        << "stem_row_tint=" << config.rowTint.name() << "\n"
        << "stem_row_tint_alpha="
        << QString::number(config.rowTint.alphaF(), 'f', 2) << "\n";
}

StemColorConfig parseConfigFile(const QString& filePath) {
    StemColorConfig config = defaultConfig();
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        kLogger.warning() << "Cannot read" << filePath << "- using defaults";
        return config;
    }
    double rowTintAlpha = config.rowTint.alphaF();
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
        if (key == QLatin1String("stem_row_tint_alpha")) {
            bool ok = false;
            const double alpha = value.toDouble(&ok);
            if (ok) {
                rowTintAlpha = qBound(0.0, alpha, 1.0);
            } else {
                kLogger.warning() << "Invalid alpha" << value << "for" << key;
            }
            continue;
        }
        const QColor color(value);
        if (!color.isValid()) {
            kLogger.warning() << "Invalid color" << value << "for" << key;
            continue;
        }
        if (key == QLatin1String("stem_drums")) {
            config.drums = color;
        } else if (key == QLatin1String("stem_bass")) {
            config.bass = color;
        } else if (key == QLatin1String("stem_other")) {
            config.other = color;
        } else if (key == QLatin1String("stem_vocals")) {
            config.vocals = color;
        } else if (key == QLatin1String("stem_row_tint")) {
            config.rowTint = color;
        } else {
            kLogger.warning() << "Unknown key" << key;
        }
    }
    config.rowTint.setAlphaF(static_cast<float>(rowTintAlpha));
    return config;
}

struct Cache {
    QMutex mutex;
    QElapsedTimer sinceCheck;
    QDateTime lastModified;
    StemColorConfig config = defaultConfig();
    bool checkedOnce = false;
    /// Empty until initialize() has run. While empty, current() does no
    /// filesystem access whatsoever.
    QString filePath;
};

Q_GLOBAL_STATIC(Cache, s_cache)

} // anonymous namespace

// static
void StemColorConfig::initialize(const QString& settingsPath) {
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
StemColorConfig StemColorConfig::current() {
    Cache* pCache = s_cache();
    const QMutexLocker locker(&pCache->mutex);
    if (pCache->filePath.isEmpty()) {
        // initialize() has not run: unit tests, or startup before the settings
        // path is final. Built-in defaults, no file I/O.
        return pCache->config;
    }
    if (pCache->checkedOnce &&
            pCache->sinceCheck.elapsed() < kRecheckIntervalMs) {
        return pCache->config;
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
        kLogger.info() << "Loaded stem colors from" << pCache->filePath;
    }
    return pCache->config;
}

} // namespace mixxx
