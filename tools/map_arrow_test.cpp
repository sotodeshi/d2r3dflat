// map_arrow_test - vr/map_arrow.h. Build: cl /EHsc /std:c++17 /I vr tools/map_arrow_test.cpp
#include "map_arrow.h"

#include <cstdio>

static int g_fail = 0;
static void Check(bool ok, const char* what) { if (!ok) { ++g_fail; std::printf("FAIL: %s\n", what); } }

int main() {
    using namespace maparrow;
    float w[2];
    // +x on the grid goes right and down on the iso map, +z left and down
    Check(ScreenWay(1, 0, w) && w[0] > 0 && w[1] > 0 && w[0] > w[1], "+x: right and down, flatter than 45 deg");
    Check(ScreenWay(0, 1, w) && w[0] < 0 && w[1] > 0, "+z: left and down");
    Check(ScreenWay(1, 1, w) && std::fabs(w[0]) < 1e-5f && w[1] > 0.99f, "+x +z: straight down");
    Check(ScreenWay(-1, -1, w) && std::fabs(w[0]) < 1e-5f && w[1] < -0.99f, "-x -z: straight up");
    Check(!ScreenWay(0, 0, w) && !ScreenWay(NAN, 1, w), "no heading: false");

    const Box clip{0, 0, 200, 200};
    Box rows[kMaxRows];
    ScreenWay(1, 1, w);   // down
    int n = Rows(100, 100, w, 40, clip, rows, kMaxRows);
    Check(n >= 30 && n <= 42, "a 40 px arrow is ~40 rows");
    bool inside = true; int widest = 0;
    for (int i = 0; i < n; ++i) {
        inside = inside && rows[i].left >= 80 && rows[i].right <= 120 && rows[i].bottom == rows[i].top + 1 && rows[i].top >= 78 && rows[i].top < 122;
        widest = std::max(widest, rows[i].right - rows[i].left);
    }
    Check(inside, "rows stay within the arrow's size round its middle");
    Check(rows[n - 1].top > rows[0].top, "rows go down");
    Check(widest >= 18 && widest <= 26, "0.6 len across");
    // the tip's row is narrower than the base's
    Check(rows[n - 1].right - rows[n - 1].left < widest, "pointing down: the tip (last row) is narrow");

    // clipped to the box
    n = Rows(5, 5, w, 40, Box{0, 0, 10, 10}, rows, kMaxRows);
    bool clipped = n > 0;
    for (int i = 0; i < n; ++i) clipped = clipped && rows[i].left >= 0 && rows[i].right <= 10 && rows[i].top >= 0 && rows[i].bottom <= 10;
    Check(clipped, "clipped to the map's box");
    Check(Rows(100, 100, w, 2, clip, rows, kMaxRows) == 0, "too short: none");
    Check(Rows(100, 100, w, 400, clip, rows, 5) <= 5, "cap respected");
    Check(LengthFor(Box{0, 0, 300, 400}) == 24.0f && LengthFor(Box{0, 0, 10, 10}) == 12.0f, "length");

    // the centre of the rows' mass is where the mark is, to a pixel
    {
        float way2[2]; ScreenWay(1, 1, way2);   // down
        const float len = 60.0f;
        n = Rows(100 + way2[0] * len * kCentroid, 100 + way2[1] * len * kCentroid, way2, len, Box{0, 0, 200, 200}, rows, kMaxRows);
        double sy = 0, a = 0;
        for (int i = 0; i < n; ++i) { const double wdt = rows[i].right - rows[i].left; sy += wdt * (rows[i].top + 0.5); a += wdt; }
        Check(n > 0 && std::fabs(sy / a - 100.0) < 1.0, "the arrow's centre of mass is on the mark");
    }
    float mx, my;
    MarkOf(Box{1707, 52, 2559, 532}, &mx, &my);
    Check(std::fabs(mx - 2133.0f) < 1.5f && std::fabs(my - 286.2f) < 1.5f, "the hero's mark: the 2560x1440 box's middle, 6 px up");

    std::printf(g_fail ? "%d FAILED\n" : "all ok\n", g_fail);
    return g_fail != 0;
}
