#pragma once

#include <cmath>

#include "track/beats.h"
#include "waveform/renderers/waveformwidgetrenderer.h"

// Zoomed far out, beat lines crowd into a solid comb. Once they sit closer
// than kMinSpacingPx we only draw every 4th beat (bars), then every 16th
// (phrases), and past that no lines at all. The kept beats are counted from
// the grid's first marker, so they don't hop around while the track scrolls.
// Which beat counts as "the one" is not the musical downbeat; nothing in the
// grid tells us that reliably, and at these zoom levels only the rough
// distance matters.
namespace beatgridthinning {

constexpr double kMinSpacingPx = 6.0;

struct Plan {
    int stride = 1;              // 0 = draw nothing
    long long firstIndex = 0;    // index of the first beat in range
    bool keep(long long index) const {
        if (stride <= 1) {
            return stride == 1;
        }
        return ((index % stride) + stride) % stride == 0;
    }
};

inline Plan plan(const mixxx::BeatsPointer& pBeats,
        WaveformWidgetRenderer* pRenderer,
        mixxx::audio::FramePos startPosition,
        ::WaveformRendererAbstract::PositionSource positionType =
                ::WaveformRendererAbstract::Play) {
    Plan result;
    auto it = pBeats->iteratorFrom(startPosition);
    if (it == pBeats->cend()) {
        return result;
    }
    const mixxx::audio::FramePos first = *it;
    ++it;
    if (it == pBeats->cend()) {
        return result;
    }
    const mixxx::audio::FramePos second = *it;
    const double beatFrames = second - first;
    if (!(beatFrames > 0.0)) {
        return result;
    }
    const double pxPerBeat = std::abs(
            pRenderer->transformSamplePositionInRendererWorld(
                    second.toEngineSamplePos(), positionType) -
            pRenderer->transformSamplePositionInRendererWorld(
                    first.toEngineSamplePos(), positionType));
    if (pxPerBeat >= kMinSpacingPx) {
        return result;
    }
    if (pxPerBeat * 4 >= kMinSpacingPx) {
        result.stride = 4;
    } else if (pxPerBeat * 16 >= kMinSpacingPx) {
        result.stride = 16;
    } else {
        result.stride = 0;
        return result;
    }
    const mixxx::audio::FramePos anchor = *pBeats->cfirstmarker();
    result.firstIndex = std::llround((first - anchor) / beatFrames);
    return result;
}

} // namespace beatgridthinning
