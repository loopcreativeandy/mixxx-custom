#pragma once

#include <QWidget>

#include "preferences/usersettings.h"
#include "widget/wbasewidget.h"

class Library;
class KeyboardEventFilter;
class DlgAutoDJ;

/// Andy's Auto DJ side pane: a full DlgAutoDJ (queue table + all transport
/// buttons) embedded next to the library, so Auto DJ and a playlist can be
/// open at the same time. Shares the AutoDJProcessor and queue model with
/// the regular Auto DJ view, so both stay in sync.
class WAndysAutoDJPane : public QWidget, public WBaseWidget {
    Q_OBJECT
  public:
    WAndysAutoDJPane(QWidget* pParent,
            UserSettingsPointer pConfig,
            Library* pLibrary,
            KeyboardEventFilter* pKeyboard,
            double backgroundColorOpacity,
            bool showButtonText);

    /// The embedded DlgAutoDJ's button row is wide, and a QSplitter refuses to
    /// shrink a child below its minimumSizeHint(). Report 0 so the pane can be
    /// resized freely; the transport buttons simply get clipped (Andy's call).
    QSize minimumSizeHint() const override;

  private:
    DlgAutoDJ* m_pAutoDJView;
};
