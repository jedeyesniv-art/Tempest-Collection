// The native 'scar' feedback (antelope.gas scale and rotate with SRCSHADE, see remaster ScarMap): the last frame
// drawn through the map with its intensity lowered as the blitter does to a CRY pixel: the brightness (here the
// brightest channel) less `dim`, the colour kept (a CRY pixel is colour x intensity).
// The melt (owner 2026-11-09: "the original blobs out and distorts, ours just zooms" - the original's nearest-pixel
// resampling smears it blockily): a noise warp that changes every step, an uneven downward drip, and bright parts
// spreading into their neighbours (a soft dilation).

cbuffer Params : register(b0, space3) {
  float4 p;   // x: intensity drop (0..1 of full), y: wobble (uv), z: blob (0..1), w: step (the noise seed)
  float4 q;   // x y: a texel (uv), z: drip (uv), w: window aspect (w / h)
};

Texture2D u_texture : register(t0, space2);
SamplerState u_sampler : register(s0, space2);

// SDL_shadercross names every vertex output TEXCOORDn (see crt.frag.hlsl).
struct PSInput {
  float4 v_color : TEXCOORD0;
  float2 v_uv : TEXCOORD1;
};

struct PSOutput {
  float4 o_color : SV_Target;
};

float Hash(float2 v) { return frac(sin(dot(v, float2(127.1, 311.7))) * 43758.5453); }

float Noise(float2 v) {   // smooth value noise, 0..1
  float2 i = floor(v), f = frac(v);
  f = f * f * (3.0 - 2.0 * f);
  float a = Hash(i), b = Hash(i + float2(1, 0)), c = Hash(i + float2(0, 1)), d = Hash(i + float2(1, 1));
  return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);
}

PSOutput main(PSInput input) {
  PSOutput output;
  float2 uv = input.v_uv;
  if (p.y > 0.0 || q.z > 0.0) {
    float2 a = float2(q.w, 1.0);   // noise cells square on screen
    float s = p.w * 1.37;
    float2 w = float2(Noise(uv * a * 5.0 + float2(s, 0.0)), Noise(uv * a * 5.0 + float2(0.0, s + 17.0))) - 0.5;
    uv += w * p.y;
    // drip: sample from above by an amount that varies across the screen (things run down unevenly)
    uv.y -= q.z * Noise(float2(uv.x * q.w * 9.0, s * 0.5));
  }
  float3 c = u_texture.Sample(u_sampler, uv).rgb;
  if (p.z > 0.0) {   // blob: the brightest of a small ring around, mixed in
    float3 m = c;
    [unroll] for (int k = 0; k < 8; ++k) {
      float an = k * 0.785398;
      float2 o = float2(cos(an), sin(an)) * q.xy * 2.5;
      m = max(m, u_texture.Sample(u_sampler, uv + o).rgb);
    }
    c = lerp(c, m, p.z);
  }
  float mx = max(c.r, max(c.g, c.b));
  float k = mx > 0.0001 ? max(mx - p.x, 0.0) / mx : 0.0;
  output.o_color = float4(c * k, 1.0);
  return output;
}
