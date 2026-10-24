#include "library/trackedits/trackeditlog.h"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>

namespace mixxx::trackedits {

namespace {

// the labels of his hand energy codes, as in phonedj/apply_edits.py
const char* const kEnergyLabels[] = {"ambient",
        "noBeat",
        "withPulse",
        "low energy",
        "low-mid energy",
        "midlevel energy",
        "raised energy",
        "high energy",
        "peak",
        "party"};

} // namespace

QStringList splitTags(const QString& text) {
    QStringList tags;
    const QStringList parts = text.split(QChar(','));
    for (const QString& part : parts) {
        const QString tag = part.simplified();
        if (!tag.isEmpty() && !tags.contains(tag)) {
            tags.append(tag);
        }
    }
    return tags;
}

QJsonObject toJson(const TrackEdit& edit,
        const QString& location,
        const QString& artist,
        const QString& title,
        const QDateTime& time) {
    QJsonObject obj;
    obj[QStringLiteral("time")] = time.toString(Qt::ISODate);
    obj[QStringLiteral("date")] = time.toString(QStringLiteral("yyMMdd"));
    obj[QStringLiteral("location")] = location;
    obj[QStringLiteral("artist")] = artist;
    obj[QStringLiteral("title")] = title;
    if (!edit.rate.isEmpty()) {
        obj[QStringLiteral("rate")] = edit.rate;
    }
    if (!edit.energy.isEmpty()) {
        obj[QStringLiteral("energy")] = edit.energy;
    }
    if (!edit.add.isEmpty()) {
        obj[QStringLiteral("add")] = QJsonArray::fromStringList(edit.add);
    }
    if (!edit.remove.isEmpty()) {
        obj[QStringLiteral("remove")] = QJsonArray::fromStringList(edit.remove);
    }
    return obj;
}

QString logFilePath(const UserSettingsPointer& pConfig) {
    return QDir(pConfig->getSettingsPath())
            .filePath(QStringLiteral("track_edits/track_edits.jsonl"));
}

bool appendLines(const QString& filePath, const QList<QJsonObject>& lines) {
    QDir().mkpath(QFileInfo(filePath).absolutePath());
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        return false;
    }
    for (const QJsonObject& line : lines) {
        file.write(QJsonDocument(line).toJson(QJsonDocument::Compact));
        file.write("\n");
    }
    return file.flush();
}

std::optional<TrackEdit> askForEdit(QWidget* pParent,
        const QString& subject,
        const QString& currentComment) {
    QDialog dialog(pParent);
    dialog.setWindowTitle(QObject::tr("Rate / Tag for the Files"));
    auto* pLayout = new QFormLayout(&dialog);

    auto* pSubject = new QLabel(subject, &dialog);
    pSubject->setWordWrap(true);
    pLayout->addRow(pSubject);
    if (!currentComment.isEmpty()) {
        auto* pComment = new QLabel(currentComment, &dialog);
        pComment->setWordWrap(true);
        pComment->setTextInteractionFlags(Qt::TextSelectableByMouse);
        pLayout->addRow(QObject::tr("Tags now:"), pComment);
    }

    auto* pRateBox = new QComboBox(&dialog);
    pRateBox->addItem(QObject::tr("-"), QString());
    for (const char* tier : {"S", "A", "B", "C", "D"}) {
        pRateBox->addItem(QString::fromLatin1(tier), QString::fromLatin1(tier));
    }
    pLayout->addRow(QObject::tr("Rating (today):"), pRateBox);

    auto* pEnergyBox = new QComboBox(&dialog);
    pEnergyBox->addItem(QObject::tr("-"), QString());
    for (int i = 0; i < 10; ++i) {
        pEnergyBox->addItem(QString::number(i).repeated(5) + QChar(' ') +
                        QString::fromLatin1(kEnergyLabels[i]),
                QString::number(i));
    }
    pLayout->addRow(QObject::tr("Energy:"), pEnergyBox);

    auto* pAddEdit = new QLineEdit(&dialog);
    pAddEdit->setPlaceholderText(QObject::tr("comma-separated, e.g. Sensual, Pop"));
    pLayout->addRow(QObject::tr("Add tags:"), pAddEdit);
    auto* pRemoveEdit = new QLineEdit(&dialog);
    pRemoveEdit->setPlaceholderText(QObject::tr("comma-separated"));
    pLayout->addRow(QObject::tr("Remove tags:"), pRemoveEdit);

    auto* pButtons = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    pButtons->button(QDialogButtonBox::Ok)->setText(QObject::tr("Save"));
    pLayout->addRow(pButtons);
    QObject::connect(pButtons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(pButtons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    pAddEdit->setFocus();

    if (dialog.exec() != QDialog::Accepted) {
        return std::nullopt;
    }
    TrackEdit edit;
    edit.rate = pRateBox->currentData().toString();
    edit.energy = pEnergyBox->currentData().toString();
    edit.add = splitTags(pAddEdit->text());
    edit.remove = splitTags(pRemoveEdit->text());
    if (edit.isEmpty()) {
        return std::nullopt;
    }
    return edit;
}

} // namespace mixxx::trackedits
