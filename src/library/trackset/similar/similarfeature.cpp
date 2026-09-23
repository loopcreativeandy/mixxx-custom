#include "library/trackset/similar/similarfeature.h"

#include "library/library.h"
#include "library/similarity/similarityindex.h"
#include "library/trackcollectionmanager.h"
#include "library/treeitem.h"
#include "track/track.h"
#include "moc_similarfeature.cpp"
#include "widget/wlibrary.h"
#include "widget/wlibrarytextbrowser.h"

namespace {

const QString kRootViewName = QStringLiteral("SIMILARHOME");

} // anonymous namespace

SimilarFeature::SimilarFeature(Library* pLibrary,
        UserSettingsPointer pConfig,
        SimilarityIndex* pSimilarityIndex)
        : BaseTrackSetFeature(pLibrary,
                  pConfig,
                  kRootViewName,
                  QStringLiteral("playlist")),
          m_pSimilarityIndex(pSimilarityIndex),
          m_similarTableModel(this, pLibrary->trackCollectionManager()),
          m_hasResults(false) {
    // A single item with no children: it is refilled per query, so there is nothing to
    // build a tree out of.
    m_pSidebarModel->setRootItem(TreeItem::newRoot(this));
}

QVariant SimilarFeature::title() {
    return tr("Similar");
}

TreeItemModel* SimilarFeature::sidebarModel() const {
    return m_pSidebarModel;
}

void SimilarFeature::bindLibraryWidget(WLibrary* pLibraryWidget,
        KeyboardEventFilter* pKeyboard) {
    Q_UNUSED(pKeyboard);
    WLibraryTextBrowser* pEdit = new WLibraryTextBrowser(pLibraryWidget);
    pEdit->setHtml(getRootViewHtml());
    pEdit->setOpenLinks(false);
    m_pRootView = QPointer(pEdit);
    pLibraryWidget->registerView(kRootViewName, pEdit);
}

void SimilarFeature::updateRootView() {
    if (m_pRootView) {
        m_pRootView->setHtml(getRootViewHtml());
    }
}

QString SimilarFeature::getRootViewHtml() const {
    const QString title = tr("Similar");
    const QString description =
            tr("Right-click a track in the library and choose <b>Find Similar</b>. "
               "Every track with a similarity of at least %1 appears here - and "
               "always at least the %2 closest - ranked from 1 = most similar, "
               "with the similarity in its own sortable column. Rank 0 on top is "
               "the track itself. Typing in the search bar filters within the "
               "results.")
                    .arg(QString::number(m_pSimilarityIndex->minScore(), 'f', 2),
                            QString::number(m_pSimilarityIndex->minResults()));

    QString statusHtml;
    SimilarityIndex::Status status = m_pSimilarityIndex->status();
    if (!status.loaded) {
        statusHtml = tr("No usable similarity index: %1").arg(status.error.toHtmlEscaped());
        if (!status.filePath.isEmpty()) {
            statusHtml += QStringLiteral("<br>") +
                    tr("Expected at: %1").arg(status.filePath.toHtmlEscaped());
        }
    } else {
        statusHtml = tr("%1 tracks in the index, built %2.")
                             .arg(QString::number(status.vectorCount),
                                     status.builtAt.toHtmlEscaped());
        const int missing = status.libraryTrackCount - status.matchedCount;
        if (missing > 0) {
            // This is the number that matters: the index is copied in by hand, so it can
            // quietly fall behind the library.
            statusHtml += QStringLiteral("<br>") +
                    tr("%1 of %2 library tracks have no similarity data yet.")
                            .arg(QString::number(missing),
                                    QString::number(status.libraryTrackCount));
        } else {
            statusHtml += QStringLiteral("<br>") +
                    tr("All %1 library tracks are covered.")
                            .arg(QString::number(status.libraryTrackCount));
        }
        if (status.unclaimedCount > 0) {
            statusHtml += QStringLiteral("<br>") +
                    tr("%1 entries in the index match no library track.")
                            .arg(QString::number(status.unclaimedCount));
        }
    }

    QString noMatchesHtml;
    if (!m_noMatchesFor.isEmpty()) {
        noMatchesHtml = QStringLiteral("<p><b>%1</b></p>")
                                .arg(tr("No track is at least %1 similar to %2.")
                                                .arg(QString::number(
                                                             m_pSimilarityIndex->minScore(),
                                                             'f',
                                                             2),
                                                        m_noMatchesFor.toHtmlEscaped()));
    }

    return QStringLiteral(
            "<html><body><h2>%1</h2>%2<p>%3</p><p><i>%4</i></p></body></html>")
            .arg(title, noMatchesHtml, description, statusHtml);
}

void SimilarFeature::activate() {
    if (m_hasResults) {
        emit saveModelState();
        emit showTrackModel(&m_similarTableModel);
        emit enableCoverArtDisplay(true);
        return;
    }
    // Nothing queried yet this session: explain the feature and report on the index.
    updateRootView();
    BaseTrackSetFeature::activate();
}

void SimilarFeature::showSimilarTo(TrackId seedTrackId) {
    const QList<SimilarityIndex::Neighbour> results =
            m_pSimilarityIndex->nearestAbove(seedTrackId,
                    m_pSimilarityIndex->minScore(),
                    m_pSimilarityIndex->minResults());
    m_noMatchesFor.clear();
    if (results.isEmpty()) {
        // Either no vector for the seed or everything was filtered out. Say so in the
        // root view rather than showing an empty track table with no explanation.
        // With a vector, the threshold is why - name the track so it is clear the
        // query did run.
        if (m_pSimilarityIndex->hasVectorFor(seedTrackId)) {
            const TrackPointer pSeed = m_pLibrary->trackCollectionManager()->getTrackById(
                    seedTrackId);
            m_noMatchesFor = pSeed ? pSeed->getInfo() : tr("this track");
        }
        m_hasResults = false;
        updateRootView();
        emit featureSelect(this, QModelIndex());
        BaseTrackSetFeature::activate();
        return;
    }
    m_hasResults = true;
    emit saveModelState();
    m_similarTableModel.setResults(results, seedTrackId);
    emit featureSelect(this, QModelIndex());
    emit showTrackModel(&m_similarTableModel);
    emit enableCoverArtDisplay(true);
}
