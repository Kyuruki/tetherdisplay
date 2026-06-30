// Metal shaders for NV12 video render (M3). Full-screen triangle + BT.709 VIDEO-RANGE YCbCr->RGB.
// The video-range coefficients (luma scale 1.164384 = 255/219, etc.) are what keep the output from
// looking washed-out/oversaturated (§7.3). Matches the NV12 the decoder requests
// (kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange).
#include <metal_stdlib>
using namespace metal;

struct VOut {
    float4 pos [[position]];
    float2 uv;
};

// Oversized full-screen triangle generated from the vertex id (no vertex buffer needed).
vertex VOut vtx(uint vid [[vertex_id]]) {
    float2 p = float2((vid << 1) & 2, vid & 2);  // (0,0), (2,0), (0,2)
    VOut o;
    o.pos = float4(p * 2.0 - 1.0, 0.0, 1.0);
    o.uv = float2(p.x, 1.0 - p.y);               // flip V so the image is upright
    return o;
}

// Column-major: M * v = col0*v.x + col1*v.y + col2*v.z  (columns are Y, Cb, Cr).
constant float3x3 kYCbCrToRGB709Video = float3x3(
    float3(1.164384,  1.164384,  1.164384),   // Y
    float3(0.0,      -0.213249,  2.112402),   // Cb
    float3(1.792741, -0.532909,  0.0));       // Cr

fragment float4 frag(VOut in [[stage_in]],
                     texture2d<float> yTex [[texture(0)]],
                     texture2d<float> cbcrTex [[texture(1)]]) {
    constexpr sampler s(filter::linear, address::clamp_to_edge);
    float  y    = yTex.sample(s, in.uv).r;
    float2 cbcr = cbcrTex.sample(s, in.uv).rg;
    float3 ycbcr = float3(y, cbcr) - float3(16.0 / 255.0, 128.0 / 255.0, 128.0 / 255.0);
    return float4(kYCbCrToRGB709Video * ycbcr, 1.0);
}
