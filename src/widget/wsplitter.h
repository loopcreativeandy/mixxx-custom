#pragma once

#include <QSplitter>

#include "preferences/usersettings.h"
#include "widget/wbasewidget.h"

class QDomNode;
class SkinContext;

class WSplitter : public QSplitter, public WBaseWidget {
    Q_OBJECT
  public:
    WSplitter(QWidget* pParent, UserSettingsPointer pConfig);

    void setup(const QDomNode& node, const SkinContext& context);

  protected:
    bool event(QEvent* pEvent) override;
    void resizeEvent(QResizeEvent* pEvent) override;

  private slots:
    void slotSplitterMoved();
    /// Re-runs the auto-centering after the pending layout pass, so the
    /// measurement below sees final geometry.
    void applyAutoCenter();

  private:
    /// Moves the split so that the <AutoCenter> descendant ends up centered
    /// in the splitter, whatever the window width is (andy-custom, CP14).
    /// No-op unless <AutoCenter> named a visible widget.
    void scheduleAutoCenter();

    /// Re-applies the <KeepSize> targets after the splitter itself was
    /// resized, so those children do not take their proportional share of the
    /// change (andy-custom, CP74). No-op unless <KeepSize> marked a child.
    void applyKeepSize();
    /// Adopts the current split as the <KeepSize> target. Used once the very
    /// first layout has settled, when no saved sizes existed to seed it.
    void captureKeepSize();

    UserSettingsPointer m_pConfig;
    ConfigKey m_configKey;
    QString m_autoCenterName;
    bool m_autoCenterPending;
    /// One flag per child: true = hold this child's extent across resizes.
    QList<bool> m_keepSize;
    /// The extent each child should keep; only entries flagged above are used.
    QList<int> m_keepTargets;
    bool m_keepTargetsValid;
    bool m_keepCapturePending;
};
