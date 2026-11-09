// The native 'scar' feedback (antelope.gas scale and rotate with SRCSHADE, see remaster ScarMap): the last frame
// drawn through the map with its intensity lowered as the blitter does to a CRY pixel: the brightness (here the
// brightest channel) less `dim`, the colour kept (a CRY pixel is colour x intensity).
// The melt (owner 2026-11-09: "the original blobs out and distorts, ours just zooms" - the original's nearest-pixel
// resampling smears it blockily): a noise warp that changes every step, an uneven downward drip, and bright parts
// spreading into their neighbours (a soft dilation). Then (owner: "jaggedly spinny melty"): a turn every step about
// the screen's centre, and broad patches snapping to square cells (a mosaic that builds up over the steps).

cbuffer Params : register(b0, space3) {
  float4 p;   // x: intensity drop (0..1 of full), y: wobble (uv), z: blob (0..1), w: step (the noise seed)
  float4 q;   // x y: a texel (uv), z: drip (uv), w: window aspect (w / h)
  float4 r;   // x: spin (radians a step), y: mosaic amount (0..1), z: mosaic cell (uv, across the height), w: 0
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
  if (r.x != 0.0) {   // turn about the centre (aspect-correct)
    float2 d = (uv - 0.5) * float2(q.w, 1.0);
    float cs = cos(r.x), sn = sin(r.x);
    d = float2(cs * d.x - sn * d.y, sn * d.x + cs * d.y);
    uv = d / float2(q.w, 1.0) + 0.5;
  }
  float2 muv = uv;
  float mos = 0.0;
  if (r.y > 0.0 && r.z > 0.0) {   // mosaic in broad patches (a slowly drifting noise field picks them)
    float2 a = float2(q.w, 1.0);
    float patch = Noise(uv * a * 2.5 + float2(p.w * 0.08, 31.0));
    mos = saturate((r.y - patch) * 8.0);   // soft-edged patches
    float2 cell = float2(r.z / q.w, r.z);
    muv = (floor(uv / cell) + 0.5) * cell;
  }
  if (p.y > 0.0 || q.z > 0.0) {
    float2 a = float2(q.w, 1.0);   // noise cells square on screen
    float s = p.w * 1.37;
    float2 w = float2(Noise(uv * a * 5.0 + float2(s, 0.0)), Noise(uv * a * 5.0 + float2(0.0, s + 17.0))) - 0.5;
    uv += w * p.y;
    // drip: sample from above by an amount that varies across the screen (things run down unevenly)
    uv.y -= q.z * Noise(float2(uv.x * q.w * 9.0, s * 0.5));
  }
  float3 c = u_texture.Sample(u_sampler, uv).rgb;
  if (mos > 0.0) c = lerp(c, u_texture.Sample(u_sampler, muv + (uv - input.v_uv)).rgb, mos * 0.6);
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
