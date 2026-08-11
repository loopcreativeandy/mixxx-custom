#include "widget/wsplitter.h"

#include <QEvent>
#include <QLayout>
#include <QList>
#include <QSizePolicy>
#include <QTimer>

#include "moc_wsplitter.cpp"
#include "skin/legacy/skincontext.h"
#include "util/layoutconfig.h"

WSplitter::WSplitter(QWidget* pParent, UserSettingsPointer pConfig)
        : QSplitter(pParent),
          WBaseWidget(this),
          m_pConfig(pConfig),
          m_autoCenterPending(false) {
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

void WSplitter::slotSplitterMoved() {
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
