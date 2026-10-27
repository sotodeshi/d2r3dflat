// roof_sides.cpp - [render] roofs_two_sided (vr/roof_sides.h) in the game: two hooks, the material builder (which material is being
// built, and is it a roof's) and the Granny member lookup it calls (the one answer redirected). Every read of the game's memory is
// guarded; the only write is the lookup's own output to its caller - the pointer to the member's value - never the material itself.
//
// Found by bearstonem/d2r-pcvr (MIT, v0.3.18: vr/roof_materials.cpp); written again here on our signatures.

#include "roof_sides.h"

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <intrin.h>
#include <mutex>
#include <string>
#include <unordered_set>

#include "sigscan.h"

namespace roofsides {
namespace {

// Granny's 64-bit structures are packed to four bytes.
#pragma pack(push, 4)
struct Material { const char* name; int mapCount; const void* maps; const void* texture; const void* type; const void* object; };
struct Definition { int kind; const char* name; const void* reference; int width; int extra[3]; uintptr_t reserved; };
#pragma pack(pop)
struct Member { const Definition* definition; const void* value; };
static_assert(offsetof(Material, type) == 0x1C && offsetof(Material, object) == 0x24 && sizeof(Material) == 0x2C, "Granny material layout");
static_assert(sizeof(Definition) == 0x2C && offsetof(Definition, width) == 0x14, "Granny member definition layout");

using BuildFn = void* (*)(const char* model, void* file, void* mesh, const Material* material, uint32_t flags, uint32_t pass);
using FindFn = bool (*)(const void* type, const void* object, const char* name, Member* out);
BuildFn g_origBuild = nullptr;
FindFn g_origFind = nullptr;
LogFn g_log = nullptr;
WantFn g_want = nullptr;
std::atomic<int (*)(const char*, void*, void*, const void*)> g_watch{nullptr};
std::atomic<bool> g_ready{false};
std::atomic<bool> g_modelLog{false};
std::atomic<unsigned> g_told{0};
thread_local Context* t_current = nullptr;
constexpr int32_t kOne = 1;   // where a redirected answer points: lives as long as the plugin
constexpr int32_t kZero = 0;
constexpr float kTwoF = 2.0f;
constexpr float kZeroF = 0.0f;
const char* const kCutoutMat = "AlphaThresholdMaterial";
const char* const kBlackTex = "data/hd/global/textures/black4x4_alb.texture";
std::atomic<unsigned> g_holeFindTold{0};

bool Read(void* to, const void* from, size_t n) noexcept {
    if ((uintptr_t)from < 0x10000) return false;
    __try { memcpy(to, from, n); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool Write(void* to, const void* from, size_t n) noexcept {
    __try { memcpy(to, from, n); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
// A string of the game's, up to N - 1 characters, never read across into the next 4 KiB page past its end.
template <size_t N> bool String(const char* from, char (&out)[N]) {
    if (!from) return false;
    for (size_t at = 0; at < N;) {
        const uintptr_t address = (uintptr_t)from + at;
        const size_t n = std::min({(size_t)64, N - at, (size_t)(4096 - (address & 4095))});
        if (!Read(out + at, (const void*)address, n)) return false;
        for (size_t i = at; i < at + n; ++i)
            if (!out[i]) return true;
        at += n;
    }
    return false;
}

// [debug] model_log: one line per distinct model + mesh + material, so a model with many materials is not told again and again.
void LogBuild(const char* model, void* mesh, const char* material, int mapCount) {
    static std::mutex lock;
    static std::unordered_set<std::string> seen;
    constexpr size_t kCap = 3000;
    char modelName[256]{}, meshName[96]{}, materialName[96]{};
    const char* meshNamePtr = nullptr;
    if (!String(model, modelName)) return;
    if (Read(&meshNamePtr, mesh, sizeof meshNamePtr)) String(meshNamePtr, meshName);   // GrannyMesh +0x00 is its name (PRZEKAZANIE.md)
    String(material, materialName);
    char line[512];
    snprintf(line, sizeof line, "vrcam: model build - %s | mesh %s | material %s (%d maps)", modelName, meshName, materialName, mapCount);
    std::lock_guard<std::mutex> guard(lock);
    if (seen.size() >= kCap || !seen.insert(line).second) return;
    g_log(line);
}

bool HookFind(const void* type, const void* object, const char* name, Member* out) {
    const bool found = g_origFind(type, object, const_cast<char*>(name), out);
    Context* ctx = t_current;
    if (!ctx || !out || !g_ready.load(std::memory_order_acquire)) return found;
    if (ctx->portalHole) {
        char member[64]{};
        if (!String(name, member)) return found;
        Member result{};
        Definition def{};
        const bool haveDef = found && Read(&result, out, sizeof result) && Read(&def, result.definition, sizeof def);
        if (g_log && g_holeFindTold.fetch_add(1) < 48) {
            int32_t iv = 0;
            float fv = 0.0f;
            char sv[96]{};
            const char* strPtr = nullptr;
            if (haveDef && result.value) {
                Read(&iv, result.value, sizeof iv);
                Read(&fv, result.value, sizeof fv);
                if (def.kind == 8 && Read(&strPtr, result.value, sizeof strPtr)) String(strPtr, sv);
            }
            char line[320];
            snprintf(line, sizeof line, "vrcam: portal hole find - member '%s' found=%d kind=%d width=%d int=%d float=%.3f str='%s'",
                     member, found ? 1 : 0, haveDef ? def.kind : -1, haveDef ? def.width : -1, iv, fv, sv);
            g_log(line);
        }
        if (!haveDef) return found;
        const std::string_view m(member);
        const void* redirect = nullptr;
        if ((m == "AlphaThresholdEnabled" || m == "MaterialThresholdEnabled" || m == "PrimaryAlphaThresholdEnabled" || m == "SecondaryAlphaThresholdEnabled") &&
            (def.kind == 19 || def.kind == 20)) {
            redirect = &kOne;
        } else if ((m == "AlphaThreshold" || m == "BlendThreshold") && def.kind == 10) {
            redirect = &kTwoF;
        } else if (m == "AlphaIntensity" && def.kind == 10) {
            redirect = &kZeroF;
        } else if (m == "VariantMaterial" && def.kind == 8) {
            redirect = &kCutoutMat;
        } else if ((m == "ThresholdTexture" || m == "MaskTexture") && def.kind == 8) {
            redirect = &kBlackTex;
        }
        if (redirect && Write(&out->value, &redirect, sizeof redirect)) ++ctx->changes;
        return found;
    }
    if (!found || ctx->type != type || ctx->object != object) return found;
    char member[64]{};
    Member result{};
    Definition def{};
    int32_t value = -1;
    if (!String(name, member) || !Read(&result, out, sizeof result) || !Read(&def, result.definition, sizeof def) ||
        !Read(&value, result.value, sizeof value) || !Override(ctx, type, object, member, def.kind, def.width, value))
        return found;
    // Only the caller's result: the builder reads its flag through this pointer and makes the double-sided variant itself.
    const void* one = &kOne;
    if (Write(&out->value, &one, sizeof one)) ++ctx->changes;
    return found;
}

void* HookBuild(const char* model, void* file, void* mesh, const Material* material, uint32_t flags, uint32_t pass) {
    int holeAction = 0;
    if (int (*watch)(const char*, void*, void*, const void*) = g_watch.load(std::memory_order_acquire)) {
        holeAction = watch(model, file, mesh, _AddressOfReturnAddress());
    }
    Context scope{};
    Material m{};
    char rawPath[512]{}, rawName[128]{}, path[512]{}, name[128]{};
    std::string_view p, n;
    const bool haveMat = Read(&m, material, sizeof m);
    if (g_modelLog.load(std::memory_order_relaxed) && g_log && haveMat) LogBuild(model, mesh, m.name, m.mapCount);
    if (holeAction == 3 && haveMat) {
        scope.type = m.type;
        scope.object = m.object;
        scope.portalHole = true;
    }
    const bool chosen = !scope.portalHole && g_ready.load(std::memory_order_acquire) && g_want && haveMat && String(model, rawPath) &&
                        String(m.name, rawName) && Canonical(std::string_view(rawPath), path, &p) && Canonical(std::string_view(rawName), name, &n) &&
                        Chosen(g_want(), p, n);
    if (chosen) { scope.type = m.type; scope.object = m.object; }
    Scope guard(t_current, (chosen || scope.portalHole) ? &scope : nullptr);   // a nested build of another material does not inherit this one
    void* result = g_origBuild(model, file, mesh, material, flags, pass);
    if (scope.portalHole && g_log && g_told.fetch_add(1) < 8 && result) {
        uint64_t tail[14]{};
        if (Read(tail, (const char*)result + 0x1AC8, sizeof tail)) {
            char line[384];
            snprintf(line, sizeof line,
                     "vrcam: portal hole result %p (+1AE0=%llX +1AE8=%llX +1AF0=%llX +1AF8=%llX +1B00=%llX +1B08=%llX +1B10=%llX +1B18=%llX +1B20=%llX +1B28=%llX +1B30=%llX)",
                     result,
                     (unsigned long long)tail[3], (unsigned long long)tail[4], (unsigned long long)tail[5],
                     (unsigned long long)tail[6], (unsigned long long)tail[7], (unsigned long long)tail[8],
                     (unsigned long long)tail[9], (unsigned long long)tail[10], (unsigned long long)tail[11],
                     (unsigned long long)tail[12], (unsigned long long)tail[13]);
            g_log(line);
        }
    }
    if (chosen && g_log && g_told.fetch_add(1) < 64) {
        char line[760];
        snprintf(line, sizeof line, "vrcam: roofs two-sided - %s, material %s: %s", path, name,
                 scope.changes ? "double-sided" : "the lookup was not asked (or already double-sided)");
        g_log(line);
    }
    return result;
}

}  // namespace

void WatchModels(int (*fn)(const char*, void*, void*, const void*)) { g_watch.store(fn, std::memory_order_release); }

void LogModels(bool on) { g_modelLog.store(on, std::memory_order_relaxed); }

void Install(LogFn log, WantFn want) {
    if (g_ready.load()) return;
    static bool findIn = false, buildIn = false, toldWait = false;
    g_log = log;
    g_want = want;
    // Once a hook is in, its entry's bytes are ours: the checks of both go first.
    if (!findIn && !buildIn &&
        !(d2rsig::Check(kBuildRva, kBuildSig, sizeof kBuildSig) && d2rsig::Check(kFindRva, kFindSig, sizeof kFindSig) &&
          d2rsig::Check(kBuildCallRva, kBuildCallSig, sizeof kBuildCallSig) && d2rsig::Check(kFlagCallRva, kFlagCallSig, sizeof kFlagCallSig) &&
          d2rsig::Check(kFindResultRva, kFindResultSig, sizeof kFindResultSig))) {
        if (!toldWait && log) { toldWait = true; log("vrcam: roofs two-sided - the material builder is not there yet (or another build): roofs one-sided as the game has them"); }
        return;
    }
    if (!findIn) findIn = d2rsig::Hook(kFindRva, kFindSig, sizeof kFindSig, (void*)&HookFind, (void**)&g_origFind);
    if (!findIn) return;
    if (!buildIn) buildIn = d2rsig::Hook(kBuildRva, kBuildSig, sizeof kBuildSig, (void*)&HookBuild, (void**)&g_origBuild);
    if (!buildIn) return;
    g_ready.store(true, std::memory_order_release);
    if (log) log("vrcam: model materials - the builder's hooks in (roof undersides, and a portal's black centre, for models loaded from now on)");
}

}  // namespace roofsides
