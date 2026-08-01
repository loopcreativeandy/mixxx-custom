#pragma once

#include <QMouseEvent>
#include <QWheelEvent>
#include <QCursor>
#include <QApplication>
#include <QPoint>

#include <cmath>

#include "util/math.h"

template <class T>
class SliderEventHandler {
  public:
    SliderEventHandler()
            : m_dStartHandlePos(0),
              m_dStartMousePos(0),
              m_bRightButtonPressed(false),
              m_dOldParameter(-1.0), // virgin
              m_dPos(0.0),
              m_dHandleLength(0),
              m_dSliderLength(0),
              m_bHorizontal(false),
              m_bDrag(false),
              m_bEventWhileDrag(true) { }

    void setHorizontal(bool horiz) {
        m_bHorizontal = horiz;
    }

    void setHandleLength(double len) {
        m_dHandleLength = len;
    }

    void setSliderLength(double len) {
        m_dSliderLength = len;
    }

    void setEventWhileDrag(bool eventwhile) {
        m_bEventWhileDrag = eventwhile;
    }

    /// Opt-in non-linear / off-centre handle response (used by the tempo
    /// fader). @p exponent > 1 gives fine control near the neutral point and
    /// coarser control toward the ends; @p center is the physical fraction of
    /// the travel (0 = far end, 1 = near end) at which the control's neutral
    /// value (parameter 0.5) sits, so center > 0.5 leaves more travel below
    /// neutral. Defaults (1.0 / 0.5) are an exact identity.
    void setWarp(double exponent, double center) {
        m_dWarpExponent = exponent > 0.0 ? exponent : 1.0;
        m_dWarpCenter = math_clamp(center, 0.05, 0.95);
    }

    /// Recompute the cached handle pixel position after the warp changed, so a
    /// runtime toggle of the response curve moves the handle to the pixel that
    /// now represents the unchanged control value.
    void refreshPosition(T* pWidget) {
        m_dPos = parameterToPosition(pWidget->getControlParameter());
        m_dOldParameter = -1.0;
    }

    void mouseMoveEvent(T* pWidget, QMouseEvent* e) {
        if (!m_bRightButtonPressed) {
            if (m_bHorizontal) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
                m_dPos = e->position().x() - m_dHandleLength / 2;
#else
                m_dPos = e->x() - m_dHandleLength / 2;
#endif
            } else {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
                m_dPos = e->position().y() - m_dHandleLength / 2;
#else
                m_dPos = e->y() - m_dHandleLength / 2;
#endif
            }

            m_dPos = m_dStartHandlePos + (m_dPos - m_dStartMousePos);

            // Clamp to the range [0, sliderLength - m_dHandleLength].
            if (m_dSliderLength - m_dHandleLength > 0.0) {
                m_dPos = math_clamp(m_dPos, 0.0, m_dSliderLength - m_dHandleLength);
            }
            double newParameter = positionToParameter(m_dPos);

            // If we don't change this, then updates might be rejected in
            // onConnectedControlChanged.
            m_dOldParameter = newParameter;

            // Emit valueChanged signal
            if (m_bEventWhileDrag) {
                pWidget->setControlParameter(newParameter);
            }

            // Update display
            pWidget->inputActivity();
        }
    }

    void mousePressEvent(T* pWidget, QMouseEvent* e) {
        if (!m_bEventWhileDrag) {
            m_dStartMousePos = 0;
            m_dStartHandlePos = 0;
            pWidget->mouseMoveEvent(e);
            m_bDrag = true;
        } else {
            if (e->button() == Qt::RightButton) {
                pWidget->resetControlParameter();
                m_bRightButtonPressed = true;
            } else {
                if (m_bHorizontal) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
                    m_dStartMousePos = e->position().x() - m_dHandleLength / 2;
#else
                    m_dStartMousePos = e->x() - m_dHandleLength / 2;
#endif
                } else {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
                    m_dStartMousePos = e->position().y() - m_dHandleLength / 2;
#else
                    m_dStartMousePos = e->y() - m_dHandleLength / 2;
#endif
                }
                m_dStartHandlePos = m_dPos;
            }
        }
    }

    void mouseDoubleClickEvent(T* pWidget, QMouseEvent* e) {
        if (e->button() == Qt::LeftButton) {
            pWidget->resetControlParameter();
        }
    }

    void mouseReleaseEvent(T* pWidget, QMouseEvent* e) {
        if (!m_bEventWhileDrag) {
            pWidget->mouseMoveEvent(e);
            m_bDrag = false;
        }
        if (e->button() == Qt::RightButton) {
            m_bRightButtonPressed = false;
        } else {
            pWidget->setControlParameter(m_dOldParameter);
        }
    }

    void wheelEvent(T* pWidget, QWheelEvent* e) {
        // For legacy (MIDI) reasons this is tuned to 127.
        double wheelAdjustment = (e)->angleDelta().y() / (120.0 * 127.0);
        double newParameter = pWidget->getControlParameter() + wheelAdjustment;

        // Clamp to [0.0, 1.0]
        newParameter = math_clamp(newParameter, 0.0, 1.0);

        pWidget->setControlParameter(newParameter);
        onConnectedControlChanged(pWidget, newParameter);
        pWidget->inputActivity();
        e->accept();
    }

    void onConnectedControlChanged(T* pWidget, double dParameter) {
        // WARNING: The second parameter to this method is unused and called with
        // invalid values in parts of WSliderComposed. Do not use it unless you fix
        // this.

        // We don't update slider values while you're dragging them. This way you
        // don't have to "fight" with a controller that is also changing the
        // control.
        if (m_bDrag) {
            return;
        }

        if (m_dOldParameter != dParameter) {
            m_dOldParameter = dParameter;

            double newPos = parameterToPosition(dParameter);

            // Clamp to [0.0, sliderLength - m_dHandleLength].
            if (m_dSliderLength - m_dHandleLength > 0.0) {
                newPos = math_clamp(newPos, 0.0, m_dSliderLength - m_dHandleLength);
            }

            // Check a second time for no-ops. It's possible the parameter changed
            // but the visible pixmap didn't. Only update() the widget if we're
            // really sure we need to since this involves painting ALL of its
            // parents.
            if (newPos != m_dPos) {
                m_dPos = newPos;
                pWidget->update();
            }
        }
    }

    void resizeEvent(T* pWidget, QResizeEvent* pEvent) {
        Q_UNUSED(pEvent);
        // m_dSliderLength and m_dHandleLength are explicitly updated.
        m_dPos = parameterToPosition(pWidget->getControlParameter());
        m_dOldParameter = -1;
    }

    // Convert CO parameter value to a handle pixel position.
    double parameterToPosition(double parameter) const {
        if (m_dSliderLength - m_dHandleLength <= 0.0) {
            return 0.0;
        }
        // Oriented physical fraction of the travel (1 = near end / top).
        double lin = warpInverse(parameter);
        if (!m_bHorizontal) {
            lin = 1.0 - lin;
        }
        return lin * (m_dSliderLength - m_dHandleLength);
    }

    // Convert handle pixel position to a CO parameter value.
    double positionToParameter(double pos) const {
        if (m_dSliderLength - m_dHandleLength <= 0.0) {
            return 0.0;
        }
        double val = pos / (m_dSliderLength - m_dHandleLength);
        double lin = m_bHorizontal ? val : (1.0 - val);
        return warpForward(lin);
    }

    // Map an oriented physical travel fraction (0..1) to a CO parameter (0..1).
    // Symmetric power curve around the (possibly off-centre) neutral point.
    double warpForward(double x) const {
        if (m_dWarpExponent == 1.0 && m_dWarpCenter == 0.5) {
            return x;
        }
        const double c = m_dWarpCenter;
        if (x >= c) {
            const double s = (c < 1.0) ? (x - c) / (1.0 - c) : 0.0;
            return 0.5 + 0.5 * std::pow(math_clamp(s, 0.0, 1.0), m_dWarpExponent);
        }
        const double s = (c > 0.0) ? (c - x) / c : 0.0;
        return 0.5 - 0.5 * std::pow(math_clamp(s, 0.0, 1.0), m_dWarpExponent);
    }

    // Inverse of warpForward: CO parameter (0..1) -> physical fraction (0..1).
    double warpInverse(double y) const {
        if (m_dWarpExponent == 1.0 && m_dWarpCenter == 0.5) {
            return y;
        }
        const double c = m_dWarpCenter;
        if (y >= 0.5) {
            const double s = std::pow(math_clamp(2.0 * (y - 0.5), 0.0, 1.0),
                    1.0 / m_dWarpExponent);
            return c + s * (1.0 - c);
        }
        const double s = std::pow(math_clamp(2.0 * (0.5 - y), 0.0, 1.0),
                1.0 / m_dWarpExponent);
        return c - s * c;
    }

  private:
    // This is the position the handle was when a drag started.
    double m_dStartHandlePos;
    // We record where the mouse was when the user started clicking so they
    // don't need to perfectly grab the slider handle.
    double m_dStartMousePos;
    // True while right mouse button is pressed.
    bool m_bRightButtonPressed;
    // Previous parameter value of the control object, 0 to 1
    double m_dOldParameter;
    // Internal storage of slider position in pixels
    double m_dPos;
    // Length of handle in pixels
    double m_dHandleLength;
    // Length of the slider in pixels
    double m_dSliderLength;
    // True if it's a horizontal slider
    bool m_bHorizontal;
    // True if slider is being dragged. Only used when m_bEventWhileDrag is false
    bool m_bDrag;
    // Is true if events is emitted while the slider is dragged
    bool m_bEventWhileDrag;
    // Non-linear response exponent (1.0 = linear) and neutral-point travel
    // fraction (0.5 = centred). See setWarp().
    double m_dWarpExponent = 1.0;
    double m_dWarpCenter = 0.5;
};
