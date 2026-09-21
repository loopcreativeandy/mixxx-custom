#pragma once

#include <QModelIndex>
#include <QPointer>
#include <QVariant>

#include "library/trackset/basetracksetfeature.h"
#include "library/trackset/similar/similartablemodel.h"
#include "preferences/usersettings.h"
#include "track/trackid.h"

class Library;
class SimilarityIndex;
class WLibrary;
class WLibraryTextBrowser;

/// Sidebar item holding the result of the last "Find Similar" query (andy-custom).
///
/// One reusable item rather than a playlist per query: the result set is a view onto the
/// library, not a thing worth keeping. Nothing about it is persisted, and nothing is
/// written to the database - see SimilarTableModel for how the score column is carried.
class SimilarFeature : public BaseTrackSetFeature {
    Q_OBJECT
  public:
    SimilarFeature(Library* pLibrary,
            UserSettingsPointer pConfig,
            SimilarityIndex* pSimilarityIndex);
    ~SimilarFeature() override = default;

    QVariant title() override;

    void bindLibraryWidget(WLibrary* pLibraryWidget,
            KeyboardEventFilter* pKeyboard) override;

    TreeItemModel* sidebarModel() const override;

    bool hasTrackTable() override {
        return true;
    }

    /// Runs a query and shows the result, selecting this item in the sidebar.
    void showSimilarTo(TrackId seedTrackId);

  public slots:
    void activate() override;

  private:
    /// Index status and usage notes, shown until the first query of the session.
    QString getRootViewHtml() const;
    void updateRootView();

    SimilarityIndex* const m_pSimilarityIndex;
    SimilarTableModel m_similarTableModel;
    bool m_hasResults;
    QPointer<WLibraryTextBrowser> m_pRootView;
};
