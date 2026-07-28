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

    UserSettingsPointer m_pConfig;
    ConfigKey m_configKey;
    QString m_autoCenterName;
    bool m_autoCenterPending;
};
