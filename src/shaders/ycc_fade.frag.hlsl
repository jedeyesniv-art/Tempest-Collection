// Tempest 3000 native renderer: the zoom feedback (00300004) on a YCrCb picture (R = Y, G = Cr, B = Cb, bytes / 255).
// The module scales every component about its neutral value and clips to a byte (tools/t3k_fadefit.py):
// Y' = clip(16 + (Y - 16) f), C' = clip(128 + (C - 128) f) - brightening keeps (and deepens) the colour.

cbuffer Params : register(b0, space3) {
  float4 fade;   // x = f
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
  const float3 n = float3(16.0, 128.0, 128.0) / 255.0;
  float3 s = u_texture.SampleLevel(u_sampler, input.v_uv, 0).rgb;
  output.o_color = float4(saturate(n + (s - n) * fade.x), 1.0);
  return output;
}
