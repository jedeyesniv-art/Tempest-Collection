// Tempest 3000 native renderer: the zoom feedback (00300004) on a YCrCb picture (R = Y, G = Cr, B = Cb, bytes / 255).
// The module scales every component about its neutral value (w11 = 10 80 80) and stores a byte:
// Y' = 16 + (Y - 16) f, C' = 128 + (C - 128) f, clipped.
//
// Rounding (the orange-patch fix, 2026-11-17; tools/t3k_warpchain.py): the hardware floors once per game picture,
// bit-exact. Floor around neutral is one-sided - a chroma one below 128 runs away under a brightening fade (127 ->
// 126 -> 124 ...) while one above stays - so any +-1 difference of ours seeded orange patches in the warp where the
// original stays pink. Truncating toward neutral once per picture keeps small deviations still in both directions
// and follows the original's colours (chain sim: orange 0.1-0.7% vs floor's 80%).
//   mode 0: in between a picture's step (float feedback texture): exact, no rounding
//   mode 1: the present that completes a picture's step: truncate toward neutral when brightening (f > 1); floor
//           (the hardware, bit-exact) when darkening, which is stable
//   mode 2: 8-bit feedback texture, in between: round to nearest (unbiased)

cbuffer Params : register(b0, space3) {
  float4 fade;   // x = f, y = mode
};

Texture2D u_texture : register(t0, space2);  // the previous picture
SamplerState u_sampler : register(s0, space2);

struct PSInput {
  float4 v_color : TEXCOORD0;
  float2 v_uv : TEXCOORD1;
};

struct PSOutput {
  float4 o_color : SV_Target;
};

PSOutput main(PSInput input) {
  PSOutput output;
  const float3 n = float3(16.0, 128.0, 128.0);
  float3 s = u_texture.SampleLevel(u_sampler, input.v_uv, 0).rgb * 255.0;
  float3 d = (s - n) * fade.x;
  if (fade.y > 2.5) {
    d = floor(d + 1e-3);   // mode 3: the old floor at every draw (A/B tests: T3K_FADE_FLOOR=1)
  } else if (fade.y > 1.5) {
    d = floor(d + 0.5);
  } else if (fade.y > 0.5) {
    // brightening: toward neutral; darkening (play trails, f < 1) is stable under the hardware's own floor
    d = fade.x > 1.0 ? trunc(d + sign(d) * 1e-3) : floor(d + 1e-3);   // (1e-3: products that should be whole)
  }
  output.o_color = float4(saturate((n + d) / 255.0), 1.0);
  return output;
}
