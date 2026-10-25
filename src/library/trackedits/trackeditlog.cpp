#include "library/trackedits/trackeditlog.h"

#include <QAbstractItemView>
#include <algorithm>
#include <QComboBox>
#include <QCompleter>
#include <QCoreApplication>
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
#include <QStringListModel>

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

const QString kTagListFileName = QStringLiteral("search_tags.txt");
constexpr int kMinTagCompletionChars = 2;

} // namespace

QStringList loadTagList(const QString& settingsPath, QString* pLoadedFrom) {
    // Settings dir first: it survives reinstalls. The install dir is a fallback.
    QStringList candidates;
    if (!settingsPath.isEmpty()) {
        candidates << QDir(settingsPath).filePath(kTagListFileName);
    }
    candidates << QDir(QCoreApplication::applicationDirPath()).filePath(kTagListFileName);
    for (const QString& path : std::as_const(candidates)) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            continue;
        }
        QStringList tags;
        while (!file.atEnd()) {
            const QString tag = QString::fromUtf8(file.readLine()).trimmed();
            if (!tag.isEmpty() && !tag.startsWith('#') && !tags.contains(tag)) {
                tags.append(tag);
            }
        }
        if (pLoadedFrom) {
            *pLoadedFrom = path;
        }
        return tags;
    }
    return {};
}

TagSegment tagSegmentAt(const QString& text, int cursor) {
    cursor = std::clamp(cursor, 0, static_cast<int>(text.size()));
    int start = static_cast<int>(text.lastIndexOf(QChar(','), cursor - 1)) + 1;
    while (start < cursor && text.at(start).isSpace()) {
        start++;
    }
    int end = static_cast<int>(text.indexOf(QChar(','), cursor));
    if (end < 0) {
        end = static_cast<int>(text.size());
    }
    return TagSegment{start, end, text.mid(start, cursor - start)};
}

QString replaceTagSegment(const QString& text, int cursor, const QString& tag, int* pNewCursor) {
    const TagSegment segment = tagSegmentAt(text, cursor);
    const QString rest = text.mid(segment.end);
    const QString before = text.left(segment.start) + tag;
    const QString result = before + (rest.isEmpty() ? QStringLiteral(", ") : rest);
    if (pNewCursor) {
        *pNewCursor = rest.isEmpty() ? static_cast<int>(result.size())
                                     : static_cast<int>(before.size());
    }
    return result;
}

// Popup completion for a comma-separated tag field, like the tag completion
// in the library search box: type 2+ letters of a tag, pick it with
// Up/Down + Enter (or a click), the entry is replaced and ", " appended.
void attachTagCompleter(QLineEdit* pEdit, const QStringList& tags) {
    if (tags.isEmpty()) {
        return;
    }
    auto* pCompleter = new QCompleter(new QStringListModel(tags, pEdit), pEdit);
    pCompleter->setCaseSensitivity(Qt::CaseInsensitive);
    pCompleter->setFilterMode(Qt::MatchStartsWith);
    pCompleter->setModelSorting(QCompleter::UnsortedModel); // file order = priority
    pCompleter->setCompletionMode(QCompleter::PopupCompletion);
    pCompleter->setMaxVisibleItems(12);
    // setWidget, not QLineEdit::setCompleter: that would complete the whole
    // text instead of the entry under the cursor.
    pCompleter->setWidget(pEdit);
    QObject::connect(pEdit, &QLineEdit::textEdited, pCompleter, [pEdit, pCompleter] {
        const TagSegment segment = tagSegmentAt(pEdit->text(), pEdit->cursorPosition());
        if (segment.word.size() < kMinTagCompletionChars) {
            pCompleter->popup()->hide();
            return;
        }
        pCompleter->setCompletionPrefix(segment.word);
        if (pCompleter->completionCount() == 0) {
            pCompleter->popup()->hide();
            return;
        }
        pCompleter->complete();
        pCompleter->popup()->setCurrentIndex(pCompleter->completionModel()->index(0, 0));
    });
    QObject::connect(pCompleter,
            qOverload<const QString&>(&QCompleter::activated),
            pEdit,
            [pEdit](const QString& tag) {
                int cursor = 0;
                pEdit->setText(replaceTagSegment(
                        pEdit->text(), pEdit->cursorPosition(), tag, &cursor));
                pEdit->setCursorPosition(cursor);
            });
}

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
        const QString& currentComment,
        const QStringList& tagSuggestions) {
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
    attachTagCompleter(pAddEdit, tagSuggestions);
    attachTagCompleter(pRemoveEdit, tagSuggestions);

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
