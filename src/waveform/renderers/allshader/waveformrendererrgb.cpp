#include "waveform/renderers/allshader/waveformrendererrgb.h"

#include "rendergraph/material/rgbmaterial.h"
#include "rendergraph/vertexupdaters/rgbvertexupdater.h"
#include "track/track.h"
#include "util/colorcomponents.h"
#include "util/math.h"
#include "waveform/renderers/waveformwidgetrenderer.h"
#include "waveform/waveform.h"
#include "waveform/waveformwidgetfactory.h"

using namespace rendergraph;

namespace {
// andy-custom CP84: the flat grey the "ghost" backdrop is drawn in. The RGB
// material has no alpha channel (see rgb.frag, which writes alpha 1.0), so the
// dimming has to be baked into the colour rather than done with opacity.
constexpr float kEqGhostGrey = 0.28f;
} // namespace

namespace allshader {

WaveformRendererRGB::WaveformRendererRGB(WaveformWidgetRenderer* waveformWidget,
        ::WaveformRendererAbstract::PositionSource type,
        WaveformRendererSignalBase::Options options)
        : WaveformRendererSignalBase(waveformWidget),
          m_isSlipRenderer(type == ::WaveformRendererAbstract::Slip),
          m_options(options),
          m_eqGhost(true) {
    initForRectangles<RGBMaterial>(0);
    setUsePreprocess(true);
}

bool WaveformRendererRGB::init() {
    if (!::WaveformRendererSignalBase::init()) {
        return false;
    }
#ifndef __SCENEGRAPH__
    auto* pWaveformWidgetFactory = WaveformWidgetFactory::instance();
    setEqGhost(pWaveformWidgetFactory->isEqGhostWaveform());
    connect(pWaveformWidgetFactory,
            &WaveformWidgetFactory::eqGhostWaveformChanged,
            this,
            &WaveformRendererRGB::setEqGhost);
#endif
    return true;
}

void WaveformRendererRGB::setAxesColor(const QColor& axesColor) {
    getRgbF(axesColor, &m_axesColor_r, &m_axesColor_g, &m_axesColor_b, &m_axesColor_a);
}

void WaveformRendererRGB::setLowColor(const QColor& lowColor) {
    getRgbF(lowColor, &m_rgbLowColor_r, &m_rgbLowColor_g, &m_rgbLowColor_b);
}

void WaveformRendererRGB::setMidColor(const QColor& midColor) {
    getRgbF(midColor, &m_rgbMidColor_r, &m_rgbMidColor_g, &m_rgbMidColor_b);
}

void WaveformRendererRGB::setHighColor(const QColor& highColor) {
    getRgbF(highColor, &m_rgbHighColor_r, &m_rgbHighColor_g, &m_rgbHighColor_b);
}

void WaveformRendererRGB::onSetup(const QDomNode&) {
}

void WaveformRendererRGB::preprocess() {
    if (!preprocessInner()) {
        if (geometry().vertexCount() != 0) {
            geometry().allocate(0);
            markDirtyGeometry();
        }
    }
}

bool WaveformRendererRGB::preprocessInner() {
    TrackPointer pTrack = m_waveformRenderer->getTrackInfo();

    if (!pTrack || (m_isSlipRenderer && !m_waveformRenderer->isSlipActive())) {
        return false;
    }

    auto positionType = m_isSlipRenderer ? ::WaveformRendererAbstract::Slip
                                         : ::WaveformRendererAbstract::Play;

    ConstWaveformPointer waveform = pTrack->getWaveform();
    if (waveform.isNull()) {
        return false;
    }

    const int dataSize = waveform->getDataSize();
    if (dataSize <= 1) {
        return false;
    }

    const WaveformData* data = waveform->data();
    if (data == nullptr) {
        return false;
    }
#ifdef __STEM__
    auto stemInfo = pTrack->getStemInfo();
    // If this track is a stem track, skip the rendering
    if (!stemInfo.isEmpty() && waveform->hasStem()) {
        return false;
    }
#endif

    const float devicePixelRatio = m_waveformRenderer->getDevicePixelRatio();
    const int length = static_cast<int>(m_waveformRenderer->getLength());
    const int pixelLength = static_cast<int>(m_waveformRenderer->getLength() * devicePixelRatio);
    const float invDevicePixelRatio = 1.f / devicePixelRatio;
    const float halfPixelSize = 0.5f / devicePixelRatio;

    // See waveformrenderersimple.cpp for a detailed explanation of the frame and index calculation
    const int visualFramesSize = dataSize / 2;
    const double firstVisualFrame =
            m_waveformRenderer->getFirstDisplayedPosition(positionType) * visualFramesSize;
    const double lastVisualFrame =
            m_waveformRenderer->getLastDisplayedPosition(positionType) * visualFramesSize;

    // Represents the # of visual frames per horizontal pixel.
    const double visualIncrementPerPixel =
            (lastVisualFrame - firstVisualFrame) / static_cast<double>(pixelLength);

    // Fixes a sporadic crash caused by a division by zero on waveform initialization
    if (visualIncrementPerPixel == 0.0) {
        return false;
    }

    // Per-band gain from the EQ knobs.
    float allGain = 1.0f;
    float lowGain = 1.0f;
    float midGain = 1.0f;
    float highGain = 1.0f;
    getGains(&allGain, &lowGain, &midGain, &highGain);

    // andy-custom CP84: the ghost shows the waveform as it would look with the
    // EQ knobs at unity, so it is compared against the per-band *visual* gains
    // (a preference, not part of the mix) rather than against 1.0. Equal gains
    // mean the EQ removes nothing and the ghost would be fully covered anyway,
    // so it is skipped entirely - no extra vertices at neutral EQ.
    const float ghostLowGain = static_cast<float>(m_lowVisualGain);
    const float ghostMidGain = static_cast<float>(m_midVisualGain);
    const float ghostHighGain = static_cast<float>(m_highVisualGain);
    const bool drawGhost = m_eqGhost &&
            (lowGain < ghostLowGain || midGain < ghostMidGain ||
                    highGain < ghostHighGain);

    const float breadth = static_cast<float>(m_waveformRenderer->getBreadth());
    const float halfBreadth = breadth / 2.0f;

    const float heightFactorAbs = allGain * halfBreadth / m_maxValue;
    const float heightFactor[2] = {-heightFactorAbs, heightFactorAbs};
    const bool splitLeftRight = m_options & WaveformRendererSignalBase::Option::SplitStereoSignal;

    const float low_r = static_cast<float>(m_rgbLowColor_r);
    const float mid_r = static_cast<float>(m_rgbMidColor_r);
    const float high_r = static_cast<float>(m_rgbHighColor_r);
    const float low_g = static_cast<float>(m_rgbLowColor_g);
    const float mid_g = static_cast<float>(m_rgbMidColor_g);
    const float high_g = static_cast<float>(m_rgbHighColor_g);
    const float low_b = static_cast<float>(m_rgbLowColor_b);
    const float mid_b = static_cast<float>(m_rgbMidColor_b);
    const float high_b = static_cast<float>(m_rgbHighColor_b);

    // Effective visual frame for x
    double xVisualFrame = qRound(firstVisualFrame / visualIncrementPerPixel) *
            visualIncrementPerPixel;

    const int numVerticesPerLine = 6; // 2 triangles

    // Slip renderer only render a single channel, so the vertices count doesn't change
    const int rectanglesPerPixel = splitLeftRight && !m_isSlipRenderer ? 2 : 1;
    const int reserved = numVerticesPerLine *
            ((drawGhost ? 2 : 1) * rectanglesPerPixel * pixelLength + 1);

    geometry().setDrawingMode(Geometry::DrawingMode::Triangles);
    geometry().allocate(reserved);
    markDirtyGeometry();

    RGBVertexUpdater vertexUpdater{geometry().vertexDataAs<Geometry::RGBColoredPoint2D>()};
    vertexUpdater.addRectangle({0.f,
                                       halfBreadth - 0.5f},
            {static_cast<float>(length),
                    m_isSlipRenderer ? halfBreadth : halfBreadth + 0.5f},
            {static_cast<float>(m_axesColor_r),
                    static_cast<float>(m_axesColor_g),
                    static_cast<float>(m_axesColor_b)});

    const double maxSamplingRange = visualIncrementPerPixel / 2.0;

    for (int pos = 0; pos < pixelLength; ++pos) {
        const int visualFrameStart = std::lround(xVisualFrame - maxSamplingRange);
        const int visualFrameStop = std::lround(xVisualFrame + maxSamplingRange);

        const int visualIndexStart = std::max(visualFrameStart * 2, 0);
        const int visualIndexStop =
                std::min(std::max(visualFrameStop, visualFrameStart + 1) * 2, dataSize - 1);

        const float fpos = static_cast<float>(pos) * invDevicePixelRatio;

        // Find the max values for low, mid, high and all in the waveform data.
        // - Max of left and right
        uchar u8maxLow[2]{};
        uchar u8maxMid[2]{};
        uchar u8maxHigh[2]{};
        // - Per channel
        uchar u8maxAllChn[2]{};
        for (int chn = 0; chn < 2; chn++) {
            // In case we don't render individual color per channel, we use only
            // the first field of the arrays to perform signal max
            int signalChn = splitLeftRight ? chn : 0;
            // data is interleaved left / right
            for (int i = visualIndexStart + chn; i < visualIndexStop + chn; i += 2) {
                const WaveformData& waveformData = data[i];

                u8maxLow[signalChn] = math_max(u8maxLow[signalChn], waveformData.filtered.low);
                u8maxMid[signalChn] = math_max(u8maxMid[signalChn], waveformData.filtered.mid);
                u8maxHigh[signalChn] = math_max(u8maxHigh[signalChn], waveformData.filtered.high);
                u8maxAllChn[signalChn] = math_max(
                        u8maxAllChn[signalChn], waveformData.filtered.all);
            }
        }
        float maxAllChn[2]{static_cast<float>(u8maxAllChn[0]), static_cast<float>(u8maxAllChn[1])};

        // In case we don't render individual color per channel, all the
        // signal information is in the first field of each array. If
        // this is the split render, we only render the left channel
        // anyway.
        for (int chn = 0;
                chn < (splitLeftRight && !m_isSlipRenderer ? 2 : 1);
                chn++) {
            // Cast to float
            float maxLowU = static_cast<float>(u8maxLow[chn]);
            float maxMidU = static_cast<float>(u8maxMid[chn]);
            float maxHighU = static_cast<float>(u8maxHigh[chn]);

            // Apply the gains
            float maxLow = maxLowU * lowGain;
            float maxMid = maxMidU * midGain;
            float maxHigh = maxHighU * highGain;

            float allUnscaled = maxLowU + maxMidU + maxHighU;
            float eqGain = 1.0f;
            float ghostEqGain = 1.0f;
            if (allUnscaled > 0.0f) {
                eqGain = (maxLow + maxMid + maxHigh) / allUnscaled;
                ghostEqGain = (maxLowU * ghostLowGain + maxMidU * ghostMidGain +
                                      maxHighU * ghostHighGain) /
                        allUnscaled;
            }

            // Use the gained maxLow, maxMid and maxHigh values to calculate the color components
            float red = maxLow * low_r + maxMid * mid_r + maxHigh * high_r;
            float green = maxLow * low_g + maxMid * mid_g + maxHigh * high_g;
            float blue = maxLow * low_b + maxMid * mid_b + maxHigh * high_b;

            // Normalize the color components using the maximum of the three
            const float maxComponent = math_max3(red, green, blue);
            if (maxComponent == 0.f) {
                // Avoid division by 0
                red = 0.f;
                green = 0.f;
                blue = 0.f;
            } else {
                const float normFactor = 1.f / maxComponent;
                red *= normFactor;
                green *= normFactor;
                blue *= normFactor;
            }

            // Lines are thin rectangles
            const auto addSignalRectangle = [&](float gain,
                                                    float r,
                                                    float g,
                                                    float b) {
                if (!splitLeftRight) {
                    vertexUpdater.addRectangle(
                            {fpos - halfPixelSize,
                                    halfBreadth -
                                            heightFactorAbs * gain *
                                                    maxAllChn[chn]},
                            {fpos + halfPixelSize,
                                    m_isSlipRenderer ? halfBreadth
                                                     : halfBreadth +
                                                    heightFactorAbs * gain *
                                                            maxAllChn[chn]},
                            {r, g, b});
                } else {
                    // note: heightFactor is the same for left and right,
                    // but negative for left (chn 0) and positive for right (chn 1)
                    vertexUpdater.addRectangle({fpos - halfPixelSize,
                                                       halfBreadth},
                            {fpos + halfPixelSize,
                                    halfBreadth + heightFactor[chn] * gain * maxAllChn[chn]},
                            {r, g, b});
                }
            };

            // The ghost goes first so the EQ-scaled signal is painted over it;
            // the fragment shader writes an opaque alpha, so wherever the two
            // overlap only the real waveform shows.
            if (drawGhost) {
                addSignalRectangle(ghostEqGain, kEqGhostGrey, kEqGhostGrey, kEqGhostGrey);
            }
            addSignalRectangle(eqGain, red, green, blue);
        }

        xVisualFrame += visualIncrementPerPixel;
    }

    DEBUG_ASSERT(reserved == vertexUpdater.index());

    markDirtyMaterial();

    return true;
}

} // namespace allshader
