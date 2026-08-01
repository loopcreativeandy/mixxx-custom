#pragma once

#include <QAbstractItemModel>
#include <QSortFilterProxyModel>

#include "control/controlproxy.h"
#include "control/pollingcontrolproxy.h"
#include "library/dao/playlistdao.h"
#include "library/trackmodel.h" // Can't forward declare enums
#include "preferences/usersettings.h"
#include "util/duration.h"
#include "util/parented_ptr.h"
#include "widget/wlibrarytableview.h"
#ifdef __STEM__
#include "engine/engine.h"
#endif

class ControlProxy;
class DlgTagFetcher;
class DlgTrackInfo;
class ExternalTrackCollection;
class Library;
class WTrackMenu;

class WTrackTableView : public WLibraryTableView {
    Q_OBJECT
  public:
    WTrackTableView(
            QWidget* pParent,
            UserSettingsPointer pConfig,
            Library* pLibrary,
            double backgroundColorOpacity);
    ~WTrackTableView() override;
#ifdef __LINUX__
    void currentChanged(const QModelIndex& current, const QModelIndex& previous) override;
#endif
    void contextMenuEvent(QContextMenuEvent * event) override;
    QString columnNameOfIndex(const QModelIndex& index) const;
    void onSearch(const QString& text) override;
    void onShow() override;
    bool hasFocus() const override;
    void setFocus() override;
    void pasteFromSidebar() override;
    void keyPressEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void editSelectedItem();
    void activateSelectedTrack();
#ifdef __STEM__
    void loadSelectedTrackToGroup(const QString& group,
            mixxx::StemChannelSelection stemMask,
            bool play);
#else
    void loadSelectedTrackToGroup(const QString& group,
            bool play);
#endif
    void assignNextTrackColor() override;
    void assignPreviousTrackColor() override;
    TrackModel::SortColumnId getColumnIdFromCurrentIndex() override;
    QList<TrackId> getSelectedTrackIds() const;
    bool isTrackInCurrentView(const TrackId& trackId);
    void setSelectedTracks(const QList<TrackId>& tracks);
    TrackId getCurrentTrackId() const;
    bool setCurrentTrackId(const TrackId& trackId, int column = 0, bool scrollToTrack = false);

    void addToAutoDJBottom();
    void addToAutoDJTop();
    void addToAutoDJReplace();
    void selectTrack(const TrackId&);

    /// Identifier stamped on drags started here and checked on drops. Mixxx
    /// rejects a drop when its mime text equals this identifier (its way of
    /// forbidding a table dropping onto itself). Every WTrackTableView defaults
    /// to "library", which means two library tables (e.g. Andy's side playlist
    /// pane next to the main library) can't exchange tracks. Give a secondary
    /// table a distinct identifier so drags between the two are accepted.
    void setDragSourceIdentifier(const QString& identifier) {
        m_dragSourceIdentifier = identifier;
    }

    /// Detach this table from the global [Library],sort_column/sort_order
    /// controls. Upstream routes every sort through those two controls, which
    /// works because only one WTrackTableView is ever visible at a time. Andy's
    /// side playlist pane is visible *alongside* the main library, so both
    /// tables saw each other's sort clicks: sorting the pane by playlist
    /// position re-sorted the search results and vice versa. An independent
    /// table sorts itself directly from its own header (whose sort indicator is
    /// part of the persisted per-table header state) and neither reads nor
    /// writes the shared controls, so controller/global sorting keeps driving
    /// the main library only.
    void setIndependentSorting(bool independent) {
        m_independentSorting = independent;
    }

    void removeSelectedTracks();
    void cutSelectedTracks();
    void copySelectedTracks();
    void pasteTracks(const QModelIndex& index);

    void moveSelection(int delta);
    void moveRows(QList<int> selectedRows, int destRow);
    void moveSelectedTracks(QKeyEvent* event);
    void selectTracksById(const QList<TrackId>& tracks, int prevColumn);
    void selectTracksByPosition(const QList<int>& positions, int prevColum);

    double getBackgroundColorOpacity() const {
        return m_backgroundColorOpacity;
    }

    // Color for the table items' focus border. Default: white
    // Used by table delegates.
    static constexpr QColor kDefaultFocusBorderColor = QColor(0xff, 0xff, 0xff);
    Q_PROPERTY(QColor focusBorderColor
                    MEMBER m_focusBorderColor
                            NOTIFY focusBorderColorChanged
                                    DESIGNABLE true);
    QColor getFocusBorderColor() const {
        return m_focusBorderColor;
    }

    // Color for played tracks' text color. Default: a bit darker than Qt::darkgray
    // BaseTrackTableModel uses this for the ForegroundRole of played tracks.
    static constexpr QColor kDefaultTrackPlayedColor = QColor(0x55, 0x55, 0x55);
    Q_PROPERTY(QColor trackPlayedColor
                    MEMBER m_trackPlayedColor
                            NOTIFY trackPlayedColorChanged
                                    DESIGNABLE true);
    QColor getTrackPlayedColor() const {
        return m_trackPlayedColor;
    }
    // Color for missing tracks' text color.  Default: red
    // BaseTrackTableModel uses this for the ForegroundRole of missing tracks.
    static constexpr QColor kDefaultTrackMissingColor = QColor(0xff, 0x00, 0x00);
    Q_PROPERTY(QColor trackMissingColor
                    MEMBER m_trackMissingColor
                            NOTIFY trackMissingColorChanged
                                    DESIGNABLE true);
    QColor getTrackMissingColor() const {
        return m_trackMissingColor;
    }
    // Color for the track drop indicator line. Default: red
    static constexpr QColor kDefaultDropIndicatorColor = QColor(0xff, 0x00, 0x00);
    Q_PROPERTY(QColor dropIndicatorColor
                    MEMBER m_dropIndicatorColor
                            NOTIFY dropIndicatorColorChanged
                                    DESIGNABLE true);

  signals:
    void trackMenuVisible(bool visible);
    void focusBorderColorChanged(QColor col);
    void trackPlayedColorChanged(QColor col);
    void trackMissingColorChanged(QColor col);
    void dropIndicatorColorChanged(QColor col);

  public slots:
    void loadTrackModel(QAbstractItemModel* model, bool restoreState = false);
    void slotMouseDoubleClicked(const QModelIndex &);
    void slotUnhide();
    void slotPurge();
    void slotDeleteTracksFromDisk();
    void slotShowHideTrackMenu(bool show);

    void slotSaveCurrentViewState() {
        saveCurrentViewState();
    };
    /// Persist the current header layout (column order/visibility/width/sort)
    /// to the model's settings right now. Normally this only happens when the
    /// model is swapped or the view is destroyed; a view that keeps one model
    /// for its whole lifetime (e.g. Andy's side playlist pane) can call this to
    /// save eagerly instead of relying on the fragile shutdown-time save.
    void slotSaveCurrentHeaderState();
    bool slotRestoreCurrentViewState() {
        return restoreCurrentViewState();
    };
    void slotrestoreCurrentIndex() {
        restoreCurrentIndex();
    }

  private slots:
    void doSortByColumn(int headerSection, Qt::SortOrder sortOrder);
    void applySortingIfVisible();
    void applySorting();
    void applySorting(TrackModel::SortColumnId sortColumnId, Qt::SortOrder sortOrder);

    // Signalled 20 times per second (every 50ms) by GuiTick.
    void slotGuiTick50ms(double);
    void slotScrollValueChanged(int);

    void slotSortingChanged(int headerSection, Qt::SortOrder order);
    void slotRandomSorting();
    void keyNotationChanged();

  protected:
    QString getModelStateKey() const override;

  private:
    void addToAutoDJ(PlaylistDAO::AutoDJSendLoc loc);
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

    void paintEvent(QPaintEvent* e) override;

    void enableCachedOnly();
    void selectionChanged(const QItemSelection &selected,
                          const QItemSelection &deselected) override;

    void mousePressEvent(QMouseEvent* pEvent) override;
    // Mouse move event, implemented to hide the text and show an icon instead
    // when dragging.
    void mouseMoveEvent(QMouseEvent *pEvent) override;

    // Returns the list of selected row indices, or an empty list if none are selected.
    QModelIndexList getSelectedRows() const;
    // Returns the list of selected row numbers, or an empty list if none are selected.
    QList<int> getSelectedRowNumbers() const;

    // Returns the current TrackModel, or returns NULL if none is set.
    TrackModel* getTrackModel() const;

    void initTrackMenu();
    void showTrackMenu(const QPoint pos, const QModelIndex& index);

    void hideOrRemoveSelectedTracks();

    const UserSettingsPointer m_pConfig;
    Library* const m_pLibrary;

    // Context menu container
    parented_ptr<WTrackMenu> m_pTrackMenu;

    const double m_backgroundColorOpacity;
    QColor m_focusBorderColor;
    QColor m_trackPlayedColor;
    QColor m_trackMissingColor;
    QColor m_dropIndicatorColor;
    // Drag/drop source tag; see setDragSourceIdentifier(). Defaults to "library".
    QString m_dragSourceIdentifier;
    bool m_sorting;

    // Control the delay to load a cover art.
    mixxx::Duration m_lastUserAction;
    bool m_selectionChangedSinceLastGuiTick;
    bool m_loadCachedOnly;

    ControlProxy* m_pCOTGuiTick;
    ControlProxy* m_pKeyNotation;
    ControlProxy* m_pSortColumn;
    ControlProxy* m_pSortOrder;
    bool m_independentSorting;

    int m_dropRow;
};
