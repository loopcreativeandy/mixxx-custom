#include "util/layoutconfig.h"

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

const Logger kLogger("LayoutConfig");

const QString kConfigFileName = QStringLiteral("andys_layout.ini");

constexpr qint64 kRecheckIntervalMs = 2000;

const QString kMinWidthPrefix = QStringLiteral("min_width.");
const QString kMinHeightPrefix = QStringLiteral("min_height.");

LayoutConfig defaultConfig() {
    LayoutConfig config;
    // CP13: the values Andy settled on after testing the AndyVideo presenter
    // column on his machine. Defaults rather than template comments, so a
    // fresh install already drags the way he wants.
    config.minWidth.insert(QStringLiteral("ControlColumn"), 860);
    config.minWidth.insert(QStringLiteral("PresenterZone"), 0);
    config.rowHeightMinFactor = 1.0;
    config.autoCenter = true;
    return config;
}

void writeTemplateFile(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        kLogger.warning() << "Cannot create" << filePath;
        return;
    }
    const LayoutConfig config = defaultConfig();
    QTextStream out(&file);
    out << "# Andy's Mixxx layout overrides - no recompile needed, but you "
           "must\n"
        << "# RESTART Mixxx (or reload the skin) after editing: these are "
           "applied\n"
        << "# while the skin is built.\n"
        << "#\n"
        << "# min_width.<ObjectName> / min_height.<ObjectName> override the "
           "minimum\n"
        << "# size of any widget group named in the skin. That minimum is "
           "what\n"
        << "# stops a splitter handle from being dragged further - lower it "
           "and\n"
        << "# the handle keeps going (widgets inside may get clipped, that's "
           "the\n"
        << "# trade-off). 0 = as close to no minimum as Qt allows (1 px).\n"
        << "#\n"
        << "# Measured with the MIXXX_DUMP_LAYOUT probe on AndyVideo: nothing "
           "in\n"
        << "# the skin sets a minimum on ControlColumn - its 1134 px floor is "
           "a\n"
        << "# minimumSizeHint inherited from LibraryContainer/LibraryVSplitter. "
           "An\n"
        << "# explicit value here overrides that hint.\n"
        << "#\n"
        << "# The two that matter for the AndyVideo presenter column:\n"
        << "#   ControlColumn = everything left of it (waveforms, decks, "
           "mixer,\n"
        << "#                   library). ITS minimum is what stops the "
           "handle\n"
        << "#                   from moving further left, i.e. what caps how "
           "wide\n"
        << "#                   the playlist column can get. Try 1000, then "
           "800.\n"
        << "#   PresenterZone = the playlist column itself; caps how NARROW "
           "it\n"
        << "#                   can get. Skin default is 200.\n"
        << "#\n"
        << "# The values below are Andy's tested defaults (CP13): they are "
           "what\n"
        << "# the build uses even if you delete these lines.\n"
        << "min_width.ControlColumn="
        << config.minWidth.value(QStringLiteral("ControlColumn"), 860) << "\n"
        << "min_width.PresenterZone="
        << config.minWidth.value(QStringLiteral("PresenterZone"), 0) << "\n"
        << "#\n"
        << "# Track table rows can never be shorter than the library font's "
           "pixel\n"
        << "# height (upstream Mixxx clamps them). This multiplies that "
           "floor:\n"
        << "# 1.0 = upstream, 0.6 = noticeably tighter rows with slight "
           "descender\n"
        << "# clipping. Row height itself stays in Preferences -> Library.\n"
        << "row_height_min_factor=1.0\n"
        << "#\n"
        << "# auto_center=1 keeps the mixer + spectrum column horizontally "
           "centered\n"
        << "# in the window whatever its width is, by moving the presenter "
           "split for\n"
        << "# you on every resize. Set 0 to keep the split exactly where you "
           "dragged\n"
        << "# it. Unlike the rest of this file this one IS read live (~2 s), "
           "no\n"
        << "# restart needed.\n"
        << "auto_center=" << (config.autoCenter ? 1 : 0) << "\n";
}

LayoutConfig parseConfigFile(const QString& filePath) {
    LayoutConfig config = defaultConfig();
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
        if (key.startsWith(kMinWidthPrefix)) {
            const QString objectName = key.mid(kMinWidthPrefix.size());
            if (!objectName.isEmpty()) {
                config.minWidth.insert(objectName,
                        static_cast<int>(qBound(0.0, number, 10000.0)));
            }
        } else if (key.startsWith(kMinHeightPrefix)) {
            const QString objectName = key.mid(kMinHeightPrefix.size());
            if (!objectName.isEmpty()) {
                config.minHeight.insert(objectName,
                        static_cast<int>(qBound(0.0, number, 10000.0)));
            }
        } else if (key == QLatin1String("auto_center")) {
            config.autoCenter = number != 0;
        } else if (key == QLatin1String("row_height_min_factor")) {
            config.rowHeightMinFactor = qBound(0.1, number, 4.0);
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
    LayoutConfig config = defaultConfig();
    bool checkedOnce = false;
    /// Empty until initialize() has run. While empty, current() does no
    /// filesystem access whatsoever.
    QString filePath;
};

Q_GLOBAL_STATIC(Cache, s_cache)

} // anonymous namespace

// static
void LayoutConfig::initialize(const QString& settingsPath) {
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
LayoutConfig LayoutConfig::current() {
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
        kLogger.info() << "Loaded layout overrides from" << pCache->filePath;
    }
    return pCache->config;
}

} // namespace mixxx
