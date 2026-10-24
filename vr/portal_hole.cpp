// portal_hole.cpp - hide / show the portal's black centre by toggling D2RRenderMesh
// triangle and vertex counts (vr/portal_hole.h). Counts are restored in F1 so the
// oval comes back. Granny buffers are never wiped.

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "portal_hole.h"
#include "sigscan.h"

namespace portalhole {
namespace {

std::atomic<int> g_mode{0};
std::atomic<void (*)(const char*)> g_log{nullptr};
std::atomic<int> g_told{0};

// Confirmed on D2R 3.3.0 (93847) for the three solid portalSphere models.
constexpr size_t kOffTri = 0x158, kOffVert = 0x1CC, kOffSub = 0x21C;
constexpr uint32_t kDefTri = 380, kDefVert = 230, kDefSub = 380;
constexpr int kMax = 32;

struct Entry {
    uintptr_t rm = 0;
    uint32_t tri = 0, vert = 0, sub = 0;
};

Entry g_ent[kMax];
int g_n = 0;
SRWLOCK g_lock = SRWLOCK_INIT;

bool ReadMem(void* to, const void* from, size_t n) noexcept {
    if ((uintptr_t)from < 0x10000) return false;
    __try {
        memcpy(to, from, n);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool Write32(uintptr_t at, uint32_t v) noexcept {
    if (at < 0x10000) return false;
    __try {
        *reinterpret_cast<volatile uint32_t*>(at) = v;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool Read32(uintptr_t at, uint32_t* v) noexcept {
    if (at < 0x10000 || !v) return false;
    __try {
        *v = *reinterpret_cast<const volatile uint32_t*>(at);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

template <size_t N>
bool String(const void* from, char (&out)[N]) noexcept {
    out[0] = 0;
    if ((uintptr_t)from < 0x10000) return false;
    for (size_t i = 0; i + 1 < N; ++i) {
        char c = 0;
        if (!ReadMem(&c, (const char*)from + i, 1)) return false;
        out[i] = c;
        if (!c) return i > 0;
        if ((unsigned char)c < 0x20 || (unsigned char)c > 0x7E) return false;
    }
    out[N - 1] = 0;
    return false;
}

bool IsValidPtr(uintptr_t p) noexcept {
    if (p < 0x10000 || p >= 0x0000800000000000ULL) return false;
    uint8_t b = 0;
    return ReadMem(&b, (const void*)p, 1);
}

uintptr_t FindRenderMeshOnStack(const uintptr_t* retSlot) noexcept {
    if ((uintptr_t)retSlot < 0x10000) return 0;
    uintptr_t slots[12]{};
    if (!ReadMem(slots, retSlot - 12, sizeof slots)) return 0;
    for (int i = 0; i < 12; ++i) {
        const uintptr_t cand = slots[i];
        if (!IsValidPtr(cand)) continue;
        for (int j = 0; j < 12; ++j) {
            if (slots[j] == cand + 0x278) return cand;
        }
    }
    return 0;
}

bool ApplyEntry(const Entry& e, bool hide) noexcept {
    if (!e.rm) return false;
    const uint32_t tri = hide ? 0 : e.tri;
    const uint32_t vert = hide ? 0 : e.vert;
    const uint32_t sub = hide ? 0 : e.sub;
    return Write32(e.rm + kOffTri, tri) && Write32(e.rm + kOffVert, vert) && Write32(e.rm + kOffSub, sub);
}

void ApplyAll(bool hide) {
    AcquireSRWLockExclusive(&g_lock);
    int ok = 0, n = g_n;
    for (int i = 0; i < g_n; ++i) {
        if (ApplyEntry(g_ent[i], hide)) ++ok;
    }
    ReleaseSRWLockExclusive(&g_lock);
    if (n > 0) {
        char line[160];
        snprintf(line, sizeof line, "vrcam: portal hole - %s %d/%d remembered centre mesh(es)",
                 hide ? "hid" : "restored", ok, n);
        LogLine(line);
    }
}

// Track this render mesh; when hide is true, zero its draw counts.
bool Remember(uintptr_t rm, bool hide) {
    if (!rm) return false;
    uint32_t tri = 0, vert = 0, sub = 0;
    if (!Read32(rm + kOffTri, &tri) || !Read32(rm + kOffVert, &vert) || !Read32(rm + kOffSub, &sub)) return false;

    AcquireSRWLockExclusive(&g_lock);
    int slot = -1;
    for (int i = 0; i < g_n; ++i) {
        if (g_ent[i].rm == rm) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        if (g_n >= kMax) {
            ReleaseSRWLockExclusive(&g_lock);
            return false;
        }
        slot = g_n++;
        g_ent[slot].rm = rm;
        // If already zeroed by an older build, fall back to the known solid-sphere counts.
        g_ent[slot].tri = tri ? tri : kDefTri;
        g_ent[slot].vert = vert ? vert : kDefVert;
        g_ent[slot].sub = sub ? sub : kDefSub;
    } else if (tri || vert || sub) {
        // Refresh originals while the mesh is visible (e.g. after an area reload).
        if (tri) g_ent[slot].tri = tri;
        if (vert) g_ent[slot].vert = vert;
        if (sub) g_ent[slot].sub = sub;
    }
    const Entry e = g_ent[slot];
    ReleaseSRWLockExclusive(&g_lock);
    return ApplyEntry(e, hide);
}

}  // namespace

void LogLine(const char* s) {
    if (void (*fn)(const char*) = g_log.load(std::memory_order_acquire)) fn(s);
}

bool IsHolePath(const char* path) {
    if (!path || !path[0]) return false;
    char low[512];
    size_t n = 0;
    for (; path[n] && n + 1 < sizeof low; ++n) {
        unsigned char c = (unsigned char)path[n];
        if (c == '\\') c = '/';
        else if (c >= 'A' && c <= 'Z') c = (unsigned char)(c + 32);
        low[n] = (char)c;
    }
    low[n] = 0;
    if (!n || n + 1 >= sizeof low) return false;
    auto has = [&](const char* needle) {
        const size_t k = strlen(needle);
        if (k > n) return false;
        for (size_t i = 0; i + k <= n; ++i)
            if (memcmp(low + i, needle, k) == 0) return true;
        return false;
    };
    if (has("innerflow") || has("lip_crawl") || has("long_hole")) return false;
    return has("portalsphere");
}

void Configure(int mode, void (*log)(const char*)) {
    g_log.store(log, std::memory_order_release);
    const int active = mode > 0 ? kCollapsed : 0;
    const int was = g_mode.exchange(active);
    if (was == active) return;
    LogLine(active ? "vrcam: portal hole on - hide the black centre in flat F2/F3 ([input] portal_face); F1 restores it"
                   : "vrcam: portal hole off - restore the black centre (F1 / isometric, or [input] portal_face=0)");
    ApplyAll(active != 0);
}

int OnModel(const char* model, void* file, void* mesh, const void* retSlotAddr) {
    (void)file;
    (void)mesh;
    char path[512];
    if (!String(model, path) || !IsHolePath(path)) return kBuild;
    const auto* retSlot = reinterpret_cast<const uintptr_t*>(retSlotAddr);
    const uintptr_t rm = FindRenderMeshOnStack(retSlot);
    const bool hide = g_mode.load(std::memory_order_acquire) != 0;
    const bool ok = Remember(rm, hide);
    const int seq = g_told.fetch_add(1);
    if (seq < 16) {
        char line[320];
        snprintf(line, sizeof line, "vrcam: portal hole - %s %s (rm=%p)%s", path,
                 hide ? "hidden" : "tracked for F2/F3", (void*)rm, ok ? "" : " - remember failed");
        LogLine(line);
    }
    return hide ? kCollapsed : kBuild;
}

}  // namespace portalhole
