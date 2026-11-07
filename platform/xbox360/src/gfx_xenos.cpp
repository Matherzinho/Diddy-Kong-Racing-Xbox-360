// dkr360: Golden Balloon's fast3d on the Xbox 360's Xenos GPU through xsdk.
//
// The renderer of the Star Fox 64 / Smash 64 / Zelda ports (libultraship's gfx_xenos.cpp), reshaped for Golden
// Balloon's C rendering API (platform/fast3d/gfx_rendering_api.h): one target (the screen, in eDRAM), resolved into a
// front buffer at the end of each frame; one vertex shader for every combiner, its fetches set per vertex layout; one
// pixel shader template in 256 variants (xenos/fast3d.frag), the combiner itself in constants.
//
// Coordinates as Golden Balloon's OpenGL backend gives them: viewports and scissors from the bottom-left, flipped here;
// depth 0 to 1 (z_is_from_0_to_1: the frontend converts), near-plane clipping done on the CPU by the frontend.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <map>
#include <vector>

extern "C" {
#include <xsdk/gpu.h>
#include <xsdk/kernel.h>
#include <xsdk/log.h>
#include <xsdk/present.h>
#include <xsdk/shader.h>
#include <xsdk/texture.h>

#include "gfx_cc.h"
#include "gfx_rendering_api.h"
}

#include "dkr360_shaders.h"
#include "dkr360_prof.h" // fast3d.vert, fast3d_v0..255.frag, ui.vert, ui.frag

extern "C" void Oot360_LogMemory(const char* where);
extern "C" void Oot360_Phase(const char* phase);
extern "C" void dkr360_menu_render(void); // dkr360_menu.cpp: the console menu, over the frame

namespace {

enum XenosPixelVariant : uint32_t {
    XENOS_PS_TP0 = 1,
    XENOS_PS_TP1 = 2,
    XENOS_PS_CYC2 = 4,
    XENOS_PS_KILL = 8,
    XENOS_PS_NOISE = 16,
    XENOS_PS_T0 = 32,
    XENOS_PS_T1 = 64,
    XENOS_PS_MASK0 = 128,
    XENOS_PS_VARIANTS = 256,
};

constexpr int kXenosOperands = 6;
constexpr int kXenosVertexSources = 8;
constexpr int kXenosPixelSources = 6;

struct XenosVertexConstants {
    float prim[4];
    float w[kXenosOperands * kXenosVertexSources][4];
};

struct XenosPixelConstants {
    float sizes[4];
    float misc[4];
    float opts0[4];
    float opts1[4];
    float clamps[4];
    float w[kXenosOperands * kXenosPixelSources][4];
};

struct XenosTexture {
    XsdkTexture texture = {};
    volatile uint32_t* memory = nullptr;
    uint32_t bytes = 0;
    uint16_t width = 0;
    uint16_t height = 0;
    bool linear = false;
    bool threePoint = false;
    uint32_t addressU = 0;
    uint32_t addressV = 0;
    XsdkSamplerState sampler = {};
    bool samplerDirty = true;
};

constexpr uint32_t kBeginTimeoutMs = 5000;
constexpr uint32_t kFenceTimeoutMs = 2000;
constexpr uint32_t kPoolBytes = 16u << 20;
constexpr uint32_t kTextureAlign = 4096;
constexpr uint32_t kFramesInFlight = 2;
constexpr uint32_t kVertexRingBytes = 8u << 20;
constexpr uint64_t kTraceFrames = 2;

// The N64's texture address modes (PR/gbi.h): G_TX_MIRROR 1, G_TX_CLAMP 2.
constexpr uint32_t kTxMirror = 1, kTxClamp = 2;

#define XENOS_VARIANT(n) &fast3d_v##n##_frag,
const XsdkBuiltShader* const kPixelVariants[XENOS_PS_VARIANTS] = {
#include "fast3d_variants.inc"
};
#undef XENOS_VARIANT
#define XENOS_VARIANT(n)                                                                                             \
    static_assert(FAST3D_V##n##_FRAG_PUSH == 0 && FAST3D_V##n##_FRAG_PUSH_REGISTERS * 16 == sizeof(XenosPixelConstants), \
                  "fast3d.frag's Push block moved or changed");                                                      \
    static_assert(FAST3D_V##n##_FRAG_SAMPLER_0_0 == 0 && FAST3D_V##n##_FRAG_SAMPLER_0_1 == 1, "samplers moved");
#include "fast3d_variants.inc"
#undef XENOS_VARIANT
static_assert(FAST3D_VERT_PUSH_REGISTERS * 16 == sizeof(XenosVertexConstants), "fast3d.vert's Push block changed");

struct UiVertex {
    float x, y;
    float u, v;
    uint32_t color;
};
static_assert(sizeof(UiVertex) == 20, "UiVertex");

const XsdkVertexElement kUiLayout[] = {
    { .usage = XSDK_USAGE_POSITION, .usage_index = 0, .format = XSDK_VERTEX_FLOAT2, .stream = 0, .offset_bytes = 0 },
    { .usage = XSDK_USAGE_TEXCOORD, .usage_index = 0, .format = XSDK_VERTEX_FLOAT2, .stream = 0, .offset_bytes = 8 },
    { .usage = XSDK_USAGE_COLOR, .usage_index = 0, .format = XSDK_VERTEX_UBYTE4N, .stream = 0, .offset_bytes = 16 },
};

void WriteWords(volatile uint32_t* dst, const void* src, uint32_t bytes) {
    const uint8_t* s = static_cast<const uint8_t*>(src);
    const uint32_t words = bytes / 4;
    for (uint32_t i = 0; i < words; i++) {
        uint32_t w;
        memcpy(&w, s + i * 4, 4);
        dst[i] = w;
    }
}

uint32_t AddressMode(uint32_t cm) {
    if (cm & kTxClamp) {
        return (cm & kTxMirror) ? XSDK_ADDRESS_MIRROR_ONCE : XSDK_ADDRESS_CLAMP;
    }
    return (cm & kTxMirror) ? XSDK_ADDRESS_MIRROR : XSDK_ADDRESS_WRAP;
}

// xsdk sets Direct3D 9's pixel centres; fast3d expects Direct3D 11's / OpenGL's, half a pixel in (Zelda MM port).
void SetViewportD3D9(float x, float y, float width, float height) {
    xsdk_gpu_set_viewport(x - 0.5f, y - 0.5f, width, height, 0.0f, 1.0f);
}

void AddWeight(XenosVertexConstants& v, XenosPixelConstants& p, int op, int item, float sign, int lane, int count) {
    float* w = nullptr;
    switch (item) {
        case SHADER_INPUT_1:
        case SHADER_INPUT_2:
        case SHADER_INPUT_3:
        case SHADER_INPUT_4:
        case SHADER_INPUT_5:
        case SHADER_INPUT_6:
        case SHADER_INPUT_7:
            w = v.w[op * kXenosVertexSources + (item - SHADER_INPUT_1)];
            break;
        case SHADER_1:
            w = v.w[op * kXenosVertexSources + 7];
            break;
        case SHADER_TEXEL0:
            w = p.w[op * kXenosPixelSources + 0];
            break;
        case SHADER_TEXEL0A:
            w = p.w[op * kXenosPixelSources + 1];
            break;
        case SHADER_TEXEL1:
            w = p.w[op * kXenosPixelSources + 2];
            break;
        case SHADER_TEXEL1A:
            w = p.w[op * kXenosPixelSources + 3];
            break;
        case SHADER_COMBINED:
            w = p.w[op * kXenosPixelSources + 4];
            break;
        case SHADER_NOISE:
            w = p.w[op * kXenosPixelSources + 5];
            break;
        default:
            return;
    }
    for (int i = lane; i < lane + count; i++) {
        w[i] += sign;
    }
}

void CycleWeights(XenosVertexConstants& v, XenosPixelConstants& p, int cycle, const int items[4], int lane,
                  int count) {
    AddWeight(v, p, cycle * 3 + 0, items[0], 1.0f, lane, count);
    AddWeight(v, p, cycle * 3 + 0, items[1], -1.0f, lane, count);
    AddWeight(v, p, cycle * 3 + 1, items[2], 1.0f, lane, count);
    AddWeight(v, p, cycle * 3 + 2, items[3], 1.0f, lane, count);
}

} // namespace

struct ShaderProgram {
    uint64_t shaderId0;
    uint32_t shaderId1;
    uint8_t numInputs;
    bool usedTextures[2];
    uint8_t numFloats;
    XsdkShader* vertex;
    uint32_t variant;
    XenosVertexConstants vertexConstants;
    XenosPixelConstants pixelConstants;
};

namespace {

struct Pool {
    volatile uint32_t* base;
    uint32_t bytes;
    std::map<uint32_t, uint32_t> freeRanges;
};
struct GpuBlock {
    volatile uint32_t* memory;
    uint32_t bytes;
    uint64_t frame;
};

struct Xenos {
    bool open = false;
    bool headless = false;
    uint32_t width = 1280, height = 720;
    bool gpuHung = false;

    std::map<std::pair<uint64_t, uint32_t>, ShaderProgram> shaderPool;
    std::map<uint64_t, XsdkShader*> vertexShaders;
    XsdkShader pixelShaders[XENOS_PS_VARIANTS] = {};
    XsdkShader uiVertex = {}, uiPixel = {};
    ShaderProgram* current = nullptr;

    std::vector<XenosTexture> textures;
    std::vector<uint32_t> freeTextureIds;
    uint32_t currentTextureIds[2] = {};
    int currentTile = 0;
    uint32_t whiteTexture = 0;

    // set_depth_mode, as the OpenGL backend reads it
    bool depthEnabled = false, depthWrite = false, depthCompare = false, decal = false;
    int blendMode = GFX_BLEND_DISABLED;
    int viewport[4] = { 0, 0, 1280, 720 };
    int scissor[4] = { 0, 0, 1280, 720 };
    bool viewportDirty = true, scissorDirty = true;

    // GPU state as last set
    int8_t lastDepthKey = -1, lastBlend = -1, lastDecal = -1;
    const XsdkShader* lastVertex = nullptr;
    const XsdkShader* lastPixel = nullptr;
    const ShaderProgram* constProgram = nullptr;
    uint32_t constVariant = 0;
    XsdkDepthStencilState depthLequal[2] = {}; // [write]
    XsdkDepthStencilState depthAlways[2] = {}; // [write]
    XsdkDepthStencilState depthNone = {};
    XsdkBlendState blendOff = {}, blendAlpha = {}, blendModulate = {}, colorNone = {};
    XsdkRasterState rasterNormal = {}, rasterDecal = {};
    XsdkSamplerState uiSampler = {};

    volatile uint32_t* vertexRing[kFramesInFlight] = {};
    uint32_t vertexOffset = 0;
    uint32_t ringIndex = 0;
    uint32_t frameFence[kFramesInFlight] = {};
    uint64_t frame = 0;
    uint32_t frameCount = 0;
    uint32_t overflows = 0;
    uint64_t drawsThisFrame = 0;
    bool inFrame = false;

    std::vector<Pool> pools;
    std::vector<GpuBlock> pendingFrees;
    std::map<uint64_t, uint32_t> fenceOfFrame;
    uint64_t bigPoolRetryFrame = 0;
    uint64_t reclaimFrame = ~0ull;

    double lastLogTime = 0.0;
    uint64_t lastLogFrame = 0;
    float fps = 0.0f;
    double fpsWindowStart = 0.0;
    uint32_t fpsWindowFrames = 0;
};

Xenos X;

double NowSeconds() {
    using namespace std::chrono;
    return duration_cast<duration<double>>(steady_clock::now().time_since_epoch()).count();
}

bool Tracing() {
    return X.frame < kTraceFrames;
}

void Trace(const char* format, ...) __attribute__((format(printf, 1, 2)));
void Trace(const char* format, ...) {
    if (!Tracing()) {
        return;
    }
    char line[256];
    va_list args;
    va_start(args, format);
    vsnprintf(line, sizeof line, format, args);
    va_end(args);
    xsdk_log_write("xenos[f%llu]: %s", (unsigned long long)X.frame, line);
}

bool TraceSync(const char* step) {
    if (X.gpuHung) {
        return false;
    }
    if (!Tracing()) {
        return true;
    }
    const NTSTATUS s = xsdk_gpu_wait(xsdk_gpu_fence(), 1000);
    if (!NT_SUCCESS(s)) {
        X.gpuHung = true;
        xsdk_log_write("xenos[f%llu]: GPU HUNG after %s (0x%08X)", (unsigned long long)X.frame, step, (unsigned)s);
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------------------------------------------
// GPU memory: textures come from pools carved in 4 KB steps; what the GPU may still read waits for its frame's fence.

volatile uint32_t* GpuTakeFree(uint32_t bytes) {
    for (Pool& pool : X.pools) {
        for (auto it = pool.freeRanges.begin(); it != pool.freeRanges.end(); ++it) {
            if (it->second >= bytes) {
                const uint32_t offset = it->first;
                const uint32_t size = it->second;
                pool.freeRanges.erase(it);
                if (size > bytes) {
                    pool.freeRanges[offset + bytes] = size - bytes;
                }
                return pool.base + offset / 4;
            }
        }
    }
    return nullptr;
}

volatile uint32_t* GpuNewPool(uint32_t poolBytes, uint32_t bytes) {
    poolBytes = (poolBytes + 0xFFFFu) & ~0xFFFFu;
    volatile uint32_t* base = xsdk_gpu_alloc(poolBytes);
    if (!base) {
        return nullptr;
    }
    Pool pool = { base, poolBytes, {} };
    if (poolBytes > bytes) {
        pool.freeRanges[bytes] = poolBytes - bytes;
    }
    X.pools.push_back(std::move(pool));
    if (poolBytes >= (1u << 20)) {
        xsdk_log_write("xenos: GPU pool %u: %u KB", (unsigned)X.pools.size(), (unsigned)(poolBytes >> 10));
        Oot360_LogMemory("after a GPU pool");
    }
    return base;
}

void GpuReturn(const GpuBlock& b) {
    for (Pool& pool : X.pools) {
        const uintptr_t start = (uintptr_t)pool.base;
        const uintptr_t at = (uintptr_t)b.memory;
        if (at >= start && at < start + pool.bytes) {
            uint32_t offset = (uint32_t)(at - start);
            uint32_t size = b.bytes;
            auto next = pool.freeRanges.lower_bound(offset);
            if (next != pool.freeRanges.end() && offset + size == next->first) {
                size += next->second;
                next = pool.freeRanges.erase(next);
            }
            if (next != pool.freeRanges.begin()) {
                auto prev = std::prev(next);
                if (prev->first + prev->second == offset) {
                    prev->second += size;
                    return;
                }
            }
            pool.freeRanges[offset] = size;
            return;
        }
    }
}

volatile uint32_t* GpuAlloc(uint32_t bytes) {
    bytes = (bytes + kTextureAlign - 1) & ~(kTextureAlign - 1);
    if (volatile uint32_t* p = GpuTakeFree(bytes)) {
        return p;
    }
    if (X.frame >= X.bigPoolRetryFrame) {
        if (volatile uint32_t* p = GpuNewPool(std::max(kPoolBytes, bytes), bytes)) {
            return p;
        }
        X.bigPoolRetryFrame = X.frame + 600;
    }
    if (!X.pendingFrees.empty() && X.reclaimFrame != X.frame) {
        X.reclaimFrame = X.frame;
        if (NT_SUCCESS(xsdk_gpu_wait(xsdk_gpu_fence(), kFenceTimeoutMs))) {
            for (const GpuBlock& b : X.pendingFrees) {
                GpuReturn(b);
            }
            X.pendingFrees.clear();
            if (volatile uint32_t* p = GpuTakeFree(bytes)) {
                return p;
            }
        }
    }
    static const uint32_t kSmallerPools[] = { 4u << 20, 1u << 20, 256u << 10 };
    for (uint32_t poolBytes : kSmallerPools) {
        if (poolBytes > bytes) {
            if (volatile uint32_t* p = GpuNewPool(poolBytes, bytes)) {
                return p;
            }
        }
    }
    if (volatile uint32_t* p = GpuNewPool(bytes, bytes)) {
        return p;
    }
    static unsigned sOutOfMemory;
    if (sOutOfMemory++ < 16) {
        xsdk_log_write("xenos: out of GPU memory for %u bytes", (unsigned)bytes);
    }
    return nullptr;
}

void GpuFreeLater(volatile uint32_t* memory, uint32_t bytes) {
    if (!memory) {
        return;
    }
    bytes = (bytes + kTextureAlign - 1) & ~(kTextureAlign - 1);
    X.pendingFrees.push_back({ memory, bytes, X.frame });
}

void GpuReclaim() {
    auto done = [&](uint64_t frame) {
        auto it = X.fenceOfFrame.find(frame);
        if (it == X.fenceOfFrame.end()) {
            return frame < X.frame;
        }
        return xsdk_gpu_fence_done(it->second);
    };
    auto keep = std::remove_if(X.pendingFrees.begin(), X.pendingFrees.end(), [&](const GpuBlock& b) {
        if (!done(b.frame)) {
            return false;
        }
        GpuReturn(b);
        return true;
    });
    X.pendingFrees.erase(keep, X.pendingFrees.end());
    for (auto it = X.fenceOfFrame.begin(); it != X.fenceOfFrame.end();) {
        it = (it->first + 8 < X.frame) ? X.fenceOfFrame.erase(it) : std::next(it);
    }
}

volatile uint32_t* VertexSpace(uint32_t bytes, uint32_t* address) {
    bytes = (bytes + 63) & ~63u;
    volatile uint32_t* ring = X.vertexRing[X.ringIndex];
    if (!ring || bytes > kVertexRingBytes) {
        return nullptr;
    }
    if (X.vertexOffset + bytes > kVertexRingBytes) {
        X.overflows++;
        xsdk_gpu_wait(xsdk_gpu_fence(), kFenceTimeoutMs);
        X.vertexOffset = 0;
    }
    volatile uint32_t* mem = ring + X.vertexOffset / 4;
    *address = xsdk_gpu_address(mem);
    X.vertexOffset += bytes;
    return mem;
}

void ResetDrawState() {
    X.lastDepthKey = -1;
    X.lastBlend = -1;
    X.lastDecal = -1;
    X.lastVertex = nullptr;
    X.lastPixel = nullptr;
    X.constProgram = nullptr;
    X.viewportDirty = true;
    X.scissorDirty = true;
}

// ---------------------------------------------------------------------------------------------------------------
// Textures

XenosTexture& Texture(uint32_t id) {
    if (id >= X.textures.size()) {
        X.textures.resize(id + 1);
    }
    return X.textures[id];
}

bool CreateTextureStorage(XenosTexture& t, uint32_t format, uint32_t width, uint32_t height) {
    GpuFreeLater(t.memory, t.bytes);
    t.memory = nullptr;
    XsdkTexture tex = {};
    if (!NT_SUCCESS(xsdk_texture_init(&tex, format, width, height, 1, true))) {
        return false;
    }
    volatile uint32_t* mem = GpuAlloc(tex.size_bytes);
    if (!mem) {
        return false;
    }
    xsdk_texture_attach(&tex, mem);
    t.texture = tex;
    t.memory = mem;
    t.bytes = tex.size_bytes;
    t.width = (uint16_t)width;
    t.height = (uint16_t)height;
    t.samplerDirty = true;
    return true;
}

void ApplySampler(XenosTexture& t) {
    if (!t.samplerDirty) {
        return;
    }
    XsdkSampler s = {};
    s.mag_filter = (t.linear && !t.threePoint) ? XSDK_FILTER_LINEAR : XSDK_FILTER_POINT;
    s.min_filter = s.mag_filter;
    s.mip_filter = XSDK_FILTER_POINT;
    s.address_u = t.addressU;
    s.address_v = t.addressV;
    xsdk_texture_sampler_state(&t.sampler, &s);
    t.samplerDirty = false;
}

uint32_t NewTextureId() {
    if (!X.freeTextureIds.empty()) {
        const uint32_t id = X.freeTextureIds.back();
        X.freeTextureIds.pop_back();
        X.textures[id] = XenosTexture();
        return id;
    }
    X.textures.emplace_back();
    return (uint32_t)X.textures.size() - 1;
}

// ---------------------------------------------------------------------------------------------------------------
// Shaders

XsdkShader* VertexShaderFor(const CCFeatures& cc, uint8_t* numFloats) {
    // Golden Balloon's packing (gfx_pc_dkr.c, dkr_emit_tri): position, then what the remastered effects add, per
    // texture its coordinates, fog, the combiner inputs.
    struct Input {
        bool present;
        uint32_t format;
        uint32_t offset;
    } in[16] = {};
    const uint32_t vecFormat[5] = { 0, XSDK_VERTEX_FLOAT1, XSDK_VERTEX_FLOAT2, XSDK_VERTEX_FLOAT3, XSDK_VERTEX_FLOAT4 };
    uint32_t pos = 0;
    in[0] = { true, XSDK_VERTEX_FLOAT4, 0 };
    pos += 4;
    if (cc.opt_world_pos) {
        pos += 3;
    }
    if (cc.opt_dfdx_light) {
        pos += 6;
    }
    for (int t = 0; t < 2; t++) {
        if (!cc.used_textures[t]) {
            continue;
        }
        in[2 + t] = { true, XSDK_VERTEX_FLOAT2, pos * 4 };
        pos += 2;
    }
    if (cc.opt_fog) {
        in[8] = { true, XSDK_VERTEX_FLOAT4, pos * 4 };
        pos += 4;
    }
    const uint32_t inputFloats = cc.opt_alpha ? 4 : 3;
    for (int i = 0; i < cc.num_inputs && i < 7; i++) {
        const int location = i == 0 ? 1 : 10 + (i - 1);
        in[location] = { true, vecFormat[inputFloats], pos * 4 };
        pos += inputFloats;
    }
    *numFloats = (uint8_t)pos;

    uint64_t key = pos;
    for (int l = 0; l < 16; l++) {
        key = key * 1000003ull + (in[l].present ? in[l].offset * 16 + in[l].format + 1 : 0);
    }
    auto found = X.vertexShaders.find(key);
    if (found != X.vertexShaders.end()) {
        return found->second;
    }
    // Inputs the layout doesn't have read the position: the combiner weighs them by 0.
    const uint32_t declared[16] = { XSDK_VERTEX_FLOAT4, XSDK_VERTEX_FLOAT4, XSDK_VERTEX_FLOAT2, XSDK_VERTEX_FLOAT2,
                                    XSDK_VERTEX_FLOAT1, XSDK_VERTEX_FLOAT1, XSDK_VERTEX_FLOAT1, XSDK_VERTEX_FLOAT1,
                                    XSDK_VERTEX_FLOAT4, XSDK_VERTEX_FLOAT4, XSDK_VERTEX_FLOAT4, XSDK_VERTEX_FLOAT4,
                                    XSDK_VERTEX_FLOAT4, XSDK_VERTEX_FLOAT4, XSDK_VERTEX_FLOAT4, XSDK_VERTEX_FLOAT4 };
    XsdkVertexElement layout[16] = {};
    for (int l = 0; l < 16; l++) {
        layout[l].usage = l == 0 ? XSDK_USAGE_POSITION : l == 1 ? XSDK_USAGE_COLOR : XSDK_USAGE_TEXCOORD;
        layout[l].usage_index = l < 2 ? 0 : l - 2;
        layout[l].format = in[l].present ? in[l].format : declared[l];
        layout[l].stream = 0;
        layout[l].offset_bytes = in[l].present ? in[l].offset : 0;
    }
    auto* shader = new XsdkShader();
    NTSTATUS s = xsdk_shader_load(shader, fast3d_vert.container, fast3d_vert.container_bytes);
    if (NT_SUCCESS(s)) {
        const uint32_t stride = pos * 4;
        s = xsdk_shader_set_vertex_layout(shader, layout, 16, &stride);
    }
    if (!NT_SUCCESS(s)) {
        xsdk_log_write("xenos: vertex shader for a %u-float layout failed: 0x%08X", (unsigned)pos, (unsigned)s);
    }
    X.vertexShaders[key] = shader;
    return shader;
}

void BuildCombinerConstants(const CCFeatures& cc, ShaderProgram& prg) {
    XenosVertexConstants& v = prg.vertexConstants;
    XenosPixelConstants& out = prg.pixelConstants;
    memset(&v, 0, sizeof v);
    memset(&out, 0, sizeof out);
    bool noise = cc.opt_noise;
    for (int cyc = 0; cyc < (cc.opt_2cyc ? 2 : 1); cyc++) {
        int colorItems[4], alphaItems[4];
        for (int k = 0; k < 4; k++) {
            colorItems[k] = cc.c[cyc][0][k];
            alphaItems[k] = (cc.color_alpha_same[cyc] || !cc.opt_alpha) ? cc.c[cyc][0][k] : cc.c[cyc][1][k];
            noise = noise || colorItems[k] == SHADER_NOISE || alphaItems[k] == SHADER_NOISE;
        }
        CycleWeights(v, out, cyc, colorItems, 0, 3);
        CycleWeights(v, out, cyc, alphaItems, 3, 1);
    }
    out.opts0[0] = cc.opt_alpha;
    out.opts0[1] = cc.opt_fog;
    out.opts0[2] = cc.opt_texture_edge;
    out.opts0[3] = cc.opt_noise;
    out.misc[2] = cc.c[1][1][2] == SHADER_COMBINED;
    out.misc[3] = cc.c[1][0][2] == SHADER_COMBINED;
    prg.variant = 0;
    if (cc.opt_2cyc) {
        prg.variant |= XENOS_PS_CYC2;
    }
    if (cc.opt_alpha && cc.opt_texture_edge) {
        prg.variant |= XENOS_PS_KILL;
    }
    if (noise) {
        prg.variant |= XENOS_PS_NOISE;
    }
    if (cc.used_textures[0]) {
        prg.variant |= XENOS_PS_T0;
    }
    if (cc.used_textures[1]) {
        prg.variant |= XENOS_PS_T1;
    }
}

// ---------------------------------------------------------------------------------------------------------------
// The rendering API

bool XenosZFrom0To1(void) {
    return true;
}

void XenosUnloadShader(struct ShaderProgram*) {
}

void XenosLoadShader(struct ShaderProgram* prg) {
    X.current = prg;
}

struct ShaderProgram* XenosCreateShader(uint64_t id0, uint32_t id1) {
    CCFeatures cc;
    gfx_cc_get_features(id0, id1, &cc);
    ShaderProgram& prg = X.shaderPool[std::make_pair(id0, id1)];
    prg.shaderId0 = id0;
    prg.shaderId1 = id1;
    prg.numInputs = (uint8_t)cc.num_inputs;
    prg.usedTextures[0] = cc.used_textures[0];
    prg.usedTextures[1] = cc.used_textures[1];
    prg.vertex = VertexShaderFor(cc, &prg.numFloats);
    BuildCombinerConstants(cc, prg);
    if (X.shaderPool.size() <= 4 || X.shaderPool.size() % 64 == 0) {
        xsdk_log_write("xenos: shader %u: %016llx:%08x, %u floats a vertex, variant %u", (unsigned)X.shaderPool.size(),
                       (unsigned long long)id0, (unsigned)id1, (unsigned)prg.numFloats, (unsigned)prg.variant);
    }
    X.current = &prg;
    return &prg;
}

struct ShaderProgram* XenosLookupShader(uint64_t id0, uint32_t id1) {
    auto it = X.shaderPool.find(std::make_pair(id0, id1));
    return it == X.shaderPool.end() ? nullptr : &it->second;
}

void XenosShaderGetInfo(struct ShaderProgram* prg, uint8_t* numInputs, bool usedTextures[2]) {
    *numInputs = prg->numInputs;
    usedTextures[0] = prg->usedTextures[0];
    usedTextures[1] = prg->usedTextures[1];
}

uint32_t XenosNewTexture(void) {
    return NewTextureId();
}

void XenosDeleteTexture(uint32_t id) {
    if (id == 0 || id == X.whiteTexture || id >= X.textures.size()) {
        return;
    }
    XenosTexture& t = X.textures[id];
    GpuFreeLater(t.memory, t.bytes);
    t = XenosTexture();
    X.freeTextureIds.push_back(id);
}

void XenosSelectTexture(int tile, uint32_t id) {
    X.currentTextureIds[tile & 1] = id;
    X.currentTile = tile & 1;
}

bool XenosUploadTexture(const uint8_t* rgba, int width, int height) {
    if (width <= 0 || height <= 0) {
        return false;
    }
    DKR360_PROF_BEGIN(tex);
    XenosTexture& t = Texture(X.currentTextureIds[X.currentTile]);
    // Bytes red, green, blue, alpha: as a big-endian word, R8G8B8A8.
    if (!CreateTextureStorage(t, XSDK_FORMAT_R8G8B8A8, (uint32_t)width, (uint32_t)height)) {
        return false;
    }
    xsdk_texture_upload(&t.texture, 0, rgba, (uint32_t)width * 4);
    xsdk_gpu_invalidate(t.texture.address, t.texture.size_bytes);
    DKR360_PROF_END(tex, DKR360_PROF_TEXTURE);
    return true;
}

void XenosSetSamplerParameters(int tile, bool linear, uint32_t cms, uint32_t cmt) {
    XenosTexture& t = Texture(X.currentTextureIds[tile & 1]);
    // The N64's three-point filter where the game asks for bilinear (point samples, filtered in the shader).
    const bool threePoint = linear;
    const uint32_t u = AddressMode(cms);
    const uint32_t v = AddressMode(cmt);
    if (t.linear != linear || t.threePoint != threePoint || t.addressU != u || t.addressV != v) {
        t.linear = linear;
        t.threePoint = threePoint;
        t.addressU = u;
        t.addressV = v;
        t.samplerDirty = true;
    }
}

void XenosSetDepthMode(bool test, bool update, bool compare, bool sourcePrim, uint16_t zmode) {
    (void)sourcePrim;
    // As Golden Balloon's OpenGL backend: depth off is no test and no write; on without compare writes and passes.
    X.depthEnabled = test;
    X.depthWrite = test && update;
    X.depthCompare = test && compare;
    X.decal = test && compare && zmode == 0xc00;
}

void XenosSetViewport(int x, int y, int w, int h) {
    X.viewport[0] = x;
    X.viewport[1] = y;
    X.viewport[2] = w;
    X.viewport[3] = h;
    X.viewportDirty = true;
}

void XenosSetScissor(int x, int y, int w, int h) {
    X.scissor[0] = x;
    X.scissor[1] = y;
    X.scissor[2] = w;
    X.scissor[3] = h;
    X.scissorDirty = true;
}

void XenosSetBlendMode(enum GfxBlendMode mode) {
    X.blendMode = mode;
}

void ClearTarget(bool color, bool depth, uint32_t rgba) {
    const UiVertex quad[4] = {
        { -1, -1, 0, 0, rgba },
        { 1, -1, 0, 0, rgba },
        { -1, 1, 0, 0, rgba },
        { 1, 1, 0, 0, rgba },
    };
    uint32_t address;
    volatile uint32_t* mem = VertexSpace(sizeof quad, &address);
    if (!mem) {
        return;
    }
    WriteWords(mem, quad, sizeof quad);
    xsdk_gpu_invalidate(address, sizeof quad);
    const XenosTexture& white = Texture(X.whiteTexture);
    xsdk_gpu_set_shaders(&X.uiVertex, &X.uiPixel);
    xsdk_built_shader_set_literals(&ui_vert);
    xsdk_built_shader_set_literals(&ui_frag);
    // ui.vert: position * xform.xy + xform.zw, depth push.depth.x (1, the far plane).
    const float push[8] = { 1, 1, 0, 0, 1, 0, 0, 0 };
    xsdk_gpu_set_vertex_constants(UI_VERT_PUSH, push, UI_VERT_PUSH_REGISTERS);
    xsdk_gpu_set_depth_stencil(depth ? &X.depthAlways[1] : &X.depthNone);
    xsdk_gpu_set_blend(color ? &X.blendOff : &X.colorNone);
    xsdk_gpu_set_raster(&X.rasterNormal);
    SetViewportD3D9(0, 0, (float)X.width, (float)X.height);
    xsdk_gpu_set_scissor(0, 0, X.width, X.height);
    xsdk_gpu_set_texture(UI_FRAG_SAMPLER_0_0, &white.texture, &X.uiSampler);
    xsdk_gpu_set_vertices(0, address, sizeof quad);
    xsdk_gpu_draw(XSDK_PRIMITIVE_TRIANGLE_STRIP, 4);
    ResetDrawState();
}

void XenosDrawTriangles(float vbo[], size_t vboLen, size_t numTris) {
    if (X.gpuHung || !X.inFrame) {
        return;
    }
    ShaderProgram* prg = X.current;
    if (!prg || !prg->vertex || numTris == 0) {
        return;
    }
    XenosTexture* tex[2] = { nullptr, nullptr };
    uint32_t variant = prg->variant;
    for (int t = 0; t < 2; t++) {
        if (!prg->usedTextures[t]) {
            continue;
        }
        XenosTexture& x = Texture(X.currentTextureIds[t]);
        if (!x.memory) {
            continue;
        }
        ApplySampler(x);
        tex[t] = &x;
        if (x.threePoint) {
            variant |= t == 0 ? XENOS_PS_TP0 : XENOS_PS_TP1;
        }
    }
    const XsdkShader* pixel = &X.pixelShaders[variant];
    if (prg->vertex != X.lastVertex || pixel != X.lastPixel) {
        xsdk_gpu_set_shaders(prg->vertex, pixel);
        xsdk_built_shader_set_literals(&fast3d_vert);
        xsdk_built_shader_set_literals(kPixelVariants[variant]);
        X.lastVertex = prg->vertex;
        X.lastPixel = pixel;
    }
    // 0 no depth; 1/2 always pass (write off/on); 3/4 less-or-equal (write off/on)
    const int8_t dk = !X.depthEnabled ? 0 : (int8_t)((X.depthCompare ? 3 : 1) + (X.depthWrite ? 1 : 0));
    if (dk != X.lastDepthKey) {
        if (dk == 0) {
            xsdk_gpu_set_depth_stencil(&X.depthNone);
        } else if (dk <= 2) {
            xsdk_gpu_set_depth_stencil(&X.depthAlways[dk - 1]);
        } else {
            xsdk_gpu_set_depth_stencil(&X.depthLequal[dk - 3]);
        }
        X.lastDepthKey = dk;
    }
    const int8_t blendKey = (int8_t)X.blendMode;
    if (blendKey != X.lastBlend) {
        switch (X.blendMode) {
            case GFX_BLEND_DISABLED:
            case GFX_BLEND_ALPHA_RDP_MEMORY:
            case GFX_BLEND_ALPHA_RDP_CVG_MEMORY:
                xsdk_gpu_set_blend(&X.blendOff);
                break;
            case GFX_BLEND_MODULATE:
                xsdk_gpu_set_blend(&X.blendModulate);
                break;
            default:
                xsdk_gpu_set_blend(&X.blendAlpha);
                break;
        }
        X.lastBlend = blendKey;
    }
    if ((int8_t)X.decal != X.lastDecal) {
        xsdk_gpu_set_raster(X.decal ? &X.rasterDecal : &X.rasterNormal);
        X.lastDecal = (int8_t)X.decal;
    }
    if (X.viewportDirty) {
        SetViewportD3D9((float)X.viewport[0], (float)((int)X.height - X.viewport[1] - X.viewport[3]),
                        (float)X.viewport[2], (float)X.viewport[3]);
        X.viewportDirty = false;
    }
    if (X.scissorDirty) {
        const int x = std::max(X.scissor[0], 0);
        const int y = std::max((int)X.height - X.scissor[1] - X.scissor[3], 0);
        const int w = std::max(std::min(X.scissor[0] + X.scissor[2], (int)X.width) - x, 0);
        const int h = std::max(std::min((int)X.height - X.scissor[1], (int)X.height) - y, 0);
        xsdk_gpu_set_scissor(x, y, w, h);
        X.scissorDirty = false;
    }
    if (prg != X.constProgram || variant != X.constVariant) {
        xsdk_gpu_set_pixel_constants(FAST3D_V0_FRAG_PUSH + 2, prg->pixelConstants.opts0,
                                     FAST3D_V0_FRAG_PUSH_REGISTERS - 2);
        xsdk_gpu_set_vertex_constants(FAST3D_VERT_PUSH + 1, &prg->vertexConstants.w[0][0],
                                      FAST3D_VERT_PUSH_REGISTERS - 1);
        X.constProgram = prg;
        X.constVariant = variant;
    }
    float perDraw[2][4];
    for (int t = 0; t < 2; t++) {
        perDraw[0][t * 2] = tex[t] ? (float)tex[t]->width : 1.0f;
        perDraw[0][t * 2 + 1] = tex[t] ? (float)tex[t]->height : 1.0f;
    }
    perDraw[1][0] = (float)(X.frameCount % 150);
    perDraw[1][1] = 240.0f / (float)X.height;
    perDraw[1][2] = prg->pixelConstants.misc[2];
    perDraw[1][3] = prg->pixelConstants.misc[3];
    xsdk_gpu_set_pixel_constants(FAST3D_V0_FRAG_PUSH, &perDraw[0][0], 2);
    const float prim[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    xsdk_gpu_set_vertex_constants(FAST3D_VERT_PUSH, prim, 1);
    for (int t = 0; t < 2; t++) {
        const XenosTexture* x = tex[t] ? tex[t] : &Texture(X.whiteTexture);
        if (x->memory) {
            xsdk_gpu_set_texture(t == 0 ? FAST3D_V0_FRAG_SAMPLER_0_0 : FAST3D_V0_FRAG_SAMPLER_0_1, &x->texture,
                                 &x->sampler);
        }
    }
    const uint32_t bytes = (uint32_t)(vboLen * sizeof(float));
    uint32_t address;
    volatile uint32_t* mem = VertexSpace(bytes, &address);
    if (!mem) {
        return;
    }
    memcpy((void*)mem, vbo, bytes); // xsdk memcpy: VMX, in-order stores, safe for write-combined memory
    xsdk_gpu_invalidate(address, bytes);
    xsdk_gpu_set_vertices(0, address, bytes);
    if (vboLen != (size_t)prg->numFloats * 3 * numTris) {
        static unsigned sMismatch;
        if (sMismatch++ < 8) {
            xsdk_log_write("xenos: draw with %u floats for %u triangles of %u floats a vertex (shader %016llx:%08x)",
                           (unsigned)vboLen, (unsigned)numTris, (unsigned)prg->numFloats,
                           (unsigned long long)prg->shaderId0, (unsigned)prg->shaderId1);
        }
    }
    Trace("draw %llu: variant %u floats %u tris %u tex %ux%u depth %d blend %d viewport %d,%d %dx%d",
          (unsigned long long)X.drawsThisFrame, (unsigned)variant, (unsigned)prg->numFloats, (unsigned)numTris,
          tex[0] ? tex[0]->width : 0, tex[0] ? tex[0]->height : 0, (int)dk, X.blendMode, X.viewport[0], X.viewport[1],
          X.viewport[2], X.viewport[3]);
    xsdk_gpu_draw(XSDK_PRIMITIVE_TRIANGLE_LIST, (uint32_t)(3 * numTris));
    X.drawsThisFrame++;
    TraceSync("a draw");
}

bool XenosInit(void) {
    NTSTATUS s = xsdk_shader_load(&X.uiVertex, ui_vert.container, ui_vert.container_bytes);
    if (NT_SUCCESS(s)) {
        const uint32_t stride = sizeof(UiVertex);
        s = xsdk_shader_set_vertex_layout(&X.uiVertex, kUiLayout, 3, &stride);
    }
    if (NT_SUCCESS(s)) {
        s = xsdk_shader_load(&X.uiPixel, ui_frag.container, ui_frag.container_bytes);
    }
    for (uint32_t v = 0; v < XENOS_PS_VARIANTS && NT_SUCCESS(s); v++) {
        s = xsdk_shader_load(&X.pixelShaders[v], kPixelVariants[v]->container, kPixelVariants[v]->container_bytes);
    }
    if (!NT_SUCCESS(s)) {
        xsdk_log_write("xenos: loading shaders failed: 0x%08X", (unsigned)s);
        return false;
    }
    for (uint32_t i = 0; i < kFramesInFlight; i++) {
        X.vertexRing[i] = xsdk_gpu_alloc(kVertexRingBytes);
        if (!X.vertexRing[i]) {
            xsdk_log_write("xenos: no GPU memory for the vertex ring");
            return false;
        }
    }
    for (int write = 0; write < 2; write++) {
        XsdkDepthStencil d = {};
        d.depth_test = true;
        d.depth_write = write;
        // LEQUAL for every z mode, as Golden Balloon's OpenGL backend (equal-depth seams after clipping).
        d.depth_compare = XSDK_COMPARE_LESS_EQUAL;
        xsdk_gpu_depth_stencil_state(&X.depthLequal[write], &d);
        d.depth_compare = XSDK_COMPARE_ALWAYS;
        xsdk_gpu_depth_stencil_state(&X.depthAlways[write], &d);
    }
    {
        XsdkDepthStencil d = {};
        d.depth_test = false;
        d.depth_write = false;
        d.depth_compare = XSDK_COMPARE_ALWAYS;
        xsdk_gpu_depth_stencil_state(&X.depthNone, &d);
    }
    const XsdkBlend off = { XSDK_BLEND_ONE, XSDK_BLEND_ZERO, XSDK_BLEND_OP_ADD, XSDK_BLEND_ONE, XSDK_BLEND_ZERO,
                            XSDK_BLEND_OP_ADD, XSDK_WRITE_ALL, false };
    const XsdkBlend alpha = { XSDK_BLEND_SRC_ALPHA, XSDK_BLEND_ONE_MINUS_SRC_ALPHA, XSDK_BLEND_OP_ADD,
                              XSDK_BLEND_SRC_ALPHA, XSDK_BLEND_ONE_MINUS_SRC_ALPHA, XSDK_BLEND_OP_ADD,
                              XSDK_WRITE_ALL, false };
    const XsdkBlend modulate = { XSDK_BLEND_DST_COLOR, XSDK_BLEND_ZERO, XSDK_BLEND_OP_ADD, XSDK_BLEND_DST_ALPHA,
                                 XSDK_BLEND_ZERO, XSDK_BLEND_OP_ADD, XSDK_WRITE_ALL, false };
    XsdkBlend none = off;
    none.write_mask = 0;
    xsdk_gpu_blend_state(&X.blendOff, &off);
    xsdk_gpu_blend_state(&X.blendAlpha, &alpha);
    xsdk_gpu_blend_state(&X.blendModulate, &modulate);
    xsdk_gpu_blend_state(&X.colorNone, &none);
    // Decals two steps of the 24-bit depth buffer nearer (Zelda MM port, console test 8).
    const XsdkRaster normal = { XSDK_CULL_NONE, false, 0.0f, 0.0f, XSDK_FILL_SOLID };
    const XsdkRaster decal = { XSDK_CULL_NONE, false, -2.0f / 16777216.0f, -2.0f, XSDK_FILL_SOLID };
    xsdk_gpu_raster_state(&X.rasterNormal, &normal);
    xsdk_gpu_raster_state(&X.rasterDecal, &decal);
    const XsdkSampler ui = { .mag_filter = XSDK_FILTER_LINEAR,
                             .min_filter = XSDK_FILTER_LINEAR,
                             .mip_filter = XSDK_FILTER_POINT,
                             .address_u = XSDK_ADDRESS_CLAMP,
                             .address_v = XSDK_ADDRESS_CLAMP };
    xsdk_texture_sampler_state(&X.uiSampler, &ui);

    X.textures.resize(1); // id 0 is no texture
    X.whiteTexture = NewTextureId();
    {
        XenosTexture& w = Texture(X.whiteTexture);
        const uint8_t white[4] = { 0xFF, 0xFF, 0xFF, 0xFF };
        if (CreateTextureStorage(w, XSDK_FORMAT_R8G8B8A8, 1, 1)) {
            xsdk_texture_upload(&w.texture, 0, white, 4);
            xsdk_gpu_invalidate(w.texture.address, w.texture.size_bytes);
        }
        ApplySampler(w);
    }
    xsdk_log_write("xenos: renderer ready (%ux%u%s)", (unsigned)X.width, (unsigned)X.height,
                   X.headless ? ", no display" : "");
    Oot360_LogMemory("xenos renderer ready");
    return true;
}

void XenosOnResize(void) {
}

bool XenosStartFrame(void) {
    if (X.gpuHung) {
        return false;
    }
    X.frameCount++;
    X.drawsThisFrame = 0;
    X.inFrame = true;
    xsdk_gpu_set_target(X.width, X.height);
    ResetDrawState();
    ClearTarget(true, true, 0xFF000000);
    TraceSync("the frame's clear");
    return true;
}

void LogFrameStats() {
    const double now = NowSeconds();
    // the counter's fps: frames in the last half second
    if (X.fpsWindowStart == 0.0) {
        X.fpsWindowStart = now;
    }
    X.fpsWindowFrames++;
    if (now - X.fpsWindowStart >= 0.5) {
        X.fps = (float)(X.fpsWindowFrames / (now - X.fpsWindowStart));
        X.fpsWindowStart = now;
        X.fpsWindowFrames = 0;
    }
    if ((X.frame % 600) != 0) {
        return;
    }
    const double rate = X.lastLogTime > 0.0 ? (double)(X.frame - X.lastLogFrame) / (now - X.lastLogTime) : 0.0;
    xsdk_log_write("xenos: frame %llu, %.1f rendered fps, %llu draws, %u shaders, %u layouts, %u pools, %u overflows, "
                   "%u textures",
                   (unsigned long long)X.frame, rate, (unsigned long long)X.drawsThisFrame,
                   (unsigned)X.shaderPool.size(), (unsigned)X.vertexShaders.size(), (unsigned)X.pools.size(),
                   (unsigned)X.overflows, (unsigned)(X.textures.size() - X.freeTextureIds.size()));
    if (!X.headless) {
        // The display's side (console only): vblanks with no new frame, flips that ran late, the longest wait for a
        // front buffer, over the last 600 frames.
        static XsdkPresentStats sLast;
        XsdkPresentStats ps = {};
        xsdk_present_stats(&ps);
        xsdk_log_write("xenos: display %u vblanks, %u flips, %u missed, %u late, longest buffer wait %u us",
                       (unsigned)(ps.vblanks - sLast.vblanks), (unsigned)(ps.flips - sLast.flips),
                       (unsigned)(ps.missed - sLast.missed), (unsigned)(ps.late - sLast.late),
                       (unsigned)ps.longest_wait_us);
        sLast = ps;
    }
    X.lastLogTime = now;
    X.lastLogFrame = X.frame;
}

// A picture of the frame for checking the renderer where there is no display (Xenia): the frames listed in
// game:\dkr360_capture.txt, one number a line, go to game:\dkr360_frame_<n>.png. Off without the file.
void CaptureIfAsked() {
    static std::vector<uint64_t> sFrames;
    static bool sRead = false;
    if (!sRead) {
        sRead = true;
        if (FILE* f = fopen("dkr360_capture.txt", "r")) {
            unsigned long long n;
            while (fscanf(f, "%llu", &n) == 1) {
                sFrames.push_back(n);
            }
            fclose(f);
            xsdk_log_write("xenos: %u frames to capture", (unsigned)sFrames.size());
        }
    }
    if (sFrames.empty() || std::find(sFrames.begin(), sFrames.end(), X.frame) == sFrames.end()) {
        return;
    }
    static XenosTexture sTarget;
    if (!sTarget.memory) {
        XsdkTexture tex = {};
        if (!NT_SUCCESS(xsdk_texture_init(&tex, XSDK_FORMAT_A8R8G8B8, X.width, X.height, 1, true))) {
            return;
        }
        xsdk_texture_base_at_corner(&tex);
        volatile uint32_t* mem = GpuAlloc(tex.size_bytes);
        if (!mem) {
            return;
        }
        xsdk_texture_attach(&tex, mem);
        sTarget.texture = tex;
        sTarget.memory = mem;
    }
    xsdk_gpu_set_resolve_clears(false, false);
    xsdk_gpu_resolve_texture(&sTarget.texture, 0xFF000000);
    xsdk_gpu_set_resolve_clears(true, true);
    xsdk_gpu_set_target(X.width, X.height);
    ResetDrawState();
    xsdk_gpu_wait(xsdk_gpu_fence(), kFenceTimeoutMs);
    std::vector<uint8_t> argb((size_t)X.width * X.height * 4);
    xsdk_texture_download_rows(&sTarget.texture, 0, 0, X.height, argb.data(), X.width * 4);
    // Bytes A R G B (a big-endian A8R8G8B8 word) to R G B
    std::vector<uint8_t> rgb((size_t)X.width * X.height * 3);
    for (size_t i = 0; i < (size_t)X.width * X.height; i++) {
        rgb[i * 3 + 0] = argb[i * 4 + 1];
        rgb[i * 3 + 1] = argb[i * 4 + 2];
        rgb[i * 3 + 2] = argb[i * 4 + 3];
    }
    char path[64];
    snprintf(path, sizeof path, "dkr360_frame_%llu.ppm", (unsigned long long)X.frame);
    if (FILE* f = fopen(path, "wb")) {
        fprintf(f, "P6\n%u %u\n255\n", (unsigned)X.width, (unsigned)X.height);
        fwrite(rgb.data(), 1, rgb.size(), f);
        fclose(f);
        xsdk_log_write("xenos: frame %llu captured into %s", (unsigned long long)X.frame, path);
    }
}

void XenosEndFrame(void) {
    if (!X.inFrame) {
        return;
    }
    X.inFrame = false;
    if (X.gpuHung) {
        X.frame++;
        return;
    }
    // The console's own menu over the game's picture.
    {
        DKR360_PROF_BEGIN(menu);
        dkr360_menu_render();
        DKR360_PROF_END(menu, DKR360_PROF_MENU);
    }
    ResetDrawState();
    CaptureIfAsked();
    DKR360_PROF_BEGIN(gpu);
    uint32_t address = 0;
    Oot360_Phase("xenos: present wait");
    DKR360_PROF_BEGIN(present);
    if (X.headless) {
        // Xenia without a display: the picture goes nowhere; the game's own pacer paces the frames.
    } else if (NT_SUCCESS(xsdk_present_begin(&address, kBeginTimeoutMs))) {
        xsdk_gpu_resolve(address, X.width, X.height, 0xFF000000);
        xsdk_present_end();
        TraceSync("the present resolve");
    } else {
        xsdk_log_write("xenos: no front buffer came free");
        xsdk_gpu_resolve(xsdk_present_buffer_address(0), X.width, X.height, 0xFF000000);
    }
    ResetDrawState();
    DKR360_PROF_END(present, DKR360_PROF_PRESENT);
    const uint32_t fence = xsdk_gpu_fence();
    X.frameFence[X.ringIndex] = fence;
    X.fenceOfFrame[X.frame] = fence;
    X.frame++;
    X.ringIndex = (X.ringIndex + 1) % kFramesInFlight;
    Oot360_Phase("xenos: wait for the GPU");
    if (X.frameFence[X.ringIndex]) {
        xsdk_gpu_wait(X.frameFence[X.ringIndex], kFenceTimeoutMs);
    }
    Oot360_Phase("xenos: frame end");
    DKR360_PROF_END(gpu, DKR360_PROF_GPUWAIT);
    X.vertexOffset = 0;
    GpuReclaim();
    LogFrameStats();
    dkr360_prof_frame();
}

void XenosFinishRender(void) {
}

void XenosShutdown(void) {
}

} // namespace

// ---------------------------------------------------------------------------------------------------------------
// What the rest of the port calls

extern "C" {

struct GfxRenderingAPI gfx_xenos_api = {
    .z_is_from_0_to_1 = XenosZFrom0To1,
    .unload_shader = XenosUnloadShader,
    .load_shader = XenosLoadShader,
    .create_and_load_new_shader = XenosCreateShader,
    .lookup_shader = XenosLookupShader,
    .shader_get_info = XenosShaderGetInfo,
    .new_texture = XenosNewTexture,
    .delete_texture = XenosDeleteTexture,
    .select_texture = XenosSelectTexture,
    .upload_texture = XenosUploadTexture,
    .set_sampler_parameters = XenosSetSamplerParameters,
    .set_depth_mode = XenosSetDepthMode,
    .set_viewport = XenosSetViewport,
    .set_scissor = XenosSetScissor,
    .set_shadow_view = nullptr,
    .set_blend_mode = XenosSetBlendMode,
    .set_modern_character_occluder = nullptr,
    .draw_triangles = XenosDrawTriangles,
    .read_framebuffer_rgb = nullptr,
    .get_modern_character_capture_dimensions = nullptr,
    .get_modern_character_capture_projection = nullptr,
    .get_modern_character_scene_projection = nullptr,
    .read_modern_character_capture_rgba = nullptr,
    .begin_modern_character_gpu_timing = nullptr,
    .finish_modern_character_gpu_timing = nullptr,
    .init = XenosInit,
    .on_resize = XenosOnResize,
    .start_frame = XenosStartFrame,
    .begin_output_overlay = nullptr,
    .end_frame = XenosEndFrame,
    .finish_render = XenosFinishRender,
    .draw_modern_mesh = nullptr,
    .modern_mesh_available = nullptr,
    .draw_modern_skinned = nullptr,
    .release_modern_asset = nullptr,
    .clear_modern_mesh_cache = nullptr,
    .upload_texture_mipped = nullptr,
    .shutdown = XenosShutdown,
    .get_status = nullptr,
};

int dkr360_video_init(int* width, int* height) {
    NTSTATUS s = xsdk_gpu_open();
    if (!NT_SUCCESS(s)) {
        xsdk_log_write("xenos: xsdk_gpu_open failed: 0x%08X", (unsigned)s);
        return -1;
    }
    uint32_t w = 1280, h = 720;
    s = xsdk_present_open(XSDK_DISPLAY_720P, 3, 1, &w, &h);
    if (!NT_SUCCESS(s)) {
        xsdk_log_write("xenos: xsdk_present_open failed: 0x%08X; going on without a display (Xenia?)", (unsigned)s);
        X.headless = true;
        w = 1280;
        h = 720;
    } else {
        X.open = true;
    }
    X.width = w;
    X.height = h;
    *width = (int)w;
    *height = (int)h;
    xsdk_log_write("xenos: display %ux%u", (unsigned)w, (unsigned)h);
    return 0;
}

// Each VI swap (platform_sdl_present): the frame went to the front buffer in end_frame already.
void dkr360_video_present(void) {
}

int dkr360_video_headless(void) {
    return X.headless ? 1 : 0;
}

float dkr360_video_fps(void) {
    return X.fps;
}

// The console menu's drawing (dkr360_menu.cpp): textures and textured triangles in pixels, y down, alpha blended.
uint32_t dkr360_ui_texture(const uint8_t* rgba, int width, int height) {
    const uint32_t id = NewTextureId();
    XenosTexture& t = Texture(id);
    if (CreateTextureStorage(t, XSDK_FORMAT_R8G8B8A8, (uint32_t)width, (uint32_t)height)) {
        xsdk_texture_upload(&t.texture, 0, rgba, (uint32_t)width * 4);
        xsdk_gpu_invalidate(t.texture.address, t.texture.size_bytes);
    }
    t.linear = true;
    t.threePoint = false;
    t.addressU = t.addressV = XSDK_ADDRESS_CLAMP;
    t.samplerDirty = true;
    ApplySampler(t);
    return id;
}

// vertices: struct { float x, y, u, v; uint8_t r, g, b, a; } (UiVertex, colour as bytes R G B A)
void dkr360_ui_draw(uint32_t textureId, const void* vertices, uint32_t vertexCount, const uint16_t* indices,
                    uint32_t indexCount, int clipX, int clipY, int clipW, int clipH) {
    if (X.gpuHung || vertexCount == 0 || indexCount == 0) {
        return;
    }
    XenosTexture& t = Texture(textureId ? textureId : X.whiteTexture);
    if (!t.memory) {
        return;
    }
    ApplySampler(t);
    const int x0 = std::max(clipX, 0), y0 = std::max(clipY, 0);
    const int x1 = std::min(clipX + clipW, (int)X.width), y1 = std::min(clipY + clipH, (int)X.height);
    if (x1 <= x0 || y1 <= y0) {
        return;
    }
    const float sx = 2.0f / (float)X.width;
    const float sy = -2.0f / (float)X.height;
    const float push[8] = { sx, sy, -1.0f, 1.0f, 0, 0, 0, 0 };
    xsdk_gpu_set_shaders(&X.uiVertex, &X.uiPixel);
    xsdk_built_shader_set_literals(&ui_vert);
    xsdk_built_shader_set_literals(&ui_frag);
    xsdk_gpu_set_vertex_constants(UI_VERT_PUSH, push, UI_VERT_PUSH_REGISTERS);
    xsdk_gpu_set_depth_stencil(&X.depthNone);
    xsdk_gpu_set_blend(&X.blendAlpha);
    xsdk_gpu_set_raster(&X.rasterNormal);
    SetViewportD3D9(0, 0, (float)X.width, (float)X.height);
    xsdk_gpu_set_scissor((uint32_t)x0, (uint32_t)y0, (uint32_t)(x1 - x0), (uint32_t)(y1 - y0));
    const uint32_t vbytes = vertexCount * (uint32_t)sizeof(UiVertex);
    uint32_t vaddr;
    volatile uint32_t* vmem = VertexSpace(vbytes, &vaddr);
    if (!vmem) {
        return;
    }
    memcpy((void*)vmem, vertices, vbytes);
    xsdk_gpu_invalidate(vaddr, vbytes);
    const uint32_t ibytes = (indexCount * 2 + 3) & ~3u;
    uint32_t iaddr;
    volatile uint32_t* imem = VertexSpace(ibytes, &iaddr);
    if (!imem) {
        return;
    }
    std::vector<uint16_t> idx(indices, indices + indexCount);
    idx.resize(ibytes / 2, 0);
    WriteWords(imem, idx.data(), ibytes);
    xsdk_gpu_invalidate(iaddr, ibytes);
    xsdk_gpu_set_texture(UI_FRAG_SAMPLER_0_0, &t.texture, &t.sampler);
    xsdk_gpu_set_vertices(0, vaddr, vbytes);
    xsdk_gpu_draw_indexed(XSDK_PRIMITIVE_TRIANGLE_LIST, iaddr, indexCount, false);
    ResetDrawState();
}

int dkr360_ui_width(void) {
    return (int)X.width;
}
int dkr360_ui_height(void) {
    return (int)X.height;
}

// Golden Balloon's OpenGL backend's helpers other files call.
bool gfx_opengl_copy_captured_frame(int width, int height, uint8_t* rgb_out) {
    (void)width;
    (void)height;
    (void)rgb_out;
    return false;
}
int gfx_opengl_max_offscreen_dim(void) {
    return 2048;
}

} // extern "C"
