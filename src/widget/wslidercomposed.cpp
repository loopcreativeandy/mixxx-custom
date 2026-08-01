#include "widget/wslidercomposed.h"

#include <QStringView>
#include <QStyleOption>
#include <QStylePainter>
#include <QtDebug>

#include "control/controlproxy.h"
#include "moc_wslidercomposed.cpp"
#include "skin/legacy/skincontext.h"
#include "util/debug.h"
#include "widget/controlwidgetconnection.h"
#include "widget/wpixmapstore.h"
#include "widget/wskincolor.h"

WSliderComposed::WSliderComposed(QWidget* parent)
        : WWidget(parent),
          m_dHandleLength(0.0),
          m_dSliderLength(0.0),
          m_bHorizontal(false),
          m_dBarWidth(0.0),
          m_dBarBgWidth(0.0),
          m_dBarStart(0.0),
          m_dBarEnd(0.0),
          m_dBarBgStart(0.0),
          m_dBarBgEnd(0.0),
          m_dBarAxisPos(0.0),
          m_bBarUnipolar(true),
          m_barColor(nullptr),
          m_barBgColor(nullptr),
          m_barPenCap(Qt::FlatCap),
          m_pSlider(nullptr),
          m_pHandle(nullptr),
          m_dConfiguredExponent(1.0),
          m_dConfiguredCenter(0.5),
          m_pExponentToggle(nullptr),
          m_dRateClampMinPercent(0.0),
          m_dRateClampMaxPercent(0.0),
          m_pRateRangeControl(nullptr),
          m_pRateDirControl(nullptr),
          m_pRateClampToggle(nullptr) {
}

WSliderComposed::~WSliderComposed() {
    unsetPixmaps();
}

bool WSliderComposed::tryParseHorizontal(const QDomNode& node) const {
    QDomNode horiNode = SkinContext::selectNode(node, "Horizontal");
    if (!horiNode.isNull()) {
        QDomNode child = horiNode.firstChild();
        if (!child.isNull() && child.isText()) {
            // No support for variables.
            if (child.nodeValue().contains("true", Qt::CaseInsensitive)) {
                return true;
            }
        }
    }
    return false;
}

void WSliderComposed::setup(const QDomNode& node, const SkinContext& context) {
    // Setup pixmaps
    unsetPixmaps();

    m_bHorizontal = context.selectBool(node, "Horizontal", false);

    double scaleFactor = context.getScaleFactor();
    QDomElement slider = context.selectElement(node, "Slider");
    if (!slider.isNull()) {
        // The implicit default in <1.12.0 was FIXED so we keep it for backwards
        // compatibility.
        PixmapSource sourceSlider = context.getPixmapSource(slider);
        setSliderPixmap(
                sourceSlider,
                context.selectScaleMode(slider, Paintable::DrawMode::Fixed),
                scaleFactor);
    }

    m_dSliderLength = m_bHorizontal ? width() : height();
    m_handler.setSliderLength(m_dSliderLength);

    QDomElement handle = context.selectElement(node, "Handle");
    PixmapSource sourceHandle = context.getPixmapSource(handle);
    // The implicit default in <1.12.0 was FIXED so we keep it for backwards
    // compatibility.
    setHandlePixmap(
            sourceHandle,
            context.selectScaleMode(handle, Paintable::DrawMode::Fixed),
            scaleFactor);

    // Set up the level bar.
    QColor barColor = context.selectColor(node, "BarColor");
    context.hasNodeSelectDouble(node, "BarWidth", &m_dBarWidth);
    if (barColor.isValid() && m_dBarWidth > 0.0) {
        m_barColor = WSkinColor::getCorrectColor(barColor);
        m_dBarWidth *= scaleFactor;
        QString margins;
        QString bgMargins;
        if (context.hasNodeSelectString(node, "BarMargins", &margins)) {
            int comma = margins.indexOf(",");
            if (comma > 0 && comma < margins.size()) {
                bool m1ok;
                bool m2ok;
                QStringView marginsView(margins);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
                double m1 = (marginsView.first(comma)).toDouble(&m1ok);
                double m2 = (marginsView.sliced(comma + 1)).toDouble(&m2ok);
#else
                QLocale c(QLocale::C);
                double m1 = c.toDouble(marginsView.left(comma), &m1ok);
                double m2 = c.toDouble(marginsView.mid(comma + 1), &m2ok);
#endif
                if (m1ok && m2ok) {
                    m_dBarStart = m1 * scaleFactor;
                    m_dBarEnd = m2 * scaleFactor;
                }
            }
        }

        // Set up the bar background if there's a valid color set.
        // If the background width and margins are not set explicitly
        // we simply adopt the settings of the level bar.
        QColor barBgColor = context.selectColor(node, "BarBgColor");
        if (barBgColor.isValid()) {
            m_barBgColor = WSkinColor::getCorrectColor(barBgColor);
            if (context.hasNodeSelectDouble(node, "BarBgWidth", &m_dBarBgWidth)) {
                if (m_dBarBgWidth > 0.0) {
                    m_dBarBgWidth *= scaleFactor;
                }
            } else {
                m_dBarBgWidth = m_dBarWidth;
            }
            if (context.hasNodeSelectString(node, "BarBgMargins", &bgMargins)) {
                int comma = bgMargins.indexOf(",");
                if (comma > 0 && comma + 1 < margins.size()) {
                    bool m1ok;
                    bool m2ok;
                    QStringView bgMarginsView(bgMargins);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
                    double m1 = (bgMarginsView.first(comma)).toDouble(&m1ok);
                    double m2 = (bgMarginsView.sliced(comma + 1)).toDouble(&m2ok);
#else
                    QLocale c(QLocale::C);
                    double m1 = c.toDouble(bgMarginsView.left(comma), &m1ok);
                    double m2 = c.toDouble(bgMarginsView.mid(comma + 1), &m2ok);
#endif
                    if (m1ok && m2ok) {
                        m_dBarBgStart = m1 * scaleFactor;
                        m_dBarBgEnd = m2 * scaleFactor;
                    }
                }
            } else {
                m_dBarBgStart = m_dBarStart;
                m_dBarBgEnd = m_dBarEnd;
            }
        }
        // Shift the bar center line to the right or to the bottom (horizontal sliders)
        if (context.hasNodeSelectDouble(node, "BarAxisPos", &m_dBarAxisPos)) {
            m_dBarAxisPos *= scaleFactor;
        }
        // Draw the bar from 0 by default, from bottom or from left (horizontal)
        m_bBarUnipolar = context.selectBool(node, "BarUnipolar", true);
        if (context.selectBool(node, "BarRoundCaps", false)) {
            m_barPenCap = Qt::RoundCap;
        }
    }

    QString eventWhileDrag;
    if (context.hasNodeSelectString(node, "EventWhileDrag", &eventWhileDrag)) {
        if (eventWhileDrag.contains("no")) {
            m_handler.setEventWhileDrag(false);
        }
    }

    // Andy custom: opt-in non-linear / off-centre handle response. Used by the
    // tempo fader so small moves near the neutral point change the value less
    // than moves toward the ends (<Exponent> > 1), and the neutral point can
    // sit above centre for more downward travel (<NeutralPosition> > 0.5).
    // Absent nodes keep the defaults (1.0 / 0.5) = exact linear/centred.
    m_dConfiguredExponent = context.selectDouble(node, "Exponent", 1.0);
    m_dConfiguredCenter = context.selectDouble(node, "NeutralPosition", 0.5);
    // Optional <ExponentControl> names a control (bound to a skin-settings
    // toggle) that switches the non-linear response on/off at runtime. Without
    // it, the configured response is applied statically.
    QString exponentControl;
    if (context.hasNodeSelectString(node, "ExponentControl", &exponentControl) &&
            !exponentControl.isEmpty()) {
        const ConfigKey key = ConfigKey::parseCommaSeparated(exponentControl);
        m_pExponentToggle = new ControlProxy(
                key, this, ControlFlag::NoAssertIfMissing);
        m_pExponentToggle->connectValueChanged(
                this, &WSliderComposed::slotExponentToggleChanged);
        // Apply the initial state (off = linear).
        slotExponentToggleChanged(m_pExponentToggle->get());
    } else {
        m_handler.setWarp(m_dConfiguredExponent, m_dConfiguredCenter);
    }
    if (!m_connections.empty()) {
        auto& pDefaultConnection = m_connections[0];
        if (pDefaultConnection) {
            if (pDefaultConnection->getEmitOption() &
                    ControlParameterWidgetConnection::EMIT_DEFAULT) {
                // ON_PRESS means here value change on mouse move during press
                pDefaultConnection->setEmitOption(
                        ControlParameterWidgetConnection::EMIT_ON_PRESS_AND_RELEASE);
            }
        }
    }

    // Andy custom: asymmetric linear tempo range. <RateClampMinPercent> /
    // <RateClampMaxPercent> give the playback-speed span (in percent, e.g.
    // -32 / +8) the full physical travel should cover. The reachable
    // parameter window is derived from the connected deck's rateRange and
    // rate_dir controls at runtime, so it holds for any Prefs range wide
    // enough to contain it (narrower ranges clamp at +-range) and follows a
    // flipped slider direction. Mapping is linear; neutral sits off-centre.
    m_dRateClampMinPercent = context.selectDouble(node, "RateClampMinPercent", 0.0);
    m_dRateClampMaxPercent = context.selectDouble(node, "RateClampMaxPercent", 0.0);
    if (m_dRateClampMinPercent < m_dRateClampMaxPercent &&
            !m_connections.empty() && m_connections[0]) {
        const QString group = m_connections[0]->getKey().group;
        m_pRateRangeControl = new ControlProxy(
                group, "rateRange", this, ControlFlag::NoAssertIfMissing);
        m_pRateRangeControl->connectValueChanged(
                this, &WSliderComposed::slotRateClampSourceChanged);
        m_pRateDirControl = new ControlProxy(
                group, "rate_dir", this, ControlFlag::NoAssertIfMissing);
        m_pRateDirControl->connectValueChanged(
                this, &WSliderComposed::slotRateClampSourceChanged);
        // Optional <RateClampControl> names a control (bound to a
        // skin-settings toggle) that switches the asymmetric range on/off at
        // runtime; off = the normal symmetric full range.
        QString clampControl;
        if (context.hasNodeSelectString(node, "RateClampControl", &clampControl) &&
                !clampControl.isEmpty()) {
            const ConfigKey clampKey = ConfigKey::parseCommaSeparated(clampControl);
            m_pRateClampToggle = new ControlProxy(
                    clampKey, this, ControlFlag::NoAssertIfMissing);
            m_pRateClampToggle->connectValueChanged(
                    this, &WSliderComposed::slotRateClampSourceChanged);
        }
        applyRateClampWindow();
    }

    setFocusPolicy(Qt::NoFocus);
}

void WSliderComposed::slotRateClampSourceChanged(double v) {
    Q_UNUSED(v);
    applyRateClampWindow();
}

void WSliderComposed::applyRateClampWindow() {
    double pmin = 0.0;
    double pmax = 1.0;
    const bool clampEnabled = !m_pRateClampToggle || m_pRateClampToggle->get() != 0.0;
    const double range = m_pRateRangeControl ? m_pRateRangeControl->get() : 0.0;
    if (clampEnabled && range > 0.0) {
        // rate parameter 1 = slider top = rate value +1; with rate_dir +1
        // (up = faster) that end is +range. With rate_dir -1 the speed axis
        // is mirrored, so the clamp span flips sign.
        const bool upIsFaster = !m_pRateDirControl || m_pRateDirControl->get() >= 0.0;
        double vLo = m_dRateClampMinPercent / 100.0 / range;
        double vHi = m_dRateClampMaxPercent / 100.0 / range;
        if (!upIsFaster) {
            const double tmp = vLo;
            vLo = -vHi;
            vHi = -tmp;
        }
        vLo = math_clamp(vLo, -1.0, 1.0);
        vHi = math_clamp(vHi, -1.0, 1.0);
        pmin = (vLo + 1.0) / 2.0;
        pmax = (vHi + 1.0) / 2.0;
    }
    m_handler.setParameterWindow(pmin, pmax);
    // The control value is unchanged; only its pixel mapping moved.
    m_handler.refreshPosition(this);
    update();
}

void WSliderComposed::slotExponentToggleChanged(double v) {
    const bool enabled = v != 0.0;
    m_handler.setWarp(enabled ? m_dConfiguredExponent : 1.0, m_dConfiguredCenter);
    // The control value is unchanged; only its pixel mapping moved, so
    // recompute the handle position and repaint.
    m_handler.refreshPosition(this);
    update();
}

void WSliderComposed::setSliderPixmap(const PixmapSource& sourceSlider,
        Paintable::DrawMode drawMode,
        double scaleFactor) {
    m_pSlider = WPixmapStore::getPaintable(sourceSlider, drawMode, scaleFactor);
    if (!m_pSlider) {
        qDebug() << "WSliderComposed: Error loading slider pixmap:" << sourceSlider.getPath();
    } else if (drawMode == Paintable::DrawMode::Fixed) {
        // Set size of widget, using size of slider pixmap
        setFixedSize(m_pSlider->size());
    }
}

void WSliderComposed::setHandlePixmap(
        const PixmapSource& sourceHandle,
        Paintable::DrawMode mode,
        double scaleFactor) {
    m_handler.setHorizontal(m_bHorizontal);
    m_pHandle = WPixmapStore::getPaintable(sourceHandle, mode, scaleFactor);
    m_dHandleLength = calculateHandleLength();
    m_handler.setHandleLength(m_dHandleLength);
    if (!m_pHandle) {
        qDebug() << "WSliderComposed: Error loading handle pixmap:" << sourceHandle.getPath();
    } else {
        // Value is unused in WSliderComposed.
        onConnectedControlChanged(getControlParameter(), 0);
        update();
    }
}

void WSliderComposed::unsetPixmaps() {
    m_pSlider.reset();
    m_pHandle.reset();
}

void WSliderComposed::mouseMoveEvent(QMouseEvent * e) {
    m_handler.mouseMoveEvent(this, e);
}

void WSliderComposed::wheelEvent(QWheelEvent *e) {
    m_handler.wheelEvent(this, e);
}

void WSliderComposed::mouseReleaseEvent(QMouseEvent * e) {
    m_handler.mouseReleaseEvent(this, e);
}

void WSliderComposed::mousePressEvent(QMouseEvent * e) {
    m_handler.mousePressEvent(this, e);
}

void WSliderComposed::mouseDoubleClickEvent(QMouseEvent* e) {
    m_handler.mouseDoubleClickEvent(this, e);
}

void WSliderComposed::paintEvent(QPaintEvent * /*unused*/) {
    QStyleOption option;
    option.initFrom(this);
    QStylePainter p(this);
    p.drawPrimitive(QStyle::PE_Widget, option);

    if (m_pSlider && !m_pSlider->isNull()) {
        m_pSlider->draw(rect(), &p);
    }

    // Draw level bar underneath handle
    if (m_barColor.isValid() && m_dBarWidth > 0.0) {
        drawBar(&p);
    }

    if (m_pHandle && !m_pHandle->isNull()) {
        // Slider position rounded, verify this for HiDPI : bug 1479037
        double drawPos = round(m_handler.parameterToPosition(getControlParameterDisplay()));
        QRectF targetRect;
        if (m_bHorizontal) {
            // The handle's draw mode determines whether it is stretched.
            targetRect = QRectF(drawPos, 0, m_dHandleLength, height());
        } else {
            // The handle's draw mode determines whether it is stretched.
            targetRect = QRectF(0, drawPos, width(), m_dHandleLength);
        }
        m_pHandle->draw(targetRect, &p);
    }
}

void WSliderComposed::drawBar(QPainter* pPainter) {
    double x1;
    double x2;
    double y1;
    double y2;
    double value;

    // Draw bar background
    if (m_dBarBgWidth > 0.0) {
        QPen barBgPen = QPen(m_barBgColor);
        barBgPen.setWidthF(m_dBarBgWidth);
        barBgPen.setCapStyle(m_barPenCap);
        pPainter->setPen(barBgPen);
        QLineF barBg;
        if (m_bHorizontal) {
            barBg = QLineF(m_dBarBgStart, m_dBarAxisPos,
                    width() - m_dBarBgEnd, m_dBarAxisPos);
        } else {
            barBg = QLineF(m_dBarAxisPos, height() - m_dBarBgEnd,
                    m_dBarAxisPos, m_dBarBgStart);
        }
        pPainter->drawLine(barBg);
    }

    QPen barPen = QPen(m_barColor);
    barPen.setWidthF(m_dBarWidth);
    barPen.setCapStyle(m_barPenCap);
    pPainter->setPen(barPen);

    if (m_bHorizontal) {
        // Left to right increases the parameter
        value = getControlParameterDisplay();
        if (m_bBarUnipolar) {
            // draw from the left
            x1 = m_dBarStart;
        } else {
            // draw from center
            x1 = m_dBarStart + (width() - m_dBarStart -m_dBarEnd) / 2;
        }
        x2 = m_dBarStart + value * (width() - m_dBarStart - m_dBarEnd);
        y1 = m_dBarAxisPos;
        y2 = y1;
    } else { // vertical slider
        // Sliders usually increase parameters when moved UP, but pixels
        // are count top to bottom, so we flip the scale
        value = 1.0 - getControlParameterDisplay();
        x1 = m_dBarAxisPos;
        x2 = x1;
        if (m_bBarUnipolar) {
            // draw from bottom
            y1 = height() - m_dBarEnd;
        } else {
            // draw from center
            y1 = m_dBarEnd + (height() - m_dBarStart - m_dBarEnd) / 2;
        }
        y2 = m_dBarStart + value * (height() - m_dBarStart - m_dBarEnd);
    }
    pPainter->drawLine(QLineF(x1, y1, x2, y2));
}

void WSliderComposed::resizeEvent(QResizeEvent* pEvent) {
    Q_UNUSED(pEvent);

    m_dHandleLength = calculateHandleLength();
    m_handler.setHandleLength(m_dHandleLength);
    m_dSliderLength = m_bHorizontal ? width() : height();
    m_handler.setSliderLength(m_dSliderLength);
    m_handler.resizeEvent(this, pEvent);

    // Re-calculate state based on our new width/height.
    onConnectedControlChanged(getControlParameter(), 0);
}

void WSliderComposed::onConnectedControlChanged(double dParameter, double /*dValue*/) {
    m_handler.onConnectedControlChanged(this, dParameter);
}

void WSliderComposed::fillDebugTooltip(QStringList* debug) {
    WWidget::fillDebugTooltip(debug);
    int sliderLength = m_bHorizontal ? width() : height();
    *debug << QString("Horizontal: %1").arg(toDebugString(m_bHorizontal))
           << QString("SliderPosition: %1").arg(
                   m_handler.parameterToPosition(getControlParameterDisplay()))
           << QString("SliderLength: %1").arg(sliderLength)
           << QString("HandleLength: %1").arg(m_dHandleLength);
}

double WSliderComposed::calculateHandleLength() {
    if (m_pHandle) {
        Paintable::DrawMode mode = m_pHandle->drawMode();
        if (m_bHorizontal) {
            // Stretch the pixmap to be the height of the widget.
            if (mode == Paintable::DrawMode::Fixed || mode == Paintable::DrawMode::Stretch ||
                    mode == Paintable::DrawMode::Tile || m_pHandle->height() == 0.0) {
                return m_pHandle->width();
            } else if (mode == Paintable::DrawMode::StretchAspect) {
                const int iHeight = m_pHandle->height();
                if (iHeight == 0) {
                  qDebug() << "WSliderComposed: Invalid height.";
                  return 0.0;
                }
                const qreal aspect =
                  static_cast<qreal>(m_pHandle->width()) / iHeight;
                return aspect * height();
            }
        } else {
            // Stretch the pixmap to be the width of the widget.
            if (mode == Paintable::DrawMode::Fixed || mode == Paintable::DrawMode::Stretch ||
                    mode == Paintable::DrawMode::Tile || m_pHandle->width() == 0.0) {
                return m_pHandle->height();
            } else if (mode == Paintable::DrawMode::StretchAspect) {
                const int iWidth = m_pHandle->width();
                if (iWidth == 0) {
                  qDebug() << "WSliderComposed: Invalid width.";
                  return 0.0;
                }
                const qreal aspect =
                  static_cast<qreal>(m_pHandle->height()) / iWidth;
                return aspect * width();
            }
        }
    }
    return 0;
}

void WSliderComposed::inputActivity() {
    update();
}
