#include "widget/wraterange.h"

#include "control/controlproxy.h"
#include "moc_wraterange.cpp"
#include "skin/legacy/skincontext.h"
#include "util/math.h"

WRateRange::WRateRange(const QString& group, QWidget* parent)
        : WNumber(parent),
          m_nodePosition(VerticalPosition::Top),
          m_nodeDisplay(DisplayType::Default) {
    m_pRateRangeControl = new ControlProxy(
            group, "rateRange", this, ControlFlag::NoAssertIfMissing);
    m_pRateRangeControl->connectValueChanged(this, &WRateRange::setValue);
    m_pRateDirControl = new ControlProxy(
            group, "rate_dir", this, ControlFlag::NoAssertIfMissing);
    m_pRateDirControl->connectValueChanged(this, &WRateRange::slotRateDirChanged);
}

void WRateRange::setup(const QDomNode& node, const SkinContext& context) {
    WNumber::setup(node, context);

    QDomElement RateRangePosition = context.selectElement(node, "Position");
    QDomElement RateRangeType = context.selectElement(node, "Display");
    m_nodePosition = RateRangePosition.text() == "Top"
            ? VerticalPosition::Top
            : VerticalPosition::Bottom;
    if (RateRangeType.text() == "prefix") {
        m_nodeDisplay = DisplayType::Prefix;
    } else if (RateRangeType.text() == "range") {
        m_nodeDisplay = DisplayType::Range;
    } else {
        m_nodeDisplay = DisplayType::Default;
    }
    setAlignment(Qt::AlignCenter);

    // Andy custom: when the tempo slider carries an asymmetric clamp
    // (RateClampMin/MaxPercent), mirror the same span here so the end labels
    // show what the slider ends actually reach (e.g. +8 / -32) instead of
    // +-range. Spans wider than the active rate range clamp at the range.
    m_dClampMinPercent = context.selectDouble(node, "ClampMinPercent", 0.0);
    m_dClampMaxPercent = context.selectDouble(node, "ClampMaxPercent", 0.0);
    // Optional <ClampControl> mirrors the slider's runtime on/off toggle;
    // when off the labels fall back to the plain +-range display.
    QString clampControl;
    if (context.hasNodeSelectString(node, "ClampControl", &clampControl) &&
            !clampControl.isEmpty()) {
        const ConfigKey clampKey = ConfigKey::parseCommaSeparated(clampControl);
        m_pClampToggle = new ControlProxy(
                clampKey, this, ControlFlag::NoAssertIfMissing);
        m_pClampToggle->connectValueChanged(
                this, &WRateRange::slotClampToggleChanged);
    }

    // Initialize the widget (overrides the base class initial value).
    const double range = m_pRateRangeControl->get();
    setValue(range);
}

void WRateRange::slotClampToggleChanged(double v) {
    Q_UNUSED(v);

    const double range = m_pRateRangeControl->get();
    setValue(range);
}

void WRateRange::slotRateDirChanged(double dir) {
    Q_UNUSED(dir);

    const double range = m_pRateRangeControl->get();
    setValue(range);
}

void WRateRange::setValue(double range) {
    const double direction = m_pRateDirControl->get();

    QString prefix('-');
    if (m_nodePosition == VerticalPosition::Top && direction > 0) {
        prefix = '+';
    }

    if (m_nodePosition == VerticalPosition::Bottom && direction < 0) {
        prefix = '+';
    }

    // Magnitude shown at this end. Without a clamp both ends read the full
    // range; with one, the faster end reads the up-clamp and the slower end
    // the down-clamp (each limited by the range itself).
    double displayPercent = range * 100;
    const bool clampEnabled = !m_pClampToggle || m_pClampToggle->get() != 0.0;
    if (clampEnabled && m_dClampMinPercent < m_dClampMaxPercent) {
        const double upPercent =
                math_clamp(m_dClampMaxPercent, 0.0, range * 100);
        const double downPercent =
                math_clamp(-m_dClampMinPercent, 0.0, range * 100);
        const bool fasterEnd =
                (m_nodePosition == VerticalPosition::Top) == (direction > 0);
        displayPercent = fasterEnd ? upPercent : downPercent;
    }

    if (m_nodeDisplay == DisplayType::Prefix) {
        m_nodeText = prefix;
    } else if (m_nodeDisplay == DisplayType::Range) {
        m_nodeText = QString::number(displayPercent);
    } else {
        m_nodeText = prefix.append(QString::number(displayPercent));
    }

    setText(m_nodeText);
}
