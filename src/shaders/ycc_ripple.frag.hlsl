// Tempest 3000 native renderer: the ripple feedback (0000000F, module 806B0B30; tools/t3k_ripplefit.py) on a YCrCb
// picture. Output game pixel (x, y): a = base_a + x slope_a, b = base_b + y slope_b + a (the module's 16.16 sums,
// bits 16-21 as the index); dx = T[a], dy = T[b] with T[i] = sin(2 pi (i + 1) / 64) px; the previous picture sampled
// bilinearly at (x + dx, y + dy), then faded toward the colour: out = s + (colour - s) fade.

cbuffer Params : register(b0, space3) {
  float4 ripple;   // base_a, slope_a (w4 / 65536), base_b, slope_b (w5 / 65536)
  float4 colour;   // w2 (YCrCb / 255), fade (w7) in w
  float4 view;     // the target's extent in game pixels: x0, y0, width, height (0, 0, 360, 240 at 4:3)
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

float tab(float a) {
  float i = fmod(floor(a), 64.0);
  if (i < 0.0) i += 64.0;
  return sin(6.283185307 * (i + 1.0) / 64.0);
}

PSOutput main(PSInput input) {
  PSOutput output;
  float x = view.x + input.v_uv.x * view.z - 0.5, y = view.y + input.v_uv.y * view.w - 0.5;   // game pixel centres
  float a = ripple.x + x * ripple.y;
  float b = ripple.z + y * ripple.w + a;
  float2 s = float2(x + tab(a), y + tab(b));
  float3 c = u_texture.SampleLevel(u_sampler, (s + 0.5 - view.xy) / view.zw, 0).rgb;
  output.o_color = float4(saturate(c + (colour.rgb - c) * colour.w), 1.0);
  return output;
}
