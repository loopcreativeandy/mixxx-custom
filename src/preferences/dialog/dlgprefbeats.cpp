#include "preferences/dialog/dlgprefbeats.h"

#include "analyzer/analyzerbeats.h"
#include "defs_urls.h"
#include "engine/beatclick.h"
#include "moc_dlgprefbeats.cpp"
#include "util/math.h"

namespace {
/// Beatgrid check click. Both controls are created by EngineMixer and are
/// persistent, so these ConfigKeys are the stored settings as well.
const ConfigKey kBeatClickKey = ConfigKey(
        QStringLiteral("[Master]"), QStringLiteral("preview_beat_click"));
const ConfigKey kBeatClickGainKey = ConfigKey(
        QStringLiteral("[Master]"), QStringLiteral("preview_beat_click_gain"));
} // namespace

DlgPrefBeats::DlgPrefBeats(QWidget* parent, UserSettingsPointer pConfig)
        : DlgPreferencePage(parent),
          m_pConfig(pConfig),
          m_bpmSettings(pConfig),
          m_bAnalyzerEnabled(m_bpmSettings.getBpmDetectionEnabledDefault()),
          m_bFixedTempoEnabled(m_bpmSettings.getFixedTempoAssumptionDefault()),
          m_bFastAnalysisEnabled(m_bpmSettings.getFastAnalysisDefault()),
          m_bReanalyze(m_bpmSettings.getReanalyzeWhenSettingsChangeDefault()),
          m_bReanalyzeImported(m_bpmSettings.getReanalyzeImportedDefault()),
          m_stemStrategy(BeatDetectionSettings::StemStrategy::Disabled),
          m_pBeatClickCO(make_parented<ControlProxy>(kBeatClickKey, this)),
          m_pBeatClickGainCO(make_parented<ControlProxy>(kBeatClickGainKey, this)),
          m_bBeatClick(false),
          m_beatClickGainDb(0) {
    setupUi(this);

    m_availablePlugins = AnalyzerBeats::availablePlugins();
    for (const auto& info : std::as_const(m_availablePlugins)) {
        comboBoxBeatPlugin->addItem(info.name(), info.id());
    }

    slotUpdate();

    // TODO (#13466) Keeping the setting hidden for now
    comboBoxStemStrategy->hide();
    labelStemStrategy->hide();

    // Connections
    connect(comboBoxBeatPlugin,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,
            &DlgPrefBeats::pluginSelected);
    connect(checkBoxAnalyzerEnabled,
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
            &QCheckBox::checkStateChanged,
#else
            &QCheckBox::stateChanged,
#endif
            this,
            &DlgPrefBeats::analyzerEnabled);
    connect(checkBoxFixedTempo,
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
            &QCheckBox::checkStateChanged,
#else
            &QCheckBox::stateChanged,
#endif
            this,
            &DlgPrefBeats::fixedtempoEnabled);
    connect(checkBoxFastAnalysis,
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
            &QCheckBox::checkStateChanged,
#else
            &QCheckBox::stateChanged,
#endif
            this,
            &DlgPrefBeats::fastAnalysisEnabled);
    connect(checkBoxReanalyze,
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
            &QCheckBox::checkStateChanged,
#else
            &QCheckBox::stateChanged,
#endif
            this,
            &DlgPrefBeats::slotReanalyzeChanged);
    connect(checkBoxReanalyzeImported,
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
            &QCheckBox::checkStateChanged,
#else
            &QCheckBox::stateChanged,
#endif
            this,
            &DlgPrefBeats::slotReanalyzeImportedChanged);
    connect(comboBoxStemStrategy,
            &QComboBox::currentIndexChanged,
            this,
            &DlgPrefBeats::slotStemStrategyChanged);
    connect(checkBoxBeatClick,
            &QCheckBox::toggled,
            this,
            &DlgPrefBeats::slotBeatClickToggled);
    connect(spinBoxBeatClickGain,
            QOverload<int>::of(&QSpinBox::valueChanged),
            this,
            &DlgPrefBeats::slotBeatClickGainChanged);
    // Follow the live controls while the dialog is open: Ctrl+Shift+P or a
    // controller can change them, and Apply/OK must not write a stale value
    // back over that.
    m_pBeatClickCO->connectValueChanged(this, [this](double value) {
        checkBoxBeatClick->setChecked(value > 0);
    });
    m_pBeatClickGainCO->connectValueChanged(this, [this](double value) {
        spinBoxBeatClickGain->setValue(static_cast<int>(std::lround(value)));
    });

    setScrollSafeGuard(comboBoxBeatPlugin);
    setScrollSafeGuard(spinBoxBeatClickGain);
}

DlgPrefBeats::~DlgPrefBeats() {
}

QUrl DlgPrefBeats::helpUrl() const {
    return QUrl(MIXXX_MANUAL_BEATS_URL);
}

void DlgPrefBeats::slotResetToDefaults() {
    if (m_availablePlugins.size() > 0) {
        m_selectedAnalyzerId = m_availablePlugins[0].id();
    }
    m_bAnalyzerEnabled = m_bpmSettings.getBpmDetectionEnabledDefault();
    m_bFixedTempoEnabled = m_bpmSettings.getFixedTempoAssumptionDefault();
    m_bFastAnalysisEnabled = m_bpmSettings.getFastAnalysisDefault();
    m_bReanalyze = m_bpmSettings.getReanalyzeWhenSettingsChangeDefault();
    m_bReanalyzeImported = m_bpmSettings.getReanalyzeImportedDefault();
    m_stemStrategy = m_bpmSettings.getStemStrategyDefault();
    m_bBeatClick = false;
    m_beatClickGainDb = 0;

    updateGui();
    updateBeatClickGui();
}

void DlgPrefBeats::pluginSelected(int i) {
    if (i == -1) {
        return;
    }
    m_selectedAnalyzerId = m_availablePlugins[i].id();
    updateGui();
}

#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
void DlgPrefBeats::analyzerEnabled(Qt::CheckState state) {
    m_bAnalyzerEnabled = (state == Qt::Checked);
#else
void DlgPrefBeats::analyzerEnabled(int i) {
    m_bAnalyzerEnabled = static_cast<bool>(i);
#endif
    updateGui();
}

#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
void DlgPrefBeats::fixedtempoEnabled(Qt::CheckState state) {
    m_bFixedTempoEnabled = (state == Qt::Checked);
#else
void DlgPrefBeats::fixedtempoEnabled(int i) {
    m_bFixedTempoEnabled = static_cast<bool>(i);
#endif
    updateGui();
}

void DlgPrefBeats::slotUpdate() {
    // Read true values from config
    m_selectedAnalyzerId = m_bpmSettings.getBeatPluginId();
    m_bAnalyzerEnabled = m_bpmSettings.getBpmDetectionEnabled();
    m_bFixedTempoEnabled = m_bpmSettings.getFixedTempoAssumption();
    m_bReanalyze = m_bpmSettings.getReanalyzeWhenSettingsChange();
    m_bReanalyzeImported = m_bpmSettings.getReanalyzeImported();
    m_bFastAnalysisEnabled = m_bpmSettings.getFastAnalysis();
    m_stemStrategy = m_bpmSettings.getStemStrategy();
    m_bBeatClick = m_pBeatClickCO->toBool();
    m_beatClickGainDb = static_cast<int>(std::lround(m_pBeatClickGainCO->get()));

    updateGui();
    updateBeatClickGui();
}

void DlgPrefBeats::updateBeatClickGui() {
    // Not part of updateGui(): that one returns early when the analyzer is
    // disabled, and the click works regardless of the analyzer settings.
    const QSignalBlocker checkBoxBlocker(checkBoxBeatClick);
    const QSignalBlocker spinBoxBlocker(spinBoxBeatClickGain);
    checkBoxBeatClick->setChecked(m_bBeatClick);
    spinBoxBeatClickGain->setValue(m_beatClickGainDb);
}

void DlgPrefBeats::slotBeatClickToggled(bool checked) {
    // Written to the control in slotApply(), like every other setting on this
    // page, so that Cancel discards it. Ctrl+Shift+P toggles the click live.
    m_bBeatClick = checked;
}

void DlgPrefBeats::slotBeatClickGainChanged(int gainDb) {
    m_beatClickGainDb = math_clamp(gainDb,
            static_cast<int>(kBeatClickGainMinDb),
            static_cast<int>(kBeatClickGainMaxDb));
}

void DlgPrefBeats::updateGui() {
    checkBoxFixedTempo->setEnabled(m_bAnalyzerEnabled);
    comboBoxBeatPlugin->setEnabled(m_bAnalyzerEnabled);
    checkBoxAnalyzerEnabled->setChecked(m_bAnalyzerEnabled);
    // Fast analysis cannot be combined with non-constant tempo beatgrids.
    checkBoxFastAnalysis->setEnabled(m_bAnalyzerEnabled && m_bFixedTempoEnabled);
    checkBoxReanalyze->setEnabled(m_bAnalyzerEnabled);
    checkBoxReanalyzeImported->setEnabled(m_bAnalyzerEnabled);

    if (!m_bAnalyzerEnabled) {
        return;
    }

    if (m_availablePlugins.size() > 0) {
        bool found = false;
        for (int i = 0; i < m_availablePlugins.size(); ++i) {
            const auto& info = m_availablePlugins.at(i);
            if (info.id() == m_selectedAnalyzerId) {
                found = true;
                comboBoxBeatPlugin->setCurrentIndex(i);
                if (!m_availablePlugins[i].isConstantTempoSupported()) {
                    checkBoxFixedTempo->setEnabled(false);
                }
                break;
            }
        }
        if (!found) {
            comboBoxBeatPlugin->setCurrentIndex(0);
            m_selectedAnalyzerId = m_availablePlugins[0].id();
        }
    }

    checkBoxFixedTempo->setChecked(m_bFixedTempoEnabled);
    // Fast analysis cannot be combined with non-constant tempo beatgrids.
    checkBoxFastAnalysis->setChecked(m_bFastAnalysisEnabled && m_bFixedTempoEnabled);

    checkBoxReanalyze->setChecked(m_bReanalyze);
    checkBoxReanalyzeImported->setChecked(m_bReanalyzeImported);

    comboBoxStemStrategy->setCurrentIndex(
            m_stemStrategy == BeatDetectionSettings::StemStrategy::Enforced
                    ? 1
                    : 0);
}

#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
void DlgPrefBeats::slotReanalyzeChanged(Qt::CheckState state) {
    m_bReanalyze = (state == Qt::Checked);
#else
void DlgPrefBeats::slotReanalyzeChanged(int value) {
    m_bReanalyze = static_cast<bool>(value);
#endif
    updateGui();
}

#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
void DlgPrefBeats::slotReanalyzeImportedChanged(Qt::CheckState state) {
    m_bReanalyzeImported = (state == Qt::Checked);
#else
void DlgPrefBeats::slotReanalyzeImportedChanged(int value) {
    m_bReanalyzeImported = static_cast<bool>(value);
#endif
    updateGui();
}

#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
void DlgPrefBeats::fastAnalysisEnabled(Qt::CheckState state) {
    m_bFastAnalysisEnabled = (state == Qt::Checked);
#else
void DlgPrefBeats::fastAnalysisEnabled(int i) {
    m_bFastAnalysisEnabled = static_cast<bool>(i);
#endif
    updateGui();
}

void DlgPrefBeats::slotStemStrategyChanged(int index) {
    switch (index) {
    case 1:
        m_stemStrategy = BeatDetectionSettings::StemStrategy::Enforced;
        break;
    default:
        m_stemStrategy = BeatDetectionSettings::StemStrategy::Disabled;
        break;
    }
    updateGui();
}

void DlgPrefBeats::slotApply() {
    m_bpmSettings.setBeatPluginId(m_selectedAnalyzerId);
    m_bpmSettings.setBpmDetectionEnabled(m_bAnalyzerEnabled);
    m_bpmSettings.setFixedTempoAssumption(m_bFixedTempoEnabled);
    m_bpmSettings.setReanalyzeWhenSettingsChange(m_bReanalyze);
    m_bpmSettings.setReanalyzeImported(m_bReanalyzeImported);
    m_bpmSettings.setFastAnalysis(m_bFastAnalysisEnabled);
    m_bpmSettings.setStemStrategy(m_stemStrategy);

    m_pBeatClickCO->set(m_bBeatClick ? 1.0 : 0.0);
    m_pBeatClickGainCO->set(m_beatClickGainDb);
    // The controls persist themselves on shutdown; write the config here too so
    // an unclean exit cannot lose the setting.
    m_pConfig->set(kBeatClickKey, ConfigValue(m_bBeatClick ? 1 : 0));
    m_pConfig->set(kBeatClickGainKey, ConfigValue(m_beatClickGainDb));
}
