#include "widget/wsplitter.h"

#include <QEvent>
#include <QLayout>
#include <QList>
#include <QResizeEvent>
#include <QSizePolicy>
#include <QTimer>

#include "moc_wsplitter.cpp"
#include "skin/legacy/skincontext.h"
#include "util/layoutconfig.h"

WSplitter::WSplitter(QWidget* pParent, UserSettingsPointer pConfig)
        : QSplitter(pParent),
          WBaseWidget(this),
          m_pConfig(pConfig),
          m_autoCenterPending(false),
          m_keepTargetsValid(false),
          m_keepCapturePending(false) {
    connect(this, &WSplitter::splitterMoved, this, &WSplitter::slotSplitterMoved);
}

void WSplitter::setup(const QDomNode& node, const SkinContext& context) {
    // Load split sizes
    QString sizesJoined;
    QString msg;
    bool ok = false;

    // Default orientation is horizontal. For vertical splitters, the orientation must be set
    // before calling setSizes() for reloading the saved state to work.
    QString layout;
    if (context.hasNodeSelectString(node, "Orientation", &layout)) {
        if (layout == "vertical") {
            setOrientation(Qt::Vertical);
        } else if (layout == "horizontal") {
            setOrientation(Qt::Horizontal);
        }
    }

    // andy-custom CP70: <FreeResize>true</FreeResize> drops the floor the
    // splitter would otherwise put under every child.
    //
    // QSplitter sizes its children through qSmartMinSize(), and for a child
    // whose size policy has no ShrinkFlag - MinimumExpanding ("me"), which is
    // what nearly every skin group uses - that returns
    // max(sizeHint, minimumSizeHint). So the *preferred* size becomes a hard
    // floor and the handle stops dead long before the pane is small, no matter
    // what the widget itself reports as its minimum. Setting the policy along
    // the splitter's own axis to Ignored makes qSmartMinSize() skip that axis
    // entirely and return 0; the cross axis is left alone, so the child still
    // behaves normally in the other direction. Same trick LateNight's
    // library.xml already uses by hand ("me,i" so the library can shrink to
    // zero height), just applied to every child of one splitter.
    //
    // An explicit <MinimumSize> in the skin still wins (qSmartMinSize applies
    // minimumSize() last), which is deliberate: it keeps the few intentional
    // floors like LibSidebarContainer's 100 px while removing the accidental
    // ones. Below the child's real minimum the content simply clips - Andy
    // asked for exactly that ("that's on me", 2026-08-11).
    bool freeResize = false;
    if (context.hasNodeSelectBool(node, "FreeResize", &freeResize) && freeResize) {
        for (int i = 0; i < count(); ++i) {
            QWidget* pChild = widget(i);
            if (pChild == nullptr) {
                continue;
            }
            QSizePolicy policy = pChild->sizePolicy();
            if (orientation() == Qt::Vertical) {
                policy.setVerticalPolicy(QSizePolicy::Ignored);
            } else {
                policy.setHorizontalPolicy(QSizePolicy::Ignored);
            }
            pChild->setSizePolicy(policy);
            // A child layout left on SetDefaultConstraint can push its own
            // minimum onto the widget, which would outrank the policy above.
            if (pChild->layout() != nullptr) {
                pChild->layout()->setSizeConstraint(QLayout::SetNoConstraint);
            }
        }
    }

    // Try to load last values stored in mixxx.cfg
    QString splitSizesConfigKey;
    if (context.hasNodeSelectString(node, "SplitSizesConfigKey", &splitSizesConfigKey)) {
        m_configKey = ConfigKey::parseCommaSeparated(splitSizesConfigKey);

        if (m_pConfig->exists(m_configKey)) {
            sizesJoined = m_pConfig->getValueString(m_configKey);
            msg = "Reading .cfg file: '" + m_configKey.group + " " +
                    m_configKey.item + " " + sizesJoined +
                    "' does not match the number of children nodes:" +
                    QString::number(count());
            ok = true;
        }
    }
    // Sizes that came out of mixxx.cfg are real pixels written by a previous
    // drag, so they can seed the <KeepSize> targets below; a <SplitSizes>
    // default is usually a ratio ("4,12,5") and must not be taken literally.
    const bool sizesFromConfig = ok;

    // nothing in mixxx.cfg? Load default values
    if (!ok && context.hasNodeSelectString(node, "SplitSizes", &sizesJoined)) {
        msg = "<SplitSizes> for <Splitter> (" + sizesJoined +
                ") does not match the number of children nodes:" +
                QString::number(count());
    }

    // found some value for splitsizes?
    if (!sizesJoined.isEmpty()) {
        const QStringList sizesSplit = sizesJoined.split(",");
        QList<int> sizesList;
        ok = false;
        for (const QString& sizeStr : sizesSplit) {
            sizesList.push_back(sizeStr.toInt(&ok));
            if (!ok) {
                break;
            }
        }
        if (sizesList.length() != count()) {
            SKIN_WARNING(node, context, msg);
            ok = false;
        }
        if (ok) {
            this->setSizes(sizesList);
            if (sizesFromConfig) {
                m_keepTargets = sizesList;
                m_keepTargetsValid = true;
            }
        }
    }

    // Which children can be collapsed?
    QString collapsibleJoined;
    if (context.hasNodeSelectString(node, "Collapsible", &collapsibleJoined)) {
        const QStringList collapsibleSplit = collapsibleJoined.split(",");
        QList<bool> collapsibleList;
        ok = false;
        for (const QString& collapsibleStr : collapsibleSplit) {
            collapsibleList.push_back(collapsibleStr.toInt(&ok)>0);
            if (!ok) {
                break;
            }
        }
        if (collapsibleList.length() != count()) {
            msg = "<Collapsible> for <Splitter> (" + collapsibleJoined +
                    ") does not match the number of children nodes:" +
                    QString::number(count());
            SKIN_WARNING(node, context, msg);
            ok = false;
        }
        if (ok) {
            int i = 0;
            for (bool collapsible : collapsibleList) {
                setCollapsible(i++, collapsible);
            }
        }
    }

    // andy-custom CP74: <KeepSize>1,0</KeepSize> - one flag per child - holds
    // that child's extent when the SPLITTER ITSELF is resized (window resize,
    // or a parent splitter's handle being dragged). Qt hands every child its
    // proportional share of such a change, whatever its size policy is (the
    // policy only decides the floor, verified with a standalone Qt probe), so
    // the library sidebar - and with it the preview deck and the tree - grew
    // and shrank every time the presenter column was dragged. Dragging THIS
    // splitter's own handle is untouched and updates the target.
    QString keepSizeJoined;
    if (context.hasNodeSelectString(node, "KeepSize", &keepSizeJoined) &&
            !keepSizeJoined.isEmpty()) {
        const QStringList keepSplit = keepSizeJoined.split(",");
        QList<bool> keepList;
        ok = false;
        for (const QString& keepStr : keepSplit) {
            keepList.push_back(keepStr.toInt(&ok) > 0);
            if (!ok) {
                break;
            }
        }
        if (ok && keepList.length() != count()) {
            msg = "<KeepSize> for <Splitter> (" + keepSizeJoined +
                    ") does not match the number of children nodes:" +
                    QString::number(count());
            SKIN_WARNING(node, context, msg);
            ok = false;
        }
        if (ok) {
            m_keepSize = keepList;
        }
    }

    // andy-custom CP14: keep a named descendant horizontally centered in the
    // splitter regardless of window/screen width. <SplitSizes> and the saved
    // config key still provide the starting point; this just corrects it once
    // the real geometry is known and again on every resize.
    context.hasNodeSelectString(node, "AutoCenter", &m_autoCenterName);
}

void WSplitter::scheduleAutoCenter() {
    if (m_autoCenterName.isEmpty() || m_autoCenterPending) {
        return;
    }
    // Wait for the layout pass this resize triggered: applyAutoCenter()
    // measures live geometry and would otherwise read stale positions.
    m_autoCenterPending = true;
    QTimer::singleShot(0, this, &WSplitter::applyAutoCenter);
}

void WSplitter::applyAutoCenter() {
    m_autoCenterPending = false;
    if (orientation() != Qt::Horizontal || count() != 2) {
        return;
    }
    if (!mixxx::LayoutConfig::current().autoCenter) {
        return;
    }
    QWidget* pTarget = findChild<QWidget*>(m_autoCenterName);
    QWidget* pFirst = widget(0);
    if (!pTarget || !pFirst || !pTarget->isVisible() ||
            !pFirst->isAncestorOf(pTarget)) {
        return;
    }
    const int total = width();
    if (total <= 0 || pTarget->width() <= 0) {
        return;
    }

    // The target sits at a fixed distance from the RIGHT edge of the first
    // pane (it is the last child of a horizontal row, and its width is its
    // natural one - SizePolicy max). So its center only depends on how wide
    // that pane is, and the wanted width follows in one step instead of
    // iterating: rightGap and the target width are invariant under the move.
    const QPoint targetTopRight = pTarget->mapTo(this, QPoint(pTarget->width(), 0));
    const int firstPaneRight = pFirst->mapTo(this, QPoint(pFirst->width(), 0)).x();
    const int rightGap = firstPaneRight - targetTopRight.x();
    const int wantedFirst = total / 2 + rightGap + pTarget->width() / 2;

    QList<int> newSizes = sizes();
    if (newSizes.size() != 2 || newSizes.at(0) == wantedFirst) {
        return;
    }
    newSizes[0] = wantedFirst;
    newSizes[1] = total - handleWidth() - wantedFirst;
    if (newSizes.at(1) < 0) {
        return;
    }
    // QSplitter clamps this against both panes' minimums, so an impossible
    // request simply lands as close as the layout allows.
    setSizes(newSizes);
}

void WSplitter::resizeEvent(QResizeEvent* pEvent) {
    QSplitter::resizeEvent(pEvent);
    if (m_keepSize.isEmpty()) {
        return;
    }
    if (!m_keepTargetsValid) {
        // No saved sizes to start from: adopt whatever the first layout ends
        // up with. Done from the event loop (like the auto-center pass) so the
        // intermediate geometries a window goes through while it is being
        // built do not get frozen in.
        if (!m_keepCapturePending) {
            m_keepCapturePending = true;
            QTimer::singleShot(0, this, &WSplitter::captureKeepSize);
        }
        return;
    }
    applyKeepSize();
}

void WSplitter::captureKeepSize() {
    m_keepCapturePending = false;
    if (m_keepTargetsValid || m_keepSize.isEmpty()) {
        return;
    }
    m_keepTargets = sizes();
    m_keepTargetsValid = true;
}

void WSplitter::applyKeepSize() {
    if (m_keepSize.length() != count() || m_keepTargets.length() != count()) {
        return;
    }
    const QList<int> current = sizes();
    int total = 0;
    for (const int paneSize : current) {
        total += paneSize;
    }
    int keepTotal = 0;
    int flexCurrent = 0;
    for (int i = 0; i < current.length(); ++i) {
        // A hidden child has no size of its own to hold on to.
        const QWidget* pChild = widget(i);
        if (m_keepSize.at(i) && pChild != nullptr && !pChild->isHidden()) {
            keepTotal += m_keepTargets.at(i);
        } else {
            flexCurrent += current.at(i);
        }
    }
    const int flexTotal = total - keepTotal;
    if (flexTotal <= 0 || flexCurrent <= 0) {
        // Not enough room left for the other panes (or they are all
        // collapsed): let Qt's proportional result stand rather than push
        // them to zero.
        return;
    }
    QList<int> wanted = current;
    int handedOut = 0;
    int lastFlex = -1;
    for (int i = 0; i < wanted.length(); ++i) {
        const QWidget* pChild = widget(i);
        if (m_keepSize.at(i) && pChild != nullptr && !pChild->isHidden()) {
            wanted[i] = m_keepTargets.at(i);
        } else {
            wanted[i] = flexTotal * current.at(i) / flexCurrent;
            handedOut += wanted.at(i);
            lastFlex = i;
        }
    }
    if (lastFlex >= 0) {
        // Rounding leftovers go to the last flexible pane so the panes still
        // add up to the full width.
        wanted[lastFlex] += flexTotal - handedOut;
    }
    if (wanted == current) {
        return;
    }
    setSizes(wanted);
}

void WSplitter::slotSplitterMoved() {
    if (!m_keepSize.isEmpty()) {
        // The user just said how wide they want the panes: that is the new
        // size to hold.
        m_keepTargets = sizes();
        m_keepTargetsValid = true;
    }
    if (!m_configKey.group.isEmpty() && !m_configKey.item.isEmpty()) {
        QStringList sizeStrList;
        const auto sizesIntList = sizes();
        for (const int& sizeInt : sizesIntList) {
            sizeStrList.push_back(QString::number(sizeInt));
        }
        QString sizesStr = sizeStrList.join(",");
        m_pConfig->set(m_configKey, ConfigValue(sizesStr));
    }
}

bool WSplitter::event(QEvent* pEvent) {
    if (pEvent->type() == QEvent::ToolTip) {
        updateTooltip();
    } else if (pEvent->type() == QEvent::Resize ||
            pEvent->type() == QEvent::Show) {
        scheduleAutoCenter();
    }
    return QSplitter::event(pEvent);
}
