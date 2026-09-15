#pragma once

#include "preferences/configobject.h"

namespace mixxx {

namespace library {

namespace prefs {

extern const ConfigKey kLegacyDirectoryConfigKey;

extern const QString kConfigGroup;

extern const ConfigKey kRescanOnStartupConfigKey;

extern const ConfigKey kRepairDatabaseOnNextRestartConfigKey;

extern const ConfigKey kShowScanSummaryConfigKey;

extern const ConfigKey kKeyNotationConfigKey;

extern const ConfigKey kTrackDoubleClickActionConfigKey;

extern const ConfigKey kSearchDebouncingTimeoutMillisConfigKey;

extern const ConfigKey kSearchBpmFuzzyRangeConfigKey;

extern const ConfigKey kEnableSearchCompletionsConfigKey;

extern const ConfigKey kEnableSearchHistoryShortcutsConfigKey;

extern const ConfigKey kBpmColumnPrecisionConfigKey;

extern const ConfigKey kApplyPlayedTrackColorConfigKey;

extern const ConfigKey kEditMetadataSelectedClickConfigKey;

extern const ConfigKey kHistoryMinTracksToKeepConfigKey;

const int kHistoryMinTracksToKeepDefault = 1;

extern const ConfigKey kHistoryTrackDuplicateDistanceConfigKey;

const int kHistoryTrackDuplicateDistanceDefault = 6;

/// Andy: practice mode - tracks played while browsing, cueing and tagging are
/// marked played for the session only: no play count, no last played date,
/// no session history entry.
extern const ConfigKey kHistoryPracticeModeConfigKey;

const bool kHistoryPracticeModeDefault = false;

const bool kEditMetadataSelectedClickDefault = false;

extern const ConfigKey kSyncTrackMetadataConfigKey;

extern const ConfigKey kResetMissingTagMetadataOnImportConfigKey;

extern const ConfigKey kSyncSeratoMetadataConfigKey;

extern const ConfigKey kUseRelativePathOnExportConfigKey;

extern const ConfigKey kCoverArtFetcherQualityConfigKey;

extern const ConfigKey kTagFetcherApplyTagsConfigKey;

extern const ConfigKey kTagFetcherApplyCoverConfigKey;

} // namespace prefs

} // namespace library

} // namespace mixxx
