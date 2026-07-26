#include "widget/wandysautodjpane.h"

#include <QVBoxLayout>

#include "library/autodj/autodjfeature.h"
#include "library/autodj/dlgautodj.h"
#include "library/library.h"
#include "moc_wandysautodjpane.cpp"
#include "util/assert.h"

WAndysAutoDJPane::WAndysAutoDJPane(QWidget* pParent,
        UserSettingsPointer pConfig,
        Library* pLibrary,
        KeyboardEventFilter* pKeyboard,
        double backgroundColorOpacity,
        bool showButtonText)
        : QWidget(pParent),
          WBaseWidget(this),
          m_pAutoDJView(nullptr) {
    setObjectName(QStringLiteral("AndysAutoDJPane"));

    AutoDJFeature* pAutoDJFeature = pLibrary->autoDJFeature();
    VERIFY_OR_DEBUG_ASSERT(pAutoDJFeature) {
        return;
    }

    m_pAutoDJView = new DlgAutoDJ(this,
            backgroundColorOpacity,
            showButtonText,
            pConfig,
            pLibrary,
            pAutoDJFeature->processor(),
            pKeyboard);
    pAutoDJFeature->bindAndysPaneView(m_pAutoDJView);

    QVBoxLayout* pLayout = new QVBoxLayout(this);
    pLayout->setContentsMargins(0, 0, 0, 0);
    pLayout->setSpacing(0);
    pLayout->addWidget(m_pAutoDJView, 1);
}
