// Flat first-person body picking. The scan reads the client's monster hash (NPCs live there too)
// and MonStats sizes. It never writes the game. The ray change is applied by d2rcam's ScreenToRay.

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "d2rcam.h"
#include "flat_pick.h"
#include "sigscan.h"
#include "unit_scan.h"

namespace flatpick {
namespace {

constexpr uint64_t kMonStats = 0x975B0;
constexpr uint64_t kUnits = 0x2A23910;
constexpr int kBodyCap = 48;
constexpr int kObjCap = 32;
constexpr int kCap = kBodyCap + kObjCap;
constexpr float kReach = 55.0f;

const uint8_t kSigMonStats[18] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x55, 0x56, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x48, 0x63, 0xF2, 0x0F, 0xB6, 0xE9};

using StatsFn = uintptr_t (*)(uint8_t bank, int cls);
void (*g_log)(const char*) = nullptr;

struct Cap {
    float x = 0, y = 0, z = 0, height = 0, radius = 0;
    uint32_t cls = 0;              // the unit's class (MonStats / Objects row), for the log
};
struct Snap {
    Cap c[kCap]{};
    int nBody = 0;                 // bodies in c[0, nBody)
    int nObj = 0;                  // objects in c[kBodyCap, kBodyCap + nObj)
    bool hero = false;             // heroY below is this frame's look-at height
    float heroY = 0;
};
Snap g_snap[2];
std::atomic<int> g_published{0};
bool g_toldOn = false, g_toldMiss = false;

// The renderer's ground shear (melee_bodies.h kGroundShear): render = (gx + h*k, h, gz + h*k). (k, 1, k) is
// the old isometric camera's line of sight, 30 degrees down along the diagonal.
constexpr float kShear = 1.22474487139f;
constexpr float kIsoLen = 2.0f;            // |(k, 1, k)| = sqrt(2 * 1.5 + 1)
constexpr float kIsoAbove = 100.0f;        // the isometric ray starts this high over the point (and over y = 0)
constexpr float kGroundReach = 200.0f;     // a crosshair ray meeting the floor farther than this is left alone
constexpr float kCentreTol = 0.02f;        // the crosshair pixel: within 2% of the half viewport of the middle

std::atomic<bool> g_iso{false}, g_elev{false};

// [debug] flat_elev counters, written from whatever thread asks for a ray, read and reset by Refresh's log.
constexpr int kCallerSlots = 8;
std::atomic<uintptr_t> g_callerRva[kCallerSlots]{};
std::atomic<uint32_t> g_callerN[kCallerSlots]{}, g_callerCentre[kCallerSlots]{};
std::atomic<uint32_t> g_nFeet{0}, g_nIsoBody{0}, g_nIsoGround{0}, g_nGroundMiss{0};

// The last ray the mouse code was handed (for the click log): 1 isometric through a body, 2 isometric through the floor under the crosshair,
// 3 aimed at feet from the eye, 0 left as it was. gx, gz: the grid place that ray stands for; cls, ux, uz: the unit it was aimed at.
std::atomic<int> g_lastMode{0};
std::atomic<float> g_lastGx{0}, g_lastGz{0}, g_lastUx{0}, g_lastUz{0};
std::atomic<uint32_t> g_lastCls{0};
std::atomic<ULONGLONG> g_lastAt{0};
bool (*g_probe)(bool* hovered, uint32_t* type, uint32_t* cls) = nullptr;   // [debug] flat_elev: the game's own hover, read by vrcam
void NoteRay(int mode, float gx, float gz, const Cap* unit) {
    g_lastMode.store(mode, std::memory_order_relaxed);
    g_lastGx.store(gx, std::memory_order_relaxed);
    g_lastGz.store(gz, std::memory_order_relaxed);
    g_lastUx.store(unit ? unit->x : 0.0f, std::memory_order_relaxed);
    g_lastUz.store(unit ? unit->z : 0.0f, std::memory_order_relaxed);
    g_lastCls.store(unit ? unit->cls : 0xFFFFFFFFu, std::memory_order_relaxed);
    g_lastAt.store(GetTickCount64(), std::memory_order_relaxed);
}

void CountCaller(uintptr_t rva, bool centre) {
    for (int i = 0; i < kCallerSlots; ++i) {
        uintptr_t have = g_callerRva[i].load(std::memory_order_relaxed);
        if (have == 0 && g_callerRva[i].compare_exchange_strong(have, rva)) have = rva;
        if (have == rva) {
            g_callerN[i].fetch_add(1, std::memory_order_relaxed);
            if (centre) g_callerCentre[i].fetch_add(1, std::memory_order_relaxed);
            return;
        }
    }
}

struct Row {
    uint32_t cls = 0xFFFFFFFFu;
    uint8_t bank = 0, fa = 0, fb = 0, h = 0, ok = 0;
};
Row g_rows[512];

bool ReadMem(void* dst, uintptr_t src, size_t n) noexcept {
    if (src < 0x10000 || n == 0 || n > 4096) return false;
    __try {
        memcpy(dst, reinterpret_cast<const void*>(src), n);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool CallStats(uintptr_t fn, uint8_t bank, int cls, uintptr_t* out) noexcept {
    __try {
        *out = reinterpret_cast<StatsFn>(fn)(bank, cls);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool RowOf(uint8_t bank, uint32_t cls, uint8_t* fa, uint8_t* fb, uint8_t* h) {
    const unsigned slot = (cls * 3u + bank) & 511u;
    Row& row = g_rows[slot];
    if (row.ok && row.cls == cls && row.bank == bank) {
        *fa = row.fa;
        *fb = row.fb;
        *h = row.h;
        return true;
    }
    const uintptr_t fn = d2rsig::Addr(kMonStats);
    uintptr_t at = 0;
    uint8_t bytes[16] = {};
    if (!fn || cls > 4095 || bank > 3 || !CallStats(fn, bank, static_cast<int>(cls), &at) || !ReadMem(bytes, at, sizeof bytes)) return false;
    row.cls = cls;
    row.bank = bank;
    row.fa = bytes[8];
    row.fb = bytes[9];
    row.h = bytes[13];
    row.ok = 1;
    *fa = row.fa;
    *fb = row.fb;
    *h = row.h;
    return true;
}

void SizeOf(uint8_t fa, uint8_t fb, uint8_t h, bool known, float* height, float* radius) {
    *height = (known && h >= 20 && h <= 200) ? h * 0.10f : 7.0f;
    const int fp = known ? (fa > fb ? fa : fb) : 0;
    *radius = (fp >= 1 && fp <= 8) ? fp * 0.7f : 1.8f;
    if (*radius < 1.5f) *radius = 1.5f;
    if (*radius > 3.5f) *radius = 3.5f;
}

// Objects.txt ids from 1.13, which D2R kept. Packed byte: low 3 bits are the kind
// (1 door, 2 shrine, 3 chest, 4 portal, 5 waypoint, 6 warp, 7 well / stash / quest),
// high 5 bits are the footprint in subtiles. 0 is scenery, a torch, a barrel.
#include "flat_pick_obj.inc"

float ObjRadius(uint8_t packed) {
    int size = packed >> 3;
    if (size < 1) size = 2;
    float r = size * 0.72f;
    if (r < 1.6f) r = 1.6f;
    if (r > 4.8f) r = 4.8f;
    return r;
}

float ObjHeight(int kind) {
    switch (kind) {
        case 1: return 11.0f;   // a door, clicked on the frame
        case 2: return 8.0f;    // a shrine
        case 3: return 5.5f;    // a chest, urn, rack: low, so a body behind it still wins
        case 4: return 12.0f;   // a portal
        case 5: return 9.0f;    // a waypoint
        case 6: return 8.5f;    // stairs, a trap door, a level entrance
        default: return 7.0f;   // a well, the stash, a quest object
    }
}

// A door, shrine, chest or fixture that has already been used (mode 2) is not a target.
bool ObjClosedOnly(int kind) { return kind == 1 || kind == 2 || kind == 3 || kind == 7; }

// An object's place: the static path's tile, the same point the torch lights use.
bool ObjectPlace(const unitscan::Unit& u, float* x, float* z) {
    const uintptr_t path = unitscan::Field64(u, unitscan::kOffPath);
    uint32_t s[2] = {};
    if (!ReadMem(s, path + 0x10, sizeof s)) return false;
    if (s[0] > 200000u || s[1] > 200000u) return false;
    *x = 2.0f * s[0] + 1.0f;
    *z = 2.0f * s[1] + 1.0f;
    return std::isfinite(*x + *z);
}

// The slot this one takes in [base, base + count), or -1 when the list is full of nearer ones.
int KeepSlot(const Snap& dst, int base, int count, int cap, float d2, float heroX, float heroZ) {
    if (count < cap) return base + count;
    int worst = base;
    float worstD = -1.0f;
    for (int i = 0; i < count; ++i) {
        const float ex = dst.c[base + i].x - heroX, ez = dst.c[base + i].z - heroZ;
        const float ed = ex * ex + ez * ez;
        if (ed > worstD) {
            worstD = ed;
            worst = base + i;
        }
    }
    if (d2 >= worstD) return -1;
    return worst;
}

bool RayCapsule(const float o[3], const float d[3], const Cap& c, float* tHit) {
    const float bot = c.y;
    const float top = c.y + c.height;
    const float ax = o[0] - c.x, az = o[2] - c.z;
    if (ax * ax + az * az <= c.radius * c.radius && o[1] >= bot && o[1] <= top) {
        *tHit = 0.02f;
        return true;
    }
    float best = 1.0e30f;
    bool hit = false;
    auto take = [&](float t) {
        if (t > 1.0e-3f && t < best) {
            best = t;
            hit = true;
        }
    };
    const float A = d[0] * d[0] + d[2] * d[2];
    const float B = 2.0f * (ax * d[0] + az * d[2]);
    const float C = ax * ax + az * az - c.radius * c.radius;
    if (A > 1.0e-8f) {
        const float disc = B * B - 4.0f * A * C;
        if (disc >= 0.0f) {
            const float s = sqrtf(disc);
            const float ts[2] = {(-B - s) / (2.0f * A), (-B + s) / (2.0f * A)};
            for (float t : ts) {
                const float y = o[1] + t * d[1];
                if (y >= bot && y <= top) take(t);
            }
        }
    }
    auto sphere = [&](float sy) {
        const float px = o[0] - c.x, py = o[1] - sy, pz = o[2] - c.z;
        const float bb = px * d[0] + py * d[1] + pz * d[2];
        const float cc = px * px + py * py + pz * pz - c.radius * c.radius;
        const float disc = bb * bb - cc;
        if (disc < 0.0f) return;
        const float s = sqrtf(disc);
        take(-bb - s);
        take(-bb + s);
    };
    sphere(bot);
    sphere(top);
    if (!hit) return false;
    *tHit = best;
    return true;
}

void AimFeet(float o[3], float d[3], const Cap& c) {
    const float v[3] = {c.x - o[0], c.y - o[1], c.z - o[2]};
    const float len = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (!(len > 0.05f)) return;
    d[0] = v[0] / len;
    d[1] = v[1] / len;
    d[2] = v[2] / len;
}

// The isometric ray through render point p: along -(k, 1, k), from high over p and over y = 0. Every point of it
// has the grid place GridPlane(p) = (p.x - p.y * k, p.z - p.y * k).
void IsoRay(float o[3], float d[3], const float p[3]) {
    const float lift = (p[1] < 0.0f ? -p[1] : 0.0f) + kIsoAbove;   // how far up the origin is over p
    o[0] = p[0] + lift * kShear;
    o[1] = p[1] + lift;
    o[2] = p[2] + lift * kShear;
    d[0] = -kShear / kIsoLen;
    d[1] = -1.0f / kIsoLen;
    d[2] = -kShear / kIsoLen;
}

bool AtCentre(const float px[2], float vpW, float vpH) {
    if (!px || !(vpW > 0.0f) || !(vpH > 0.0f)) return false;
    return fabsf(2.0f * px[0] / vpW - 1.0f) <= kCentreTol && fabsf(2.0f * px[1] / vpH - 1.0f) <= kCentreTol;
}

// [debug] flat_elev: is the camera's look-at on the grid (the path's place) or sheared by its height? Once every
// kElevMs: the look-at L, the hero's path place P (the player unit nearest L), r = L - P on the ground and
// L.y * k beside it. r ~ 0: the look-at is on the grid; r ~ (L.y * k, L.y * k): it is the render point.
// Then who asked for rays in that time (the game's return RVAs) and what the adjust did.
constexpr uint64_t kElevMs = 2000;

// [debug] flat_elev: where the nearest bodies and objects are on the screen, and what floor the crosshair says each stands on.
// base0: the capsule's feet as the code first placed them (the grid place at the hero's floor, unsheared); base: sheared by that floor
// (where the test now puts them). The unit's own floor: its upright axis in the render world is the line (gx + h*k, h, gz + h*k); h where
// the crosshair's ray passes nearest to it is the floor the unit stands on if the crosshair is on its feet (miss = how near, units).
void TargetsLog(const Snap& s, float hx, float hy, float hz) {
    float vw = 0, vh = 0, o[3], d[3];
    if (!d2rcam::ViewportNow(&vw, &vh) || !d2rcam::PickRay(vw * 0.5f, vh * 0.5f, o, d)) {
        g_log("flat elev: targets - no camera ray");
        return;
    }
    const float gx = hx - hy * kShear, gz = hz - hy * kShear;
    int idx[4] = {-1, -1, -1, -1};   // the nearest two bodies, the nearest two objects
    float dist[4] = {1e30f, 1e30f, 1e30f, 1e30f};
    auto offer = [&](int i, int slot) {
        const float ex = s.c[i].x - gx, ez = s.c[i].z - gz, e = ex * ex + ez * ez;
        if (e < dist[slot]) {
            dist[slot + 1] = dist[slot];
            idx[slot + 1] = idx[slot];
            dist[slot] = e;
            idx[slot] = i;
        } else if (e < dist[slot + 1]) {
            dist[slot + 1] = e;
            idx[slot + 1] = i;
        }
    };
    for (int i = 0; i < s.nBody; ++i) offer(i, 0);
    for (int i = 0; i < s.nObj; ++i) offer(kBodyCap + i, 2);
    char b[420];
    snprintf(b, sizeof b, "flat elev: targets - crosshair ray from (%.1f, %.1f, %.1f) dir (%.3f, %.3f, %.3f), viewport %.0fx%.0f, hero grid (%.1f, %.1f) floor %.2f",
             o[0], o[1], o[2], d[0], d[1], d[2], vw, vh, gx, gz, hy);
    g_log(b);
    for (int n = 0; n < 4; ++n) {
        if (idx[n] < 0) continue;
        const Cap& c = s.c[idx[n]];
        float x0 = -1, y0 = -1, x1 = -1, y1 = -1, x2 = -1, y2 = -1;
        const float p0[3] = {c.x, hy, c.z};
        const float p1[3] = {c.x + hy * kShear, hy, c.z + hy * kShear};
        const float p2[3] = {p1[0], hy + c.height, p1[2]};
        const bool v0 = d2rcam::WorldToScreen(p0, &x0, &y0), v1 = d2rcam::WorldToScreen(p1, &x1, &y1), v2 = d2rcam::WorldToScreen(p2, &x2, &y2);
        // the axis nearest to the crosshair's ray
        const float u[3] = {kShear, 1.0f, kShear}, a0[3] = {c.x, 0.0f, c.z}, w[3] = {a0[0] - o[0], a0[1] - o[1], a0[2] - o[2]};
        const float a = u[0] * u[0] + u[1] * u[1] + u[2] * u[2], bb = u[0] * d[0] + u[1] * d[1] + u[2] * d[2];
        const float dd = u[0] * w[0] + u[1] * w[1] + u[2] * w[2], e = d[0] * w[0] + d[1] * w[1] + d[2] * w[2];
        const float den = a - bb * bb;
        char axis[120] = "axis parallel to the ray";
        if (den > 1e-3f * a) {
            const float h = (bb * e - dd) / den, t = e + bb * h;
            const float q[3] = {a0[0] + h * u[0] - o[0] - t * d[0], h - o[1] - t * d[1], a0[2] + h * u[2] - o[2] - t * d[2]};
            snprintf(axis, sizeof axis, "crosshair-axis floor h = %.2f (%+.2f over the hero's), miss %.2f", h, h - hy, sqrtf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2]));
        }
        snprintf(b, sizeof b, "flat elev: targets - %s grid (%.1f, %.1f) %.1f away, height %.1f radius %.1f: base0 px (%.0f, %.0f)%s, base px (%.0f, %.0f)%s, top px (%.0f, %.0f)%s; %s",
                 n < 2 ? "body" : "object", c.x, c.z, sqrtf(dist[n]), c.height, c.radius, x0, y0, v0 ? "" : " BEHIND", x1, y1, v1 ? "" : " BEHIND", x2, y2,
                 v2 ? "" : " BEHIND", axis);
        g_log(b);
    }
}

void ElevLog(const Snap& snap, float hx, float hy, float hz, uintptr_t table) {
    static ULONGLONG lastAt = 0;
    const ULONGLONG now = GetTickCount64();
    if (now - lastAt < kElevMs) return;
    lastAt = now;
    if (!g_log) return;
    auto read = [](void* d, uintptr_t s, size_t n) { return ReadMem(d, s, n); };
    // wide: a look-at on a raised floor is sheared away from the grid by L.y * k on each axis (41 units at L.y = 33.7)
    float px = 0, pz = 0, best = 200.0f * 200.0f;
    bool found = false;
    unitscan::EachUnit(read, table, unitscan::kPlayer, [&](uintptr_t, const unitscan::Unit& u) {
        float x, z;
        if (!unitscan::Place(read, u, &x, &z)) return true;
        const float d2 = (x - hx) * (x - hx) + (z - hz) * (z - hz);
        if (d2 < best) {
            best = d2;
            px = x;
            pz = z;
            found = true;
        }
        return true;
    });
    char b[320];
    if (found) {
        const float rx = hx - px, rz = hz - pz, s = hy * kShear;
        const char* verdict = fabsf(hy) < 0.5f ? "floor ~0 here, cannot tell"
                              : (fabsf(rx) < 0.75f && fabsf(rz) < 0.75f) ? "look-at is on the GRID (unsheared)"
                              : (fabsf(rx - s) < 0.75f && fabsf(rz - s) < 0.75f) ? "look-at is the RENDER point (sheared by its height)"
                                                                                : "neither - see the numbers";
        snprintf(b, sizeof b, "flat elev: look-at (%.2f, %.2f, %.2f), hero path (%.2f, %.2f), r = (%+.2f, %+.2f), L.y*k = %+.2f: %s", hx, hy, hz,
                 px, pz, rx, rz, s, verdict);
    } else {
        snprintf(b, sizeof b, "flat elev: look-at (%.2f, %.2f, %.2f), no player unit within 200 of it on the grid", hx, hy, hz);
    }
    g_log(b);
    char c[320];
    int n = snprintf(c, sizeof c, "flat elev: rays asked in %llu s:", kElevMs / 1000);
    bool any = false;
    for (int i = 0; i < kCallerSlots && n > 0 && n < (int)sizeof c; ++i) {
        const uintptr_t rva = g_callerRva[i].load(std::memory_order_relaxed);
        const uint32_t cnt = g_callerN[i].exchange(0, std::memory_order_relaxed), mid = g_callerCentre[i].exchange(0, std::memory_order_relaxed);
        if (!rva || !cnt) continue;
        any = true;
        n += snprintf(c + n, sizeof c - n, " 0x%llX x%u (%u at the middle)", (unsigned long long)rva, cnt, mid);
    }
    if (!any && n > 0 && n < (int)sizeof c) snprintf(c + n, sizeof c - n, " none");
    g_log(c);
    snprintf(b, sizeof b, "flat elev: adjust - aimed at feet %u, isometric to a body %u, isometric to the floor %u, crosshair missed the floor %u (flat_iso_ray %d)",
             g_nFeet.exchange(0), g_nIsoBody.exchange(0), g_nIsoGround.exchange(0), g_nGroundMiss.exchange(0), g_iso.load() ? 1 : 0);
    g_log(b);
    TargetsLog(snap, hx, hy, hz);
}

}  // namespace

void SetLog(void (*fn)(const char*)) { g_log = fn; }

void SetModes(bool isoRay, bool elevLog) {
    if (g_log && g_iso.exchange(isoRay) != isoRay)
        g_log(isoRay ? "flat pick: isometric ray on ([input] flat_iso_ray=1)" : "flat pick: isometric ray off ([input] flat_iso_ray=0)");
    g_iso.store(isoRay);
    g_elev.store(elevLog);
}

void Refresh(bool active, float heroX, float heroY, float heroZ) {
    const int cur = g_published.load(std::memory_order_relaxed);
    Snap& dst = g_snap[1 - cur];
    dst.nBody = 0;
    dst.nObj = 0;
    dst.hero = false;
    if (!active || !std::isfinite(heroX + heroY + heroZ)) {
        g_published.store(1 - cur, std::memory_order_release);
        g_toldOn = false;
        return;
    }
    // LookAt and path Place share one world frame (camera look-at). F4's absolute
    // ground shear (pose is sheared) must NOT be applied here - v15 did, capsules
    // missed the ray, hover/labels never appeared (2026-10-19).
    const uintptr_t table = d2rsig::Addr(kUnits);
    if (!table) {
        g_published.store(1 - cur, std::memory_order_release);
        return;
    }
    dst.hero = true;
    dst.heroY = heroY;
    // The look-at is the RENDER point of the hero (measured 2026-10-24, [debug] flat_elev: L - path place = L.y * k on both axes), the
    // units' places are on the GRID: the reach and the nearest-first order are the hero's grid place's.
    const float gridX = heroX - heroY * kShear, gridZ = heroZ - heroY * kShear;
    auto read = [](void* d, uintptr_t s, size_t n) { return ReadMem(d, s, n); };
    if (active && !d2rsig::Check(kMonStats, kSigMonStats, sizeof kSigMonStats)) {
        if (!g_toldMiss && g_log) {
            g_toldMiss = true;
            g_log("flat pick: MonStats signature missing, monster aim stays off");
        }
    } else if (active) {
        unitscan::EachUnit(read, table, unitscan::kMonster, [&](uintptr_t, const unitscan::Unit& u) {
            const uint32_t mode = unitscan::Field32(u, unitscan::kOffMode);
            if (mode == 0 || mode == 12) return true;
            float x, z;
            if (!unitscan::Place(read, u, &x, &z)) return true;
            const float dx = x - gridX, dz = z - gridZ;
            const float d2 = dx * dx + dz * dz;
            if (d2 > kReach * kReach) return true;
            const int slot = KeepSlot(dst, 0, dst.nBody, kBodyCap, d2, gridX, gridZ);
            if (slot < 0) return true;
            if (slot == dst.nBody) ++dst.nBody;
            uint8_t fa = 0, fb = 0, h = 0;
            const bool known = RowOf(u.b[unitscan::kOffBank], unitscan::Field32(u, unitscan::kOffClass), &fa, &fb, &h);
            Cap& c = dst.c[slot];
            c.x = x;
            c.y = heroY;
            c.z = z;
            SizeOf(fa, fb, h, known, &c.height, &c.radius);
            c.cls = unitscan::Field32(u, unitscan::kOffClass);
            return true;
        });
    }
    unitscan::EachUnit(read, table, 2, [&](uintptr_t, const unitscan::Unit& u) {
        const uint32_t cls = unitscan::Field32(u, unitscan::kOffClass);
        if (cls >= static_cast<uint32_t>(kObjN)) return true;
        const uint8_t packed = kObj[cls];
        const int kind = packed & 7;
        if (!kind) return true;
        const uint32_t mode = unitscan::Field32(u, unitscan::kOffMode);
        if (ObjClosedOnly(kind) && mode > 1) return true;
        float x, z;
        if (!ObjectPlace(u, &x, &z)) return true;
        const float dx = x - gridX, dz = z - gridZ;
        const float d2 = dx * dx + dz * dz;
        if (d2 > kReach * kReach) return true;
        const int slot = KeepSlot(dst, kBodyCap, dst.nObj, kObjCap, d2, gridX, gridZ);
        if (slot < 0) return true;
        if (slot == kBodyCap + dst.nObj) ++dst.nObj;
        Cap& c = dst.c[slot];
        c.x = x;
        c.y = heroY;
        c.z = z;
        c.height = ObjHeight(kind);
        c.radius = ObjRadius(packed);
        c.cls = cls;
        return true;
    });
    g_published.store(1 - cur, std::memory_order_release);
    if (!g_toldOn && (dst.nBody > 0 || dst.nObj > 0) && g_log) {
        g_toldOn = true;
        char b[96];
        snprintf(b, sizeof b, "flat pick: body aim on, %d bodies, %d objects in reach", dst.nBody, dst.nObj);
        g_log(b);
    }
    if (g_elev.load(std::memory_order_relaxed)) ElevLog(dst, heroX, heroY, heroZ, table);
}

void HoverWatch();

void Adjust(float origin[3], float dir[3], const float px[2], float vpW, float vpH, uintptr_t callerRva) {
    if (g_elev.load(std::memory_order_relaxed)) HoverWatch();   // what the ray before this one found
    const Snap& snap = g_snap[g_published.load(std::memory_order_acquire)];
    const bool centre = AtCentre(px, vpW, vpH);
    if (g_elev.load(std::memory_order_relaxed)) CountCaller(callerRva, centre);
    const bool iso = g_iso.load(std::memory_order_relaxed) && snap.hero;
    if (snap.nBody <= 0 && snap.nObj <= 0 && !iso) return;
    float len = sqrtf(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
    if (!(len > 0.5f)) return;
    dir[0] /= len;
    dir[1] /= len;
    dir[2] /= len;
    int best = -1;
    float bestT = 1.0e30f;
    auto consider = [&](int i) {
        float t = 0;
        // the capsule stands where the picture draws the unit: its grid place sheared up by the floor's height
        Cap rc = snap.c[i];
        rc.x += rc.y * kShear;
        rc.z += rc.y * kShear;
        if (RayCapsule(origin, dir, rc, &t) && t < bestT) {
            bestT = t;
            best = i;
        }
    };
    for (int i = 0; i < snap.nBody; ++i) consider(i);
    for (int i = 0; i < snap.nObj; ++i) consider(kBodyCap + i);
    if (best >= 0) {
        const Cap& c = snap.c[best];
        if (!iso) {
            AimFeet(origin, dir, c);
            g_nFeet.fetch_add(1, std::memory_order_relaxed);
            NoteRay(3, c.x, c.z, &c);
            return;
        }
        // The feet as the renderer would draw them on the hero's floor: their grid place is the unit's path place.
        // A body is aimed at the height of the body, not at its feet: the game's hover is built for the isometric picture, where a pixel of
        // a unit's body stands for the ground point `grid - height * k` (both axes), and a unit that floats (Tyrael) has no hover at its
        // feet at all (measured 2026-10-26 with [debug] flat_elev: his hover starts about 2 units behind the path place, along that
        // diagonal, and runs on to 10 and more; other NPCs and monsters have it at the feet too). So the isometric ray goes through the
        // point of the body's axis at the height the crosshair meets it, kept in 25 .. 60 % of the body's height. Objects stay on the feet.
        float up = 0.0f;
        if (best < kBodyCap) {
            const float hitY = origin[1] + bestT * dir[1] - c.y;
            up = std::clamp(hitY, 0.25f * c.height, 0.6f * c.height);
        }
        const float feet[3] = {c.x + c.y * kShear, c.y + up, c.z + c.y * kShear};
        IsoRay(origin, dir, feet);
        g_nIsoBody.fetch_add(1, std::memory_order_relaxed);
        NoteRay(1, c.x - up * kShear, c.z - up * kShear, &c);
        return;
    }
    // No body: only the crosshair's own ray (the game's mouse code asks for the middle in the flat mouse look). Where it
    // meets the hero's floor in the picture, and the isometric ray through that point, so the game walks or casts there.
    if (!iso || !centre) return;
    const float t = dir[1] < -1.0e-4f ? (snap.heroY - origin[1]) / dir[1] : -1.0f;
    const float hit[3] = {origin[0] + t * dir[0], snap.heroY, origin[2] + t * dir[2]};
    const float along = t * sqrtf(dir[0] * dir[0] + dir[2] * dir[2]);
    if (!(t > 0.0f) || !(along <= kGroundReach) || !std::isfinite(hit[0] + hit[2])) {
        g_nGroundMiss.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    IsoRay(origin, dir, hit);
    g_nIsoGround.fetch_add(1, std::memory_order_relaxed);
    NoteRay(2, hit[0] - snap.heroY * kShear, hit[2] - snap.heroY * kShear, nullptr);   // the grid place of the floor point
}

// The last ray's grid point against the nearest body, one line. tag: what asked ("click type 0", "hover"), hovered/htype/hcls: the game's own hover.
void LogLast(const char* tag, bool hovered, uint32_t htype, uint32_t hcls) {
    const ULONGLONG now = GetTickCount64();
    const ULONGLONG age = now - g_lastAt.load(std::memory_order_relaxed);
    const int mode = g_lastMode.load(std::memory_order_relaxed);
    char hv[48] = "none";
    if (hovered) snprintf(hv, sizeof hv, "unit type %u class %u", htype, hcls);
    char b[460];
    if (mode == 0 || age > 1000) {
        snprintf(b, sizeof b, "flat elev: %s, game's hover: %s, no ray of ours in the last second (mode %d, %llu ms)", tag, hv, mode, (unsigned long long)age);
        g_log(b);
        return;
    }
    const float gx = g_lastGx.load(std::memory_order_relaxed), gz = g_lastGz.load(std::memory_order_relaxed);
    const Snap& snap = g_snap[g_published.load(std::memory_order_acquire)];
    int nearest = -1;
    float nd = 1.0e30f;
    for (int i = 0; i < snap.nBody; ++i) {
        const float ex = snap.c[i].x - gx, ez = snap.c[i].z - gz, e = ex * ex + ez * ez;
        if (e < nd) {
            nd = e;
            nearest = i;
        }
    }
    const char* what = mode == 1 ? "isometric ray through a body's feet" : mode == 2 ? "isometric ray through the floor under the crosshair" : "ray aimed at feet from the eye";
    if (nearest >= 0) {
        const Cap& c = snap.c[nearest];
        snprintf(b, sizeof b, "flat elev: %s, game's hover: %s; the ray before it: %s, grid point (%.1f, %.1f); nearest body class %u at (%.1f, %.1f) height %.1f, offset (%+.1f, %+.1f) = %.1f; aimed at class %d",
                 tag, hv, what, gx, gz, c.cls, c.x, c.z, c.height, gx - c.x, gz - c.z, sqrtf(nd), g_lastCls.load(std::memory_order_relaxed) == 0xFFFFFFFFu ? -1 : (int)g_lastCls.load(std::memory_order_relaxed));
    } else {
        snprintf(b, sizeof b, "flat elev: %s, game's hover: %s; the ray before it: %s, grid point (%.1f, %.1f); no body in reach", tag, hv, what, gx, gz);
    }
    g_log(b);
}

// [debug] flat_elev: at a click of the game's mouse code, what its own hover found against the ray it was handed. The measure of the
// game's selection region of a unit: mode 2 (floor) with a hover puts a grid point inside it, none outside; offset = that point
// minus the nearest body's path place (grid units). Mode 1 / 3 aimed at a body's feet.
void LogClick(int type, bool hovered) {
    if (!g_elev.load(std::memory_order_relaxed) || !g_log) return;
    static ULONGLONG lastAt = 0;
    const ULONGLONG now = GetTickCount64();
    if (now - lastAt < 250) return;
    lastAt = now;
    char tag[32];
    snprintf(tag, sizeof tag, "click type %d", type);
    LogLast(tag, hovered, 0, 0);
}

// Without a click: every ray of the mouse code the game's hover is asked (the probe: HoverUnit of the local player, on the mouse code's own
// thread) - it still holds what the ray before this one found. A change of what is under the pointer is told at once, and 4 a second at most.
void SetHoverProbe(bool (*fn)(bool* hovered, uint32_t* type, uint32_t* cls)) { g_probe = fn; }
void HoverWatch() {
    static bool lastHover = false, told = false;
    static uint32_t lastType = 0, lastCls = 0;
    static ULONGLONG lastAt = 0;
    bool hovered = false;
    uint32_t type = 0, cls = 0;
    if (!g_probe || !g_probe(&hovered, &type, &cls)) {
        if (!told && g_log) {
            told = true;
            g_log("flat elev: hover probe - the game's hover cannot be read yet");
        }
        return;
    }
    const ULONGLONG now = GetTickCount64();
    const bool changed = hovered != lastHover || (hovered && (type != lastType || cls != lastCls));
    if (!changed && !(hovered && now - lastAt >= 1000)) return;
    if (now - lastAt < 250) return;
    lastAt = now;
    lastHover = hovered;
    lastType = type;
    lastCls = cls;
    LogLast("hover", hovered, type, cls);
}

bool ElevOn() { return g_elev.load(std::memory_order_relaxed); }

bool MonsterPose(uintptr_t unit, float floorY, float* x, float* y, float* z, float* height) {
    if (!unit || !x || !y || !z || !height || !std::isfinite(floorY)) return false;
    unitscan::Unit u{};
    if (!ReadMem(u.b, unit, sizeof u.b)) return false;
    if (unitscan::Field32(u, unitscan::kOffType) != static_cast<uint32_t>(unitscan::kMonster)) return false;
    const uint32_t mode = unitscan::Field32(u, unitscan::kOffMode);
    if (mode == 0 || mode == 12) return false;
    auto read = [](void* d, uintptr_t s, size_t n) { return ReadMem(d, s, n); };
    float px = 0, pz = 0;
    if (!unitscan::Place(read, u, &px, &pz)) return false;
    uint8_t fa = 0, fb = 0, h = 0;
    const bool known = RowOf(u.b[unitscan::kOffBank], unitscan::Field32(u, unitscan::kOffClass), &fa, &fb, &h);
    float rad = 0;
    SizeOf(fa, fb, h, known, height, &rad);
    *x = px;
    *y = floorY;
    *z = pz;
    return std::isfinite(*x + *y + *z + *height) && *height > 0.5f;
}

uintptr_t NearestMonster(float hx, float hy, float hz, float reach) {
    if (!std::isfinite(hx + hy + hz + reach) || reach < 0.0f) return 0;
    hx -= hy * kShear;   // the look-at is the render point; the units' places are on the grid
    hz -= hy * kShear;
    const uintptr_t table = d2rsig::Addr(kUnits);
    if (!table) return 0;
    auto read = [](void* d, uintptr_t s, size_t n) { return ReadMem(d, s, n); };
    uintptr_t best = 0;
    float bestD2 = reach * reach;
    unitscan::EachUnit(read, table, unitscan::kMonster, [&](uintptr_t at, const unitscan::Unit& u) {
        const uint32_t mode = unitscan::Field32(u, unitscan::kOffMode);
        if (mode == 0 || mode == 12) return true;
        float x = 0, z = 0;
        if (!unitscan::Place(read, u, &x, &z)) return true;
        const float d2 = (x - hx) * (x - hx) + (z - hz) * (z - hz);
        if (d2 <= bestD2) {
            bestD2 = d2;
            best = at;
        }
        return true;
    });
    return best;
}

}  // namespace flatpick
