#include "util/layoutconfig.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QTextStream>

#include "util/cmdlineargs.h"
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
    config.rowHeightMinFactor = 1.0;
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
        << "# trade-off). 0 = no minimum at all.\n"
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
        << "#min_width.ControlColumn=1000\n"
        << "#min_width.PresenterZone=200\n"
        << "#\n"
        << "# Track table rows can never be shorter than the library font's "
           "pixel\n"
        << "# height (upstream Mixxx clamps them). This multiplies that "
           "floor:\n"
        << "# 1.0 = upstream, 0.6 = noticeably tighter rows with slight "
           "descender\n"
        << "# clipping. Row height itself stays in Preferences -> Library.\n"
        << "row_height_min_factor=1.0\n";
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
    bool initialized = false;
};

Q_GLOBAL_STATIC(Cache, s_cache)

} // anonymous namespace

// static
LayoutConfig LayoutConfig::current() {
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
        kLogger.info() << "Loaded layout overrides from" << filePath;
    }
    return pCache->config;
}

} // namespace mixxx
