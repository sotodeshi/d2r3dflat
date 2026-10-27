// uitrace - one frame of the game's GPU work, through ReShade, to find where the
// world ends and the interface (HUD, panels, text) begins: which render
// targets a frame draws into, in what order, what is cleared to what, what is
// copied where, and which texture is presented. Ctrl + F10 in the game writes
// d2r_vr_uitrace.txt beside d2r_vr.ini.
//
// The order is the order the game RECORDS its commands in (D3D12 records on
// several threads); each line carries its command list, so the lists can be
// read apart. Draws are counted per target, not listed.
//
// Every 8-bit colour target (and the back buffer) is also copied out right
// after the game drew into it - on the game's own command list, where its
// state is known (render target) - and saved as uitex_<id>_<w>x<h>_<fmt>.raw
// beside the trace: 16-byte header (w, h, format, row pitch), then the rows.
//
// Every full-screen draw (6 vertices at most, one instance) gets a line of its own
// with both shaders' hashes, its depth and blend state and the root constant buffers
// bound for it (by register: b10, b13...); their contents, each distinct one once, go
// to d2r_vr_uitrace_cb.txt beside the trace - what a post pass is told about the
// camera (the act 2 town's straight veil, 2026-10-13).

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <cstdio>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <algorithm>
#include <cmath>

#include <reshade.hpp>
#include "game_device_only.h"

namespace uitrace {
namespace {

using namespace reshade::api;

std::atomic<int> g_state{0};   // 0 idle, 1 armed (waits for a present), 2 recording until the next one
std::mutex g_lock;
std::string g_out;
std::string g_cbOut;           // the constant buffers' contents (d2r_vr_uitrace_cb.txt)
std::wstring g_path;
uint32_t g_seq = 0;

struct Pending {
    std::string target;
    uint32_t draws = 0, dispatches = 0, indirect = 0, pipelines = 0, constants = 0, bundles = 0;
    uint64_t pipe = 0;    // the graphics pipeline of the last draw counted here
    resource rts[8] = {};
    uint32_t rtCount = 0;
    bool watch = false;   // one full-size 8-bit colour target: every draw into it is listed with its counts
    bool layer = false;   // a full-size R16G16B16A16_FLOAT target alone (the interface): draws counted per scissor rect
    bool inPass = false;
};
// Colour targets copied out this frame: game texture -> our readback copy.
struct Grab { resource src{}, copy{}; resource_desc desc{}; device* dev = nullptr; };
std::unordered_map<uint64_t, Grab> g_grabs;

bool Grabbable(const resource_desc& d) {
    const uint32_t f = (uint32_t)d.texture.format;
    return d.type == resource_type::texture_2d && (d.usage & resource_usage::render_target) != 0 && d.texture.samples <= 1 &&
           (f == 28 || f == 29 || f == 87 || f == 91 || f == 27 || f == 86 || f == 90 || f == 10 ||   // 10: R16G16B16A16_FLOAT
            // the lighting and its masks too (the act 2 town's straight line across the picture, 2026-10-07):
            // 26 R11G11B10_FLOAT, 61 R8_UNORM, 54 R16_FLOAT, 56 R16_UNORM, 34 R16G16_FLOAT
            f == 26 || f == 61 || f == 54 || f == 56 || f == 34);
}

// [debug] ui_trace_passes: the scene colour (R11G11B10, full size) copied after EVERY pass that drew into it, not only once at the end - one
// copy each, named by the trace line it follows (uipass_<line>_<target>_<w>x<h>_<fmt>.raw): which pass first puts a thing in the picture.
struct PassGrab { resource copy{}; resource_desc desc{}; device* dev = nullptr; uint32_t line = 0; uint64_t handle = 0; };
std::vector<PassGrab> g_passGrabs;
std::atomic<bool> g_eachPass{false};
constexpr size_t kMaxPassGrabs = 96;

// Copies a colour target, now in the render target state, on the list that drew it.
void GrabNow(command_list* cl, resource r, resource_usage state) {
    device* dev = cl->get_device();
    const resource_desc d = dev->get_resource_desc(r);
    if (!Grabbable(d)) return;
    if (g_eachPass.load(std::memory_order_relaxed) && (uint32_t)d.texture.format == 26 && d.texture.width >= 2000 && g_passGrabs.size() < kMaxPassGrabs) {
        PassGrab pg;
        resource_desc rd(d.texture.width, d.texture.height, 1, 1, d.texture.format, 1, memory_heap::readback, resource_usage::copy_dest);
        if (dev->create_resource(rd, nullptr, resource_usage::copy_dest, &pg.copy)) {
            pg.desc = d; pg.dev = dev; pg.handle = r.handle; pg.line = g_seq ? g_seq - 1 : 0;   // the summary line just written
            cl->barrier(r, state, resource_usage::copy_source);
            cl->copy_texture_region(r, 0, nullptr, pg.copy, 0, nullptr, filter_mode::min_mag_mip_point);
            cl->barrier(r, resource_usage::copy_source, state);
            g_passGrabs.push_back(pg);
        }
    }
    Grab& g = g_grabs[r.handle];
    if (!g.copy.handle) {
        resource_desc rd(d.texture.width, d.texture.height, 1, 1, d.texture.format, 1, memory_heap::readback, resource_usage::copy_dest);
        if (!dev->create_resource(rd, nullptr, resource_usage::copy_dest, &g.copy)) { g_grabs.erase(r.handle); return; }
        g.src = r; g.desc = d; g.dev = dev;
    }
    cl->barrier(r, state, resource_usage::copy_source);
    cl->copy_texture_region(r, 0, nullptr, g.copy, 0, nullptr, filter_mode::min_mag_mip_point);
    cl->barrier(r, resource_usage::copy_source, state);
}
std::unordered_map<command_list*, Pending> g_pending;
std::unordered_set<uint64_t> g_described;

const char* FormatName(format f) {
    switch ((uint32_t)f) {
    case 2: return "R32G32B32A32_FLOAT";
    case 10: return "R16G16B16A16_FLOAT";
    case 11: return "R16G16B16A16_UNORM";
    case 24: return "R10G10B10A2_UNORM";
    case 26: return "R11G11B10_FLOAT";
    case 28: return "R8G8B8A8_UNORM";
    case 29: return "R8G8B8A8_UNORM_SRGB";
    case 34: return "R16G16_FLOAT";
    case 40: return "D32_FLOAT";
    case 41: return "R32_FLOAT";
    case 45: return "D24_UNORM_S8_UINT";
    case 54: return "R16_FLOAT";
    case 61: return "R8_UNORM";
    case 87: return "B8G8R8A8_UNORM";
    case 91: return "B8G8R8A8_UNORM_SRGB";
    default: return nullptr;
    }
}

// "#<resource> WxH FORMAT" once per resource, then "#<resource>".
std::string Describe(device* dev, resource r) {
    char b[160];
    if (r.handle == 0) return "(none)";
    if (!g_described.insert(r.handle).second) { snprintf(b, sizeof b, "#%llx", (unsigned long long)r.handle); return b; }
    const resource_desc d = dev->get_resource_desc(r);
    const char* fn = FormatName(d.texture.format);
    char fb[24]; if (!fn) { snprintf(fb, sizeof fb, "fmt%u", (uint32_t)d.texture.format); fn = fb; }
    snprintf(b, sizeof b, "#%llx %ux%u %s%s%s%s", (unsigned long long)r.handle, d.texture.width, d.texture.height, fn,
             (d.usage & resource_usage::render_target) != 0 ? " RT" : "",
             (d.usage & resource_usage::shader_resource) != 0 ? " SRV" : "",
             (d.usage & resource_usage::unordered_access) != 0 ? " UAV" : "");
    return b;
}
std::string View(device* dev, resource_view v) { return v.handle ? Describe(dev, dev->get_resource_from_view(v)) : "(none)"; }

void Line(command_list* cl, const std::string& text) {
    char b[48]; snprintf(b, sizeof b, "%5u cl%04llx ", g_seq++, (unsigned long long)(((uintptr_t)cl >> 4) & 0xFFFF));
    g_out += b; g_out += text; g_out += '\n';
}

uint64_t PsHash(uint64_t pl);   // below

// The draws counted into the target bound before: one line when it changes.
void Flush(command_list* cl) {
    auto it = g_pending.find(cl);
    if (it == g_pending.end()) return;
    const Pending& p = it->second;
    if (p.draws || p.dispatches || p.indirect || p.pipelines || p.bundles) {
        char b[240]; snprintf(b, sizeof b, "    %u draws, %u dispatches, %u indirect, %u pipelines, %u constants, %u bundles, last pipe %llx ps %016llx -> ",
                              p.draws, p.dispatches, p.indirect, p.pipelines, p.constants, p.bundles, (unsigned long long)p.pipe,
                              (unsigned long long)PsHash(p.pipe));
        Line(cl, b + p.target);
    }
    // what was drawn is copied out now, while it is still a render target
    if ((p.draws || p.indirect || p.bundles) && !p.inPass)
        for (uint32_t i = 0; i < p.rtCount; ++i) if (p.rts[i].handle) GrabNow(cl, p.rts[i], resource_usage::render_target);
    g_pending.erase(it);
}

bool Recording() { return g_state.load(std::memory_order_relaxed) == 2; }

bool OnBeginRenderPass(command_list* cl, uint32_t count, const render_pass_render_target_desc* rts, const render_pass_depth_stencil_desc* ds, render_pass_flags) {
    if (!Recording()) return false;
    std::lock_guard<std::mutex> g(g_lock);
    Flush(cl);
    device* dev = cl->get_device();
    std::string t = "begin pass:";
    for (uint32_t i = 0; i < count; ++i) {
        t += " [" + View(dev, rts[i].view);
        if (rts[i].load_op == render_pass_load_op::clear) {
            char b[64]; snprintf(b, sizeof b, " clear %.2f %.2f %.2f %.2f", rts[i].clear_color[0], rts[i].clear_color[1], rts[i].clear_color[2], rts[i].clear_color[3]);
            t += b;
        }
        t += "]";
    }
    if (ds) t += " depth " + View(dev, ds->view);
    Line(cl, t);
    Pending& p = g_pending[cl];
    p.target = t.substr(12);
    p.inPass = true;
    return false;
}

bool OnEndRenderPass(command_list* cl) {
    if (!Recording()) return false;
    std::lock_guard<std::mutex> g(g_lock);
    Flush(cl);
    return false;
}

void OnBindRenderTargets(command_list* cl, uint32_t count, const resource_view* rtvs, resource_view dsv) {
    if (!Recording()) return;
    std::lock_guard<std::mutex> g(g_lock);
    Flush(cl);
    device* dev = cl->get_device();
    std::string t;
    for (uint32_t i = 0; i < count; ++i) t += " [" + View(dev, rtvs[i]) + "]";
    if (dsv.handle) t += " depth " + View(dev, dsv);
    if (t.empty()) t = " (nothing)";
    Line(cl, "bind:" + t);
    Pending& p = g_pending[cl];
    p.target = t;
    p.rtCount = std::min<uint32_t>(count, 8);
    for (uint32_t i = 0; i < p.rtCount; ++i) p.rts[i] = rtvs[i].handle ? dev->get_resource_from_view(rtvs[i]) : resource{};
    p.watch = false;
    p.layer = false;
    if (count == 1 && p.rts[0].handle) {
        const resource_desc d = dev->get_resource_desc(p.rts[0]);
        p.watch = Grabbable(d) && d.texture.width >= 1000 && d.texture.format != format::r16g16b16a16_float;
        p.layer = d.texture.format == format::r16g16b16a16_float && d.texture.width >= 640;
    }
}

// On the interface's layer: the draws so far under the old scissor rect, then the new one.
void OnScissor(command_list* cl, uint32_t, uint32_t count, const rect* rects) {
    if (!Recording() || count == 0) return;
    std::lock_guard<std::mutex> g(g_lock);
    auto it = g_pending.find(cl);
    if (it == g_pending.end() || !it->second.layer) return;
    Pending& p = it->second;
    if (p.draws) {
        char b[64]; snprintf(b, sizeof b, "      %u draws", p.draws);
        Line(cl, b);
        p.draws = 0;
    }
    char b[96]; snprintf(b, sizeof b, "      scissor %d %d %d %d", rects[0].left, rects[0].top, rects[0].right, rects[0].bottom);
    Line(cl, b);
}

// Each graphics pipeline's pixel shader, hashed (FNV-1a 64 of its bytecode) as the game creates it:
// a pipeline's handle changes with every run, its shader does not. [render] skip_shaders (vrcam) lists
// shaders whose draws are skipped for good - the act 2 town's straight veil across the picture
// (2026-10-07). The handles to skip are known as they are created; a bind of one marks its list
// (thread-local: a list records on one thread), and the draws on it are skipped until another bind.
std::mutex g_hashLock;
std::unordered_map<uint64_t, uint64_t> g_psHash;   // pipeline -> its pixel shader's hash
// pipeline -> its vertex shader's hash and its depth / blend state, for the full-screen draws' lines
struct PipeMore { uint64_t vs = 0; char state[40] = {}; };
std::unordered_map<uint64_t, PipeMore> g_pipeMore;
constexpr int kSkipMax = 8;
std::atomic<uint64_t> g_skipHash[kSkipMax];        // the shaders to skip (0 = empty)
std::atomic<uint64_t> g_skipHandle[64];            // pipelines made of them (0 = empty)
std::atomic<int> g_skipHandles{0};
uint64_t Fnv(const void* p, size_t n) {
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < n; ++i) { h ^= ((const uint8_t*)p)[i]; h *= 1099511628211ull; }
    return h;
}
// [debug] dump_shaders: the shaders listed (hash, as Ctrl+F10 prints vs / ps) are written as shader_<hash>.dxbc into g_dumpDir as the game makes
// their pipelines - to read them with fxc /dumpbin. Nothing in the game changes.
std::atomic<uint64_t> g_dumpHash[16];
std::atomic<bool> g_dumpCompute{false};   // [debug] dump_compute
std::wstring g_dumpDir;   // under g_hashLock
bool DumpHash(uint64_t h) {
    if (!h) return false;
    for (int i = 0; i < 16; ++i) if (g_dumpHash[i].load(std::memory_order_relaxed) == h) return true;
    return false;
}
void DumpShader(uint64_t h, const void* code, size_t size, const wchar_t* prefix = L"shader_") {
    std::wstring dir;
    { std::lock_guard<std::mutex> g(g_hashLock); dir = g_dumpDir; }
    if (dir.empty()) return;
    wchar_t name[64];
    swprintf_s(name, L"%s%016llx.dxbc", prefix, (unsigned long long)h);
    HANDLE f = CreateFileW((dir + name).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);   // CREATE_NEW: once
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD wrote = 0;
    WriteFile(f, code, (DWORD)size, &wrote, nullptr);
    CloseHandle(f);
}
bool SkipHash(uint64_t h) {
    if (!h) return false;
    for (int i = 0; i < kSkipMax; ++i) if (g_skipHash[i].load(std::memory_order_relaxed) == h) return true;
    return false;
}
void AddSkipHandle(uint64_t pl) {
    const int n = g_skipHandles.load();
    for (int i = 0; i < n; ++i) if (g_skipHandle[i].load() == pl) return;
    if (n < 64) { g_skipHandle[n].store(pl); g_skipHandles.store(n + 1); }
}
bool SkipHandle(uint64_t pl) {
    const int n = g_skipHandles.load(std::memory_order_relaxed);
    for (int i = 0; i < n; ++i) if (g_skipHandle[i].load(std::memory_order_relaxed) == pl) return true;
    return false;
}
void OnInitPipeline(device*, pipeline_layout, uint32_t count, const pipeline_subobject* subs, pipeline pl) {
    uint64_t ps = 0;
    PipeMore more;
    // depth: z<func><w if written>, s if stencil; blend of target 0: b<src>.<dst>.<op> (ReShade's enums)
    char depth[16] = "z-", blend[24] = "";
    for (uint32_t i = 0; i < count; ++i) {
        if (!subs[i].data) continue;
        switch (subs[i].type) {
        case pipeline_subobject_type::pixel_shader:
        case pipeline_subobject_type::vertex_shader: {
            const shader_desc* sd = (const shader_desc*)subs[i].data;
            if (!sd->code || !sd->code_size) break;
            (subs[i].type == pipeline_subobject_type::pixel_shader ? ps : more.vs) = Fnv(sd->code, sd->code_size);
            if (DumpHash((subs[i].type == pipeline_subobject_type::pixel_shader ? ps : more.vs))) DumpShader((subs[i].type == pipeline_subobject_type::pixel_shader ? ps : more.vs), sd->code, sd->code_size);
            break;
        }
        case pipeline_subobject_type::compute_shader: {   // [debug] dump_compute: every compute shader the game makes (the lighting is one), as compute_<hash>.dxbc
            const shader_desc* sd = (const shader_desc*)subs[i].data;
            if (sd->code && sd->code_size) ps = Fnv(sd->code, sd->code_size);   // [render] skip_shaders may name a compute shader: its dispatches are left out
            if (sd->code && sd->code_size && g_dumpCompute.load(std::memory_order_relaxed)) DumpShader(Fnv(sd->code, sd->code_size), sd->code, sd->code_size, L"compute_");
            break;
        }
        case pipeline_subobject_type::depth_stencil_state: {
            const depth_stencil_desc* d = (const depth_stencil_desc*)subs[i].data;
            if (d->depth_enable) snprintf(depth, sizeof depth, "z%u%s", (uint32_t)d->depth_func, d->depth_write_mask ? "w" : "");
            if (d->stencil_enable) strcat_s(depth, "s");
            break;
        }
        case pipeline_subobject_type::blend_state: {
            const blend_desc* b = (const blend_desc*)subs[i].data;
            if (b->blend_enable[0])
                snprintf(blend, sizeof blend, " b%u.%u.%u", (uint32_t)b->source_color_blend_factor[0], (uint32_t)b->dest_color_blend_factor[0],
                         (uint32_t)b->color_blend_op[0]);
            break;
        }
        default: break;
        }
    }
    snprintf(more.state, sizeof more.state, "%s%s", depth, blend);
    {
        std::lock_guard<std::mutex> g(g_hashLock);
        if (ps) g_psHash[pl.handle] = ps;
        g_pipeMore[pl.handle] = more;
    }
    if (SkipHash(ps)) AddSkipHandle(pl.handle);
}
void OnDestroyPipeline(device*, pipeline pl) {
    std::lock_guard<std::mutex> g(g_hashLock);
    g_psHash.erase(pl.handle);
    g_pipeMore.erase(pl.handle);
}
PipeMore More(uint64_t pl) {
    std::lock_guard<std::mutex> g(g_hashLock);
    auto it = g_pipeMore.find(pl);
    return it == g_pipeMore.end() ? PipeMore{} : it->second;
}

// [render] depth_fix: the depth range of the game's linear depth, made for our camera.
// The game keeps a linear copy of its depth for its post passes - the fog and its height
// fog (ps_atmosphere_apply), the ambient occlusion (assao), the reflections (ppr), water,
// lava, SMAA, the wall fade - as (z - start) / range, from the view constants' word 53
// (b10[53] = range, 1 / range, start). Its camera from above sets 550 .. 1060 world units.
// Ours stands a hero's height over the ground: everything is nearer than 550, every pixel's
// linear depth came out 0 and every post pass put the whole world on one plane 550 units
// away (2026-10-13). On that plane the height fog's 100-unit band became a pale veil with a
// straight edge across the act 2 town, and the depth fog (885 on) a haze in the picture's
// corners. On, each b10 the game binds gets our start and range before the GPU reads it -
// in the game's upload memory, on the list that binds it, as camfix moves the cameras for
// the right eye; its own memory and code are not touched.
std::atomic<bool> g_depthFix{false};
std::atomic<float> g_depthStart{0.0f}, g_depthRange{1000.0f};
std::atomic<uint32_t> g_depthPatched{0};
struct B10Param { std::atomic<uint64_t> layout{0}; std::atomic<uint32_t> param{0}; };
constexpr int kB10Max = 512;
B10Param g_b10[kB10Max];                    // the root parameter that binds b10, by layout (append only)
std::atomic<int> g_b10n{0};
std::mutex g_b10Lock;                       // for the appends
bool IsB10(uint64_t layout, uint32_t param) {
    const int n = g_b10n.load(std::memory_order_acquire);
    for (int i = 0; i < n; ++i)
        if (g_b10[i].layout.load(std::memory_order_relaxed) == layout) return g_b10[i].param.load(std::memory_order_relaxed) == param;
    return false;
}
// word 53 of a b10 buffer: the game's range written over with ours. SEH: the game's memory.
bool WriteRange(float* w, float start, float range) {
    __try {
        const float r = w[0], inv = w[1], st = w[2];
        if (r == range && st == start) return false;                           // done already (a bind before)
        if (!(r > 0.0f && inv > 0.0f && fabsf(r * inv - 1.0f) < 1e-3f && st >= 0.0f)) return false;   // not the range
        w[0] = range; w[1] = 1.0f / range; w[2] = start;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
void FixDepthRange(device* dev, const buffer_range& r) {
    constexpr uint64_t kAt = 53 * 16;
    const resource_desc d = dev->get_resource_desc(r.buffer);
    if (d.type != resource_type::buffer || d.heap == memory_heap::gpu_only || r.offset + kAt + 16 > d.buffer.size) return;
    void* p = nullptr;
    if (!dev->map_buffer_region(r.buffer, r.offset + kAt, 16, map_access::read_write, &p) || !p) return;
    if (WriteRange((float*)p, g_depthStart.load(std::memory_order_relaxed), g_depthRange.load(std::memory_order_relaxed)))
        g_depthPatched.fetch_add(1, std::memory_order_relaxed);
    dev->unmap_buffer_region(r.buffer);
}

// Each pipeline layout's root parameters: what register a root constant buffer view
// (or root constants) at that index binds - 'b' and the register, 0 for the rest.
std::mutex g_layoutLock;
std::unordered_map<uint64_t, std::vector<uint32_t>> g_layouts;   // layout -> per parameter: register + 1 of a CBV / root constants, 0 else
// [debug] ui_trace_srv (below): each layout's descriptor table ranges of textures the pixel shader may read
struct SrvRange { uint32_t param = 0, binding = 0, reg = 0, count = 0; };
std::unordered_map<uint64_t, std::vector<SrvRange>> g_srvRanges;   // layout -> its pixel-visible SRV ranges; under g_layoutLock
void AddSrvRanges(uint32_t param, const pipeline_layout_param& p, std::vector<SrvRange>& out) {
    const bool flags = p.type == pipeline_layout_param_type::descriptor_table_with_flags;
    if (!flags && p.type != pipeline_layout_param_type::descriptor_table) return;
    const uint32_t n = flags ? p.descriptor_table_with_flags.count : p.descriptor_table.count;
    for (uint32_t i = 0; i < n; ++i) {
        const descriptor_range& r = flags ? static_cast<const descriptor_range&>(p.descriptor_table_with_flags.ranges[i]) : p.descriptor_table.ranges[i];
        if (r.type != descriptor_type::shader_resource_view) continue;
        if (r.visibility != shader_stage::all && (r.visibility & shader_stage::pixel) == 0) continue;
        out.push_back({param, r.binding, r.dx_register_index, std::min<uint32_t>(r.count, 64)});   // (unbounded = UINT32_MAX: the first 64)
    }
}
void OnInitPipelineLayout(device*, uint32_t count, const pipeline_layout_param* params, pipeline_layout layout) {
    std::vector<uint32_t> regs(count, 0);
    std::vector<SrvRange> srvs;   // [debug] ui_trace_srv
    for (uint32_t i = 0; i < count; ++i) {
        const pipeline_layout_param& p = params[i];
        AddSrvRanges(i, p, srvs);
        // a root constant buffer view comes as push descriptors, alone or as a range (as ReShade gives the game's)
        const descriptor_range* cbv = nullptr;
        if (p.type == pipeline_layout_param_type::push_constants) regs[i] = p.push_constants.dx_register_index + 1;
        else if (p.type == pipeline_layout_param_type::push_descriptors && p.push_descriptors.type == descriptor_type::constant_buffer)
            cbv = &p.push_descriptors;
        else if ((p.type == pipeline_layout_param_type::push_descriptors_with_ranges ||
                  p.type == pipeline_layout_param_type::push_descriptors_with_ranges_and_flags) &&
                 p.descriptor_table.count && p.descriptor_table.ranges && p.descriptor_table.ranges[0].type == descriptor_type::constant_buffer)
            cbv = &p.descriptor_table.ranges[0];
        if (cbv) regs[i] = cbv->dx_register_index + 1;
        if (cbv && cbv->dx_register_index == 10) {
            std::lock_guard<std::mutex> g(g_b10Lock);
            const int n = g_b10n.load();
            if (n < kB10Max) {
                g_b10[n].layout.store(layout.handle); g_b10[n].param.store(i);
                g_b10n.store(n + 1, std::memory_order_release);
            }
        }
    }
    std::lock_guard<std::mutex> g(g_layoutLock);
    g_layouts[layout.handle] = std::move(regs);
    if (!srvs.empty()) g_srvRanges[layout.handle] = std::move(srvs);
}
void OnDestroyPipelineLayout(device*, pipeline_layout layout) {
    for (int i = 0, n = g_b10n.load(); i < n; ++i)
        if (g_b10[i].layout.load() == layout.handle) g_b10[i].layout.store(0);
    std::lock_guard<std::mutex> g(g_layoutLock);
    g_layouts.erase(layout.handle);
    g_srvRanges.erase(layout.handle);
}
// [debug] ui_trace_srv: which textures a pixel shader of interest reads, by register (t44 = ...). The game's SRVs sit in
// descriptor tables; the tables' contents are followed as the game writes them (Create*View = update_descriptor_tables,
// CopyDescriptors into the shader-visible heaps = copy_descriptor_tables), by heap and slot, and each draw's tables come from
// bind_descriptor_tables. A line per watched shader and distinct set of tables: "srv ps <hash>: t35=#<resource> ...".
// The slot tracking runs on every descriptor write of the game while it is on - a debug aid, registered only with ui_trace.
struct Slot { uint64_t view = 0, res = 0; };
std::mutex g_slotLock;
std::unordered_map<uint64_t, std::unordered_map<uint32_t, Slot>> g_slots;   // heap -> slot -> what was last written there
std::atomic<uint64_t> g_srvWatch[8];
bool SrvWatched(uint64_t ps) {
    if (!ps) return false;
    for (auto& w : g_srvWatch) if (w.load(std::memory_order_relaxed) == ps) return true;
    return false;
}
bool OnUpdateDescriptors(device* dev, uint32_t count, const descriptor_table_update* updates) {
    std::lock_guard<std::mutex> g(g_slotLock);
    for (uint32_t i = 0; i < count; ++i) {
        const descriptor_table_update& u = updates[i];
        if (u.type != descriptor_type::shader_resource_view || !u.descriptors) continue;
        descriptor_heap heap{};
        uint32_t base = 0;
        dev->get_descriptor_heap_offset(u.table, u.binding, u.array_offset, &heap, &base);
        if (!heap.handle) continue;
        auto& slots = g_slots[heap.handle];
        for (uint32_t j = 0; j < u.count; ++j) slots[base + j] = {static_cast<const resource_view*>(u.descriptors)[j].handle, 0};   // (the view is made after this event: resolved when read)
    }
    return false;
}
bool OnCopyDescriptors(device* dev, uint32_t count, const descriptor_table_copy* copies) {
    std::lock_guard<std::mutex> g(g_slotLock);
    for (uint32_t i = 0; i < count; ++i) {
        const descriptor_table_copy& c = copies[i];
        descriptor_heap sh{}, dh{};
        uint32_t so = 0, d0 = 0;
        dev->get_descriptor_heap_offset(c.source_table, c.source_binding, c.source_array_offset, &sh, &so);
        dev->get_descriptor_heap_offset(c.dest_table, c.dest_binding, c.dest_array_offset, &dh, &d0);
        if (!sh.handle || !dh.handle) continue;
        auto src = g_slots.find(sh.handle);
        auto& dst = g_slots[dh.handle];
        for (uint32_t j = 0; j < c.count; ++j) {
            Slot s{};
            if (src != g_slots.end()) {
                auto it = src->second.find(so + j);
                if (it != src->second.end()) s = it->second;
            }
            if (s.view && !s.res) s.res = dev->get_resource_from_view(resource_view{s.view}).handle;   // made by now: what the copy carries
            dst[d0 + j] = s;
        }
    }
    return false;
}
// While recording: the descriptor tables bound on each list, by root parameter (and the layout they were bound with).
struct BoundTables { uint64_t layout = 0; std::unordered_map<uint32_t, uint64_t> tables; };
std::unordered_map<command_list*, BoundTables> g_bound;   // under g_lock
std::unordered_set<uint64_t> g_srvTold;                    // (watched shader, tables) already written this frame; under g_lock
void OnBindDescriptorTables(command_list* cl, shader_stage stages, pipeline_layout layout, uint32_t first, uint32_t count, const descriptor_table* tables) {
    if (!Recording() || (stages & shader_stage::pixel) == 0) return;
    std::lock_guard<std::mutex> g(g_lock);
    BoundTables& b = g_bound[cl];
    if (b.layout != layout.handle) { b.tables.clear(); b.layout = layout.handle; }
    for (uint32_t i = 0; i < count; ++i) b.tables[first + i] = tables[i].handle;
}
// Under g_lock, at a draw with a watched pixel shader: its textures by register.
void SrvLine(command_list* cl, uint64_t ps) {
    auto bt = g_bound.find(cl);
    if (bt == g_bound.end()) return;
    std::vector<SrvRange> ranges;
    {
        std::lock_guard<std::mutex> g(g_layoutLock);
        auto it = g_srvRanges.find(bt->second.layout);
        if (it == g_srvRanges.end()) return;
        ranges = it->second;
    }
    uint64_t key = Fnv(&ps, sizeof ps);
    for (const SrvRange& r : ranges) {
        auto t = bt->second.tables.find(r.param);
        const uint64_t table = t == bt->second.tables.end() ? 0 : t->second;
        key ^= Fnv(&table, sizeof table) + r.param;
    }
    if (!g_srvTold.insert(key).second) return;
    device* dev = cl->get_device();
    char b[64];
    snprintf(b, sizeof b, "      srv ps %016llx:", (unsigned long long)ps);
    std::string text = b;
    for (const SrvRange& r : ranges) {
        auto t = bt->second.tables.find(r.param);
        if (t == bt->second.tables.end()) continue;
        for (uint32_t i = 0; i < r.count; ++i) {
            descriptor_heap heap{};
            uint32_t at = 0;
            dev->get_descriptor_heap_offset(descriptor_table{t->second}, r.binding + i, 0, &heap, &at);
            Slot s{};
            {
                std::lock_guard<std::mutex> g(g_slotLock);
                auto h = g_slots.find(heap.handle);
                if (h != g_slots.end()) { auto it = h->second.find(at); if (it != h->second.end()) s = it->second; }
            }
            if (s.view && !s.res) s.res = dev->get_resource_from_view(resource_view{s.view}).handle;
            // the ground's own maps (t35 colour, t36 normals: 8-bit, thousands of texels a side, written by a compute shader, never a
            // render target) copied out too, once a frame, as they are when the ground reads them: uitex_<id>_..._28.raw
            if (s.res && !g_grabs.count(s.res)) {
                const resource_desc d = dev->get_resource_desc(resource{s.res});
                if (d.type == resource_type::texture_2d && d.texture.format == format::r8g8b8a8_unorm && d.texture.width >= 2048 &&
                    (d.usage & resource_usage::render_target) == 0) {
                    Grab& gr = g_grabs[s.res];
                    resource_desc rd(d.texture.width, d.texture.height, 1, 1, d.texture.format, 1, memory_heap::readback, resource_usage::copy_dest);
                    if (dev->create_resource(rd, nullptr, resource_usage::copy_dest, &gr.copy)) {
                        gr.src = resource{s.res}; gr.desc = d; gr.dev = dev;
                        cl->barrier(gr.src, resource_usage::shader_resource, resource_usage::copy_source);
                        cl->copy_texture_region(gr.src, 0, nullptr, gr.copy, 0, nullptr, filter_mode::min_mag_mip_point);
                        cl->barrier(gr.src, resource_usage::copy_source, resource_usage::shader_resource);
                    } else g_grabs.erase(s.res);
                }
            }
            snprintf(b, sizeof b, " t%u=", r.reg + i);
            text += b;
            text += s.res ? Describe(dev, resource{s.res}) : (heap.handle ? "?" : "(no heap)");
        }
    }
    Line(cl, text);
}

uint32_t RegisterOf(uint64_t layout, uint32_t param) {   // register + 1, 0 = not known
    std::lock_guard<std::mutex> g(g_layoutLock);
    auto it = g_layouts.find(layout);
    return it == g_layouts.end() || param >= it->second.size() ? 0 : it->second[param];
}

// While recording: the root constant buffers and root constants bound on each list so far.
struct RootCb { uint64_t layout = 0; buffer_range range{}; std::vector<uint32_t> values; bool cbv = false; };
std::unordered_map<command_list*, std::unordered_map<uint32_t, RootCb>> g_roots;   // under g_lock
std::unordered_map<uint64_t, int> g_cbSeen;   // a buffer's contents (hashed) -> its number in the cb file; under g_lock
constexpr uint32_t kCbBytes = 1024;            // read of each: 64 float4 (the game's post passes use up to cb10[53])

// A constant buffer's bytes, read where the game wrote them (an upload heap); false if it cannot be mapped.
bool ReadMapped(const void* p, uint8_t* out, uint32_t len) {
    __try { memcpy(out, p, len); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
uint32_t ReadCb(device* dev, const buffer_range& r, uint8_t* out) {
    const resource_desc d = dev->get_resource_desc(r.buffer);
    if (d.type != resource_type::buffer || r.offset >= d.buffer.size) return 0;
    const uint32_t len = (uint32_t)std::min<uint64_t>({kCbBytes, d.buffer.size - r.offset, r.size});
    void* p = nullptr;
    if (!dev->map_buffer_region(r.buffer, r.offset, len, map_access::read_only, &p) || !p) return 0;
    const bool ok = ReadMapped(p, out, len);
    dev->unmap_buffer_region(r.buffer);
    return ok ? len : 0;
}

// [debug] layer_log (with ui_trace): the ground map's compute (775c45982728780a) is told by the CPU where each material layer lies
// (b7: per layer a world -> layer matrix). Those layers move with our camera (2026-10-27: the same two, 30 units apart, moved by the
// same (-3.3, -6.5) between two views). At each of its dispatches every layer's centre (where the layer's u = v = 0) is worked out
// and a line logged whenever one moves, with the eye and the view direction the same dispatch was given (b10 rows 1, 2): the data
// to find what of the camera the offset follows. Nothing is changed.
std::atomic<bool> g_layerLog{false};
void (*g_logFn)(const char*) = nullptr;
struct LayerCbs { buffer_range b7{}, b10{}; bool has7 = false, has10 = false; };
std::mutex g_layerLock;
std::unordered_map<command_list*, LayerCbs> g_layerCbs;
std::unordered_map<uint64_t, std::pair<float, float>> g_layerLast;   // the layer's matrix (its bits) -> its centre last logged
uint32_t g_layerLines = 0;
constexpr uint64_t kGroundCompose = 0x775c45982728780aull;
void LayerNoteCb(command_list* cl, uint64_t layout, uint32_t param, const buffer_range& r) {
    const uint32_t reg = RegisterOf(layout, param);   // register + 1
    if (reg != 8 && reg != 11) return;                // b7, b10
    std::lock_guard<std::mutex> g(g_layerLock);
    LayerCbs& c = g_layerCbs[cl];
    if (reg == 8) { c.b7 = r; c.has7 = true; } else { c.b10 = r; c.has10 = true; }
}
void LayerDispatch(command_list* cl) {
    LayerCbs c;
    {
        std::lock_guard<std::mutex> g(g_layerLock);
        auto it = g_layerCbs.find(cl);
        if (it == g_layerCbs.end()) return;
        c = it->second;
    }
    if (!c.has7 || !c.has10 || !g_logFn) return;
    uint8_t d7[kCbBytes], d10[kCbBytes];
    device* dev = cl->get_device();
    if (ReadCb(dev, c.b7, d7) < 25 * 16 || ReadCb(dev, c.b10, d10) < 3 * 16) return;
    const float* f = reinterpret_cast<const float*>(d7);
    const float* e = reinterpret_cast<const float*>(d10);
    for (int r = 1; r + 3 <= 24; ++r) {   // a layer's matrix: rows r (x), r+1 = (0, 1, 0, 0), r+2 (z), r+3 = translation with w = 1
        const float *m5 = f + r * 4, *m6 = f + (r + 1) * 4, *m7 = f + (r + 2) * 4, *m8 = f + (r + 3) * 4;
        if (m6[1] != 1.0f || m8[3] != 1.0f || (m5[0] == 0.0f && m5[2] == 0.0f)) continue;
        const double a = m5[0], b = m7[0], cc = m5[2], d = m7[2], tu = m8[0], tv = m8[2], det = a * d - b * cc;
        if (std::fabs(det) < 1e-12) continue;
        const float x = (float)((-tu * d + b * tv) / det), z = (float)((-a * tv + cc * tu) / det);
        const uint64_t key = Fnv(m5, 4) ^ (Fnv(m7, 4) << 1) ^ (Fnv(m5 + 2, 4) << 2) ^ (Fnv(m7 + 2, 4) << 3);
        bool log = false;
        {
            std::lock_guard<std::mutex> g(g_layerLock);
            auto [it, fresh] = g_layerLast.try_emplace(key, x, z);
            if (fresh || std::fabs(it->second.first - x) > 0.05f || std::fabs(it->second.second - z) > 0.05f) {
                it->second = {x, z};
                log = g_layerLines < 6000;
                if (log) ++g_layerLines;
            }
        }
        if (log) {
            char b2[256];
            snprintf(b2, sizeof b2, "vrcam: [debug] layer_log t=%llu s=%.5f centre %.3f %.3f | eye %.3f %.3f %.3f fwd %.4f %.4f %.4f",
                     (unsigned long long)GetTickCount64(), std::sqrt(std::fabs(det)), x, z, e[4], e[5], e[6], e[8], e[9], e[10]);
            g_logFn(b2);
        }
        r += 3;
    }
}

void OnPushDescriptors(command_list* cl, shader_stage, pipeline_layout layout, uint32_t param, const descriptor_table_update& u) {
    if (u.type != descriptor_type::constant_buffer || !u.count || !u.descriptors) return;
    if (g_depthFix.load(std::memory_order_relaxed) && IsB10(layout.handle, param))
        FixDepthRange(cl->get_device(), static_cast<const buffer_range*>(u.descriptors)[0]);
    if (g_layerLog.load(std::memory_order_relaxed)) LayerNoteCb(cl, layout.handle, param, static_cast<const buffer_range*>(u.descriptors)[0]);
    if (!Recording()) return;
    std::lock_guard<std::mutex> g(g_lock);
    RootCb& r = g_roots[cl][param];
    r.layout = layout.handle; r.range = static_cast<const buffer_range*>(u.descriptors)[0]; r.cbv = true; r.values.clear();
}

// One full-screen draw: its shaders, state and constants - a line in the trace, the contents in the cb file.
void FullScreen(command_list* cl, uint64_t pipe, uint32_t geometryIndices = 0, bool dispatch = false) {   // under g_lock; geometryIndices: [debug] ui_trace_geometry, a mesh draw; dispatch: [debug] ui_trace_srv, a watched compute shader
    const PipeMore m = More(pipe);
    char b[160];
    if (dispatch) snprintf(b, sizeof b, "      dispatch: cs %016llx, constants", (unsigned long long)PsHash(pipe));
    else if (geometryIndices) snprintf(b, sizeof b, "      geometry %u idx: vs %016llx ps %016llx %s pipe %llx, constants", geometryIndices, (unsigned long long)m.vs, (unsigned long long)PsHash(pipe), m.state, (unsigned long long)pipe);
    else snprintf(b, sizeof b, "      full-screen: vs %016llx ps %016llx %s, constants", (unsigned long long)m.vs, (unsigned long long)PsHash(pipe), m.state);
    std::string t = b;
    auto it = g_roots.find(cl);
    if (it != g_roots.end()) {
        std::vector<std::pair<uint32_t, const RootCb*>> roots(it->second.size());
        size_t n = 0;
        for (const auto& [param, r] : it->second) roots[n++] = {param, &r};
        std::sort(roots.begin(), roots.end(), [](auto& a, auto& c) { return a.first < c.first; });
        for (const auto& [param, r] : roots) {
            const uint32_t reg = RegisterOf(r->layout, param);
            uint8_t data[kCbBytes];
            uint32_t len = 0;
            if (r->cbv) len = ReadCb(cl->get_device(), r->range, data);
            else { len = std::min<uint32_t>((uint32_t)r->values.size() * 4, kCbBytes); memcpy(data, r->values.data(), len); }
            char name[24];
            if (reg) snprintf(name, sizeof name, "%s%u", r->cbv ? "b" : "rc b", reg - 1);
            else snprintf(name, sizeof name, "p%u", param);
            if (!len) { t += std::string(" ") + name + "=?"; continue; }
            const uint64_t h = Fnv(data, len) ^ ((uint64_t)len << 48);
            auto [seen, fresh] = g_cbSeen.emplace(h, (int)g_cbSeen.size());
            snprintf(b, sizeof b, " %s=cb%d", name, seen->second);
            t += b;
            if (!fresh) continue;
            snprintf(b, sizeof b, "cb%d (%s, %u bytes, first seen at %u):\n", seen->second, name, len, g_seq);
            g_cbOut += b;
            const float* f = (const float*)data;
            for (uint32_t k = 0; k + 4 <= len / 4; k += 4) {
                snprintf(b, sizeof b, "  [%2u] %12.6g %12.6g %12.6g %12.6g\n", k / 4, f[k], f[k + 1], f[k + 2], f[k + 3]);
                g_cbOut += b;
            }
        }
    }
    Line(cl, t);
}
uint64_t PsHash(uint64_t pl) {
    std::lock_guard<std::mutex> g(g_hashLock);
    auto it = g_psHash.find(pl);
    return it == g_psHash.end() ? 0 : it->second;
}
struct ListSkip { command_list* cl = nullptr; bool skip = false; };
thread_local ListSkip t_skip;

// [debug] skip_pipeline (vrcam): every draw with this graphics pipeline is skipped - to find by
// elimination which of the game's passes draws something (the act 2 town's line, 2026-10-07).
// Nothing in the game's memory is changed; 0 = off.
std::atomic<uint64_t> g_skipPipe{0};
std::atomic<bool> g_geometry{false};   // [debug] ui_trace_geometry: the trace describes mesh draws too (shaders, state, constants), not only the full-screen ones
std::mutex g_pipeLock;
std::unordered_map<command_list*, uint64_t> g_lastPipe;   // the last graphics pipeline bound on each list
std::unordered_map<command_list*, uint64_t> g_lastCompute;   // the last compute pipeline, while recording
uint64_t LastCompute(command_list* cl) {   // D3D12 binds every pipeline as "all stages": a compute one lands in g_lastPipe
    std::lock_guard<std::mutex> g(g_pipeLock);
    auto it = g_lastCompute.find(cl);
    if (it != g_lastCompute.end()) return it->second;
    auto p = g_lastPipe.find(cl);
    return p == g_lastPipe.end() ? 0 : p->second;
}
uint64_t LastPipe(command_list* cl) {
    std::lock_guard<std::mutex> g(g_pipeLock);
    auto it = g_lastPipe.find(cl);
    return it == g_lastPipe.end() ? 0 : it->second;
}

void OnBindPipeline(command_list* cl, pipeline_stage stages, pipeline pl) {
    if (g_skipHandles.load(std::memory_order_relaxed) &&
        ((stages & pipeline_stage::pixel_shader) != 0 || stages == pipeline_stage::all || stages == pipeline_stage::all_graphics || (stages & pipeline_stage::compute_shader) != 0))
        t_skip = {cl, SkipHandle(pl.handle)};
    // only while looked for: a lock on every bind of every frame otherwise costs the game for nothing
    if ((g_skipPipe.load(std::memory_order_relaxed) || Recording() || g_layerLog.load(std::memory_order_relaxed)) &&
        ((stages & pipeline_stage::pixel_shader) != 0 || stages == pipeline_stage::all || stages == pipeline_stage::all_graphics)) {
        std::lock_guard<std::mutex> g(g_pipeLock);
        g_lastPipe[cl] = pl.handle;
    }
    if (Recording() && (stages & pipeline_stage::compute_shader) != 0 && stages != pipeline_stage::all) {   // [debug] ui_trace_srv: the dispatches' shader
        std::lock_guard<std::mutex> g(g_pipeLock);
        g_lastCompute[cl] = pl.handle;
    }
    if (Recording()) { std::lock_guard<std::mutex> g(g_lock); ++g_pending[cl].pipelines; }
}
void OnPushConstants(command_list* cl, shader_stage, pipeline_layout layout, uint32_t param, uint32_t first, uint32_t count, const void* values) {
    if (!Recording()) return;
    std::lock_guard<std::mutex> g(g_lock);
    ++g_pending[cl].constants;
    if (!values || first + count > kCbBytes / 4) return;
    RootCb& r = g_roots[cl][param];
    if (r.cbv || r.layout != layout.handle) r = RootCb{};
    r.layout = layout.handle;
    if (r.values.size() < first + count) r.values.resize(first + count);
    memcpy(r.values.data() + first, values, count * 4);
}
void OnExecuteSecondary(command_list* cl, command_list* secondary) {
    if (!Recording()) return;
    std::lock_guard<std::mutex> g(g_lock);
    ++g_pending[cl].bundles;
    char b[64]; snprintf(b, sizeof b, "execute bundle / secondary list cl%04llx", (unsigned long long)(((uintptr_t)secondary >> 4) & 0xFFFF));
    Line(cl, b);
}

bool OnClear(command_list* cl, resource_view rtv, const float c[4], uint32_t, const rect*) {
    if (!Recording()) return false;
    std::lock_guard<std::mutex> g(g_lock);
    char b[80]; snprintf(b, sizeof b, " to %.2f %.2f %.2f %.2f", c[0], c[1], c[2], c[3]);
    Line(cl, "clear " + View(cl->get_device(), rtv) + b);
    return false;
}

bool OnDraw(command_list* cl, uint32_t vertices, uint32_t instances, uint32_t, uint32_t) {
    if (t_skip.skip && t_skip.cl == cl) return true;   // [render] skip_shaders
    const uint64_t skip = g_skipPipe.load(std::memory_order_relaxed);
    const uint64_t pipe = (skip || Recording()) ? LastPipe(cl) : 0;
    if (skip && pipe == skip) return true;
    if (!Recording()) return false;
    std::lock_guard<std::mutex> g(g_lock);
    Pending& p = g_pending[cl];
    ++p.draws;
    p.pipe = pipe;
    if (p.watch) { char b[96]; snprintf(b, sizeof b, "      draw %u vertices x %u instances", vertices, instances); Line(cl, b); }
    if (vertices <= 6 && instances == 1) FullScreen(cl, pipe);
    if (const uint64_t ps = PsHash(pipe); SrvWatched(ps)) SrvLine(cl, ps);   // [debug] ui_trace_srv
    return false;
}
bool OnDrawIndexed(command_list* cl, uint32_t indices, uint32_t instances, uint32_t, int32_t, uint32_t) {
    if (t_skip.skip && t_skip.cl == cl) return true;   // [render] skip_shaders
    const uint64_t skip = g_skipPipe.load(std::memory_order_relaxed);
    const uint64_t pipe = (skip || Recording()) ? LastPipe(cl) : 0;
    if (skip && pipe == skip) return true;
    if (!Recording()) return false;
    std::lock_guard<std::mutex> g(g_lock);
    Pending& p = g_pending[cl];
    ++p.draws;
    p.pipe = pipe;
    if (p.watch) { char b[96]; snprintf(b, sizeof b, "      draw indexed %u indices x %u instances", indices, instances); Line(cl, b); }
    if (indices <= 6 && instances == 1) FullScreen(cl, pipe);
    else if (g_geometry.load(std::memory_order_relaxed)) FullScreen(cl, pipe, indices);
    if (const uint64_t ps = PsHash(pipe); SrvWatched(ps)) SrvLine(cl, ps);   // [debug] ui_trace_srv
    return false;
}
bool OnDispatch(command_list* cl, uint32_t, uint32_t, uint32_t) {
    if (t_skip.skip && t_skip.cl == cl) return true;   // [render] skip_shaders: a compute shader named there
    if (g_layerLog.load(std::memory_order_relaxed) && PsHash(LastCompute(cl)) == kGroundCompose) LayerDispatch(cl);   // [debug] layer_log (vrcam's ground_layer_fix ran at the binding: where the layers now are)
    if (Recording()) {
        const uint64_t cs = LastCompute(cl);
        std::lock_guard<std::mutex> g(g_lock);
        ++g_pending[cl].dispatches;
        if (cs && SrvWatched(PsHash(cs))) FullScreen(cl, cs, 0, true);   // [debug] ui_trace_srv: what the CPU told a watched compute shader
    }
    return false;
}
bool OnIndirect(command_list* cl, indirect_command type, resource, uint64_t, uint32_t, uint32_t) {
    if (Recording()) {
        const uint64_t cs = type == indirect_command::dispatch ? LastCompute(cl) : 0;
        std::lock_guard<std::mutex> g(g_lock);
        ++g_pending[cl].indirect;
        if (cs && SrvWatched(PsHash(cs))) FullScreen(cl, cs, 0, true);
    }
    return false;
}

bool OnCopy(command_list* cl, resource src, resource dst) {
    if (!Recording()) return false;
    std::lock_guard<std::mutex> g(g_lock);
    device* dev = cl->get_device();
    Line(cl, "copy " + Describe(dev, src) + " -> " + Describe(dev, dst));
    return false;
}
bool OnCopyRegion(command_list* cl, resource src, uint32_t, const subresource_box*, resource dst, uint32_t, const subresource_box*, filter_mode) {
    return OnCopy(cl, src, dst);
}
bool OnResolve(command_list* cl, resource src, uint32_t, const subresource_box*, resource dst, uint32_t, uint32_t, uint32_t, uint32_t, format) {
    if (!Recording()) return false;
    std::lock_guard<std::mutex> g(g_lock);
    device* dev = cl->get_device();
    Line(cl, "resolve " + Describe(dev, src) + " -> " + Describe(dev, dst));
    return false;
}

// The copies, now that the GPU is done with the frame: one .raw file each.
void SaveGrabs(command_queue* queue) {
    queue->wait_idle();
    std::wstring dir = g_path;
    dir.resize(dir.find_last_of(L'\\') + 1);
    for (auto& [handle, g] : g_grabs) {
        subresource_data data{};
        if (g.dev->map_texture_region(g.copy, 0, nullptr, map_access::read_only, &data) && data.data) {
            wchar_t name[96];
            swprintf_s(name, L"uitex_%llx_%ux%u_%u.raw", (unsigned long long)handle, g.desc.texture.width, g.desc.texture.height, (uint32_t)g.desc.texture.format);
            if (FILE* f = _wfopen((dir + name).c_str(), L"wb")) {
                const uint32_t head[4] = {g.desc.texture.width, g.desc.texture.height, (uint32_t)g.desc.texture.format, data.row_pitch};
                fwrite(head, 4, 4, f);
                for (uint32_t y = 0; y < g.desc.texture.height; ++y) fwrite((const uint8_t*)data.data + (size_t)y * data.row_pitch, 1, data.row_pitch, f);
                fclose(f);
            }
            g.dev->unmap_texture_region(g.copy, 0);
            char b[96]; snprintf(b, sizeof b, "# saved #%llx\n", (unsigned long long)handle);
            g_out += b;
        }
        g.dev->destroy_resource(g.copy);
    }
    g_grabs.clear();
    for (PassGrab& pg : g_passGrabs) {   // [debug] ui_trace_passes
        subresource_data data{};
        if (pg.dev->map_texture_region(pg.copy, 0, nullptr, map_access::read_only, &data) && data.data) {
            wchar_t name[112];
            swprintf_s(name, L"uipass_%05u_%llx_%ux%u_%u.raw", pg.line, (unsigned long long)pg.handle, pg.desc.texture.width, pg.desc.texture.height, (uint32_t)pg.desc.texture.format);
            if (FILE* f = _wfopen((dir + name).c_str(), L"wb")) {
                const uint32_t head[4] = {pg.desc.texture.width, pg.desc.texture.height, (uint32_t)pg.desc.texture.format, data.row_pitch};
                fwrite(head, 4, 4, f);
                for (uint32_t y = 0; y < pg.desc.texture.height; ++y) fwrite((const uint8_t*)data.data + (size_t)y * data.row_pitch, 1, data.row_pitch, f);
                fclose(f);
            }
            pg.dev->unmap_texture_region(pg.copy, 0);
        }
        pg.dev->destroy_resource(pg.copy);
    }
    g_passGrabs.clear();
}

void OnPresent(command_queue* queue, swapchain* sc, const rect*, const rect*, uint32_t, const rect*) {
    const int s = g_state.load();
    if (s == 0) return;
    std::lock_guard<std::mutex> g(g_lock);
    device* dev = sc->get_device();
    const std::string bb = Describe(dev, sc->get_current_back_buffer());
    if (s == 1) {
        g_out = "# one frame of the game's GPU work, as recorded (see uitrace.cpp)\n# present before: back buffer " + bb + "\n";
        g_seq = 0; g_pending.clear(); g_described.clear();
        g_roots.clear(); g_cbSeen.clear(); g_bound.clear(); g_srvTold.clear();
        g_cbOut = "# the root constant buffers of the full-screen draws in d2r_vr_uitrace.txt, each distinct one once (see uitrace.cpp)\n";
        g_state.store(2);
        return;
    }
    for (auto it = g_pending.begin(); it != g_pending.end();) {   // lists closed already: counts only, no copies
        it->second.inPass = true;
        command_list* cl = it->first; ++it; Flush(cl);
    }
    g_out += "# present: back buffer " + bb + "\n";
    {   // the back buffer as presented
        command_list* imm = queue->get_immediate_command_list();
        GrabNow(imm, sc->get_current_back_buffer(), resource_usage::present);
        queue->flush_immediate_command_list();
    }
    SaveGrabs(queue);
    if (FILE* f = _wfopen(g_path.c_str(), L"wb")) { fwrite(g_out.data(), 1, g_out.size(), f); fclose(f); }
    {   // the constants beside it: <trace>_cb.txt
        std::wstring cbPath = g_path;
        const size_t dot = cbPath.find_last_of(L'.');
        cbPath.insert(dot == std::wstring::npos ? cbPath.size() : dot, L"_cb");
        if (FILE* f = _wfopen(cbPath.c_str(), L"wb")) { fwrite(g_cbOut.data(), 1, g_cbOut.size(), f); fclose(f); }
    }
    g_cbOut.clear(); g_roots.clear(); g_cbSeen.clear(); g_bound.clear(); g_srvTold.clear();
    g_out.clear();
    g_state.store(0);
}

}  // namespace

// Every event registered here makes ReShade call it at each such call of the game's - thousands a frame for the draws, the
// pipeline and scissor binds, the push constants - even when the handler returns at once; and with a push_descriptors handler
// ReShade looks up the buffer of every root constant buffer the game binds under its resource lock (2026-10-18, an outside
// profile of mono at ~155 fps: 5.5% of the game's draw thread in that lock alone). So only what is wanted is registered, at
// the plugin's start: the pipelines and layouts always (once each, at creation - the b10 search and the shader hashes);
// the per-call ones only for the Ctrl+F10 trace ([debug] ui_trace), the draws and pipeline binds also for
// [render] skip_shaders / [debug] skip_pipeline, and push_descriptors also for depth_fix when vrcam's own root CBV hook
// (camfix, with the replay's hooks) is not there to fix the depth range before the binding.
bool g_traceOn = false;
void Register(bool trace, bool skip, bool depthViaReShade, bool srv) {
    g_traceOn = trace;
    if (trace && srv) {   // [debug] ui_trace_srv: every descriptor write of the game, from now on
        reshade::register_event<reshade::addon_event::update_descriptor_tables>(&d2rvr::D3D12Only<&OnUpdateDescriptors>::Call);
        reshade::register_event<reshade::addon_event::copy_descriptor_tables>(&d2rvr::D3D12Only<&OnCopyDescriptors>::Call);
        reshade::register_event<reshade::addon_event::bind_descriptor_tables>(&d2rvr::D3D12Only<&OnBindDescriptorTables>::Call);
    }
    reshade::register_event<reshade::addon_event::init_pipeline>(&d2rvr::D3D12Only<&OnInitPipeline>::Call);
    reshade::register_event<reshade::addon_event::destroy_pipeline>(&d2rvr::D3D12Only<&OnDestroyPipeline>::Call);
    reshade::register_event<reshade::addon_event::init_pipeline_layout>(&d2rvr::D3D12Only<&OnInitPipelineLayout>::Call);
    reshade::register_event<reshade::addon_event::destroy_pipeline_layout>(&d2rvr::D3D12Only<&OnDestroyPipelineLayout>::Call);
    if (trace) {
        reshade::register_event<reshade::addon_event::begin_render_pass>(&d2rvr::D3D12Only<&OnBeginRenderPass>::Call);
        reshade::register_event<reshade::addon_event::end_render_pass>(&d2rvr::D3D12Only<&OnEndRenderPass>::Call);
        reshade::register_event<reshade::addon_event::bind_render_targets_and_depth_stencil>(&d2rvr::D3D12Only<&OnBindRenderTargets>::Call);
        reshade::register_event<reshade::addon_event::clear_render_target_view>(&d2rvr::D3D12Only<&OnClear>::Call);
        reshade::register_event<reshade::addon_event::dispatch>(&d2rvr::D3D12Only<&OnDispatch>::Call);
        reshade::register_event<reshade::addon_event::draw_or_dispatch_indirect>(&d2rvr::D3D12Only<&OnIndirect>::Call);
        reshade::register_event<reshade::addon_event::copy_resource>(&d2rvr::D3D12Only<&OnCopy>::Call);
        reshade::register_event<reshade::addon_event::copy_texture_region>(&d2rvr::D3D12Only<&OnCopyRegion>::Call);
        reshade::register_event<reshade::addon_event::resolve_texture_region>(&d2rvr::D3D12Only<&OnResolve>::Call);
        reshade::register_event<reshade::addon_event::present>(&d2rvr::D3D12Only<&OnPresent>::Call);
        reshade::register_event<reshade::addon_event::bind_scissor_rects>(&d2rvr::D3D12Only<&OnScissor>::Call);
        reshade::register_event<reshade::addon_event::push_constants>(&d2rvr::D3D12Only<&OnPushConstants>::Call);
        reshade::register_event<reshade::addon_event::execute_secondary_command_list>(&d2rvr::D3D12Only<&OnExecuteSecondary>::Call);
    }
    if (trace || skip) {
        reshade::register_event<reshade::addon_event::draw>(&d2rvr::D3D12Only<&OnDraw>::Call);
        reshade::register_event<reshade::addon_event::draw_indexed>(&d2rvr::D3D12Only<&OnDrawIndexed>::Call);
        reshade::register_event<reshade::addon_event::bind_pipeline>(&d2rvr::D3D12Only<&OnBindPipeline>::Call);
    }
    if (trace || depthViaReShade)
        reshade::register_event<reshade::addon_event::push_descriptors>(&d2rvr::D3D12Only<&OnPushDescriptors>::Call);
}

// Records the next whole frame into path.
bool Arm(const std::wstring& path) {
    if (!g_traceOn) return false;   // its events are not registered: [debug] ui_trace=1 and a restart
    int idle = 0;
    if (!g_state.compare_exchange_strong(idle, 1)) return false;
    std::lock_guard<std::mutex> g(g_lock);
    g_path = path;
    return true;
}

bool Busy() { return g_state.load() != 0; }

// [debug] skip_pipeline: 0 = off
void SetSkip(uint64_t pipe) { g_skipPipe.store(pipe); }
void SetGeometry(bool on) { g_geometry.store(on); }
void SetEachPass(bool on) { g_eachPass.store(on); }
// [debug] layer_log: the ground map's material layers logged as they move (needs ui_trace: its events)
void SetLayerLog(bool on, void (*log)(const char*)) {
    g_logFn = log;
    if (on && !g_layerLog.load()) { std::lock_guard<std::mutex> g(g_layerLock); g_layerLast.clear(); g_layerLines = 0; }
    g_layerLog.store(on && g_traceOn);
}
// The constant buffer register (b<n>, as register + 1; 0 = not a root CBV / unknown) a root signature's parameter binds -
// for vrcam's own root CBV hooks (camfix: ground_layer_fix).
uint32_t CbRegister(uint64_t layout, uint32_t param) { return RegisterOf(layout, param); }
// [debug] ui_trace_srv: up to 8 pixel shader hashes whose textures the trace lists
void SetSrvWatch(const uint64_t* hashes, int n) { for (int i = 0; i < 8; ++i) g_srvWatch[i].store(i < n ? hashes[i] : 0); }
void SetDumpCompute(bool on) { g_dumpCompute.store(on); }
void SetDumpShaders(const uint64_t* hashes, int n, const std::wstring& dir) {
    for (int i = 0; i < 16; ++i) g_dumpHash[i].store(i < n ? hashes[i] : 0);
    std::lock_guard<std::mutex> g(g_hashLock);
    g_dumpDir = dir;
}
// [render] depth_fix / depth_range: the game's linear depth from 0 to range world units (off: the game's own)
void SetDepthRange(bool on, float range) {
    g_depthRange.store(std::clamp(range, 50.0f, 100000.0f));
    g_depthFix.store(on);
}
uint32_t DepthRangeFixes() { return g_depthPatched.load(); }   // b10 buffers rewritten so far
// From vrcam's own root CBV hook (camfix), which runs before ReShade tells add-ons of the binding:
// with the right eye replayed, camfix copies a camera buffer - b10 among them, the camera's position
// at [1] - into its own slot right there, and the copy, not the game's buffer, is what the GPU reads.
bool DepthFixFor(uint64_t layout, uint32_t param) { return g_depthFix.load(std::memory_order_relaxed) && IsB10(layout, param); }
void FixDepthRangeAt(uint8_t* cb) {   // cb: the game's b10, in its upload memory
    if (WriteRange((float*)(cb + 53 * 16), g_depthStart.load(std::memory_order_relaxed), g_depthRange.load(std::memory_order_relaxed)))
        g_depthPatched.fetch_add(1, std::memory_order_relaxed);
}
int DepthRangeLayouts() { return g_b10n.load(); }             // root signatures with a b10 found
// [render] skip_shaders: up to 8 pixel shader hashes; pipelines already made of them are found again
void SetSkipShaders(const uint64_t* hashes, int n) {
    for (int i = 0; i < kSkipMax; ++i) g_skipHash[i].store(i < n ? hashes[i] : 0);
    g_skipHandles.store(0);
    std::lock_guard<std::mutex> g(g_hashLock);
    for (const auto& kv : g_psHash) if (SkipHash(kv.second)) AddSkipHandle(kv.first);
}

}  // namespace uitrace
