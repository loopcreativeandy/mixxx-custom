#pragma once

#include <QList>

#include "library/librarytablemodel.h"
#include "library/similarity/similarityindex.h"

/// Track model behind the "Similar" sidebar item (andy-custom).
///
/// It is the plain library model over a view that is joined against a temporary table
/// holding the current result set. That single join does three jobs: it restricts the
/// view to the hits, it carries the similarity score as a real column so the header can
/// sort on it, and it needs no rows in any persistent table - the temporary objects live
/// and die with the database connection, so mixxx.sqlite is not modified at all.
class SimilarTableModel : public LibraryTableModel {
    Q_OBJECT
  public:
    SimilarTableModel(QObject* parent,
            TrackCollectionManager* pTrackCollectionManager);
    ~SimilarTableModel() override = default;

    /// Replaces the result set and re-selects, best match first.
    void setResults(const QList<SimilarityIndex::Neighbour>& results);

    /// Like the library, minus ReceiveDrops: membership comes from a query, so tracks
    /// cannot be dropped in here.
    TrackModel::Capabilities getCapabilities() const override;

  protected:
    void initSortColumnMapping() override;

  private:
    /// Creates the temporary table and the view over it, then hands both to the base
    /// class. Called from our own constructor, not from the base one.
    void setupSimilarTable();
};
