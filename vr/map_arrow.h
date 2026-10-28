#pragma once
// map_arrow.h - [hud] map_direction (flat F2 / F3): an arrow on the game's mini map, at the hero's mark, pointing where the camera looks.
// Pure: no game, no Windows (checked by tools/map_arrow_test.cpp).
//
// The game's mini map is the isometric one: a step (dx, dz) on the ground grid is drawn (dx - dz, (dx + dz) / 2) pixels - the 2:1 diamond. The hero stays in
// the middle of the map's box. The arrow is an arrowhead (a triangle with a notch in its back) cut into the interface layer as rows of 1 pixel: a clear of
// each row's rectangle, in the colour that is opaque in the layer (premultiplied, alpha 0 = nothing of the world shows through).

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace maparrow {

struct Box { int32_t left = 0, top = 0, right = 0, bottom = 0; };

constexpr int kMaxRows = 96;   // an arrow's most rows: it is a few dozen pixels long

// The map's screen way (unit, +y down) of the ground heading (dx, dz). False when there is no heading (both ~0, or not finite).
inline bool ScreenWay(float dx, float dz, float out[2]) {
    if (!std::isfinite(dx) || !std::isfinite(dz)) return false;
    const float sx = dx - dz, sy = 0.5f * (dx + dz);
    const float len = std::sqrt(sx * sx + sy * sy);
    if (len < 1e-4f) return false;
    out[0] = sx / len; out[1] = sy / len;
    return true;
}

// The arrow's rows, one box a pixel row each, inside `clip`: tip at (cx, cy) + way * len / 2, back at - way * len / 2, `len` pixels long and 0.6 len across,
// the back notched by a quarter of it. Returns how many (<= cap).
inline int Rows(float cx, float cy, const float way[2], float len, const Box& clip, Box* out, int cap) {
    if (!(len >= 4.0f) || cap <= 0) return 0;
    const float px = -way[1], py = way[0];   // across
    const float hl = 0.5f * len, hw = 0.3f * len, notch = 0.25f * len;
    // the head as two triangles round the notch: tip, left corner, notch; tip, notch, right corner
    const float tx = cx + way[0] * hl, ty = cy + way[1] * hl;
    const float lx = cx - way[0] * hl + px * hw, ly = cy - way[1] * hl + py * hw;
    const float rx = cx - way[0] * hl - px * hw, ry = cy - way[1] * hl - py * hw;
    const float nx = cx - way[0] * (hl - notch), ny = cy - way[1] * (hl - notch);
    const float tri[2][3][2] = {{{tx, ty}, {lx, ly}, {nx, ny}}, {{tx, ty}, {nx, ny}, {rx, ry}}};
    const float top = std::min({ty, ly, ry, ny}), bottom = std::max({ty, ly, ry, ny});
    int n = 0;
    const int y0 = std::max((int)std::floor(top), (int)clip.top), y1 = std::min((int)std::ceil(bottom), (int)clip.bottom);
    for (int y = y0; y < y1 && n < cap; ++y) {
        const float sy = (float)y + 0.5f;
        float lo = 1e30f, hi = -1e30f;
        for (const auto& t : tri) {
            for (int e = 0; e < 3; ++e) {
                const float ax = t[e][0], ay = t[e][1], bx = t[(e + 1) % 3][0], by = t[(e + 1) % 3][1];
                if ((ay <= sy) == (by <= sy)) continue;   // the edge does not cross this row
                const float x = ax + (sy - ay) / (by - ay) * (bx - ax);
                lo = std::min(lo, x); hi = std::max(hi, x);
            }
        }
        if (lo > hi) continue;
        const int xa = std::max((int)std::floor(lo + 0.5f), (int)clip.left), xb = std::min((int)std::floor(hi + 0.5f), (int)clip.right);
        if (xb > xa) out[n++] = Box{xa, y, xb, y + 1};
    }
    return n;
}

// How long the arrow is on a map box: 6 % of its height, at least 12 px (a few dozen rows - the driver does not like a hundred rects in one clear).
inline float LengthFor(const Box& map) { return std::max(12.0f, 0.06f * (float)(map.bottom - map.top)); }
constexpr int kBatch = 8;   // rects per clear

// The hero's mark is (nearly) in the middle of the box the game draws the map in: measured 2026-10-28 on a 2560 x 1440 layer (box 852 x 480) in F2, where the map is not
// zoomed: 2 px left of the middle and 6 px above it, in parts of the box's HEIGHT so it follows the layer's size and the interface scale. (In F3 the map is drawn smaller
// from its corner - [hud_floor] map_zoom - and the arrow, cut into the map's own texture, goes with it.)
// The arrowhead's weight is behind its middle (the notch fills in on every row, so it is a whole triangle: its centre is 1/6 of its length back from the middle) - the middle is put that far ahead of the mark, so the mark
// stands in the arrow's own centre.
constexpr float kCentroid = 1.0f / 6.0f;
constexpr float kMarkRight = 0.0f, kMarkDown = -0.012f;
inline void MarkOf(const Box& map, float* x, float* y) {
    const float h = (float)(map.bottom - map.top);
    *x = 0.5f * (float)(map.left + map.right) + kMarkRight * h;
    *y = 0.5f * (float)(map.top + map.bottom) + kMarkDown * h;
}

}  // namespace maparrow
