// Tempest 3000 native renderer: an RGB picture (an emulated one the native feedback continues from) -> YCrCb
// (R = Y, G = Cr, B = Cb, bytes / 255): the inverse of ycc_out (BT.601, video range).

Texture2D u_texture : register(t0, space2);
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
  float3 c = u_texture.SampleLevel(u_sampler, input.v_uv, 0).rgb;
  float y = 0.299 * c.r + 0.587 * c.g + 0.114 * c.b;
  float cb = (c.b - y) / 1.772, cr = (c.r - y) / 1.402;
  float3 ycc = float3(16.0 + 219.0 * y, 16.0 + 224.0 * (cr + 0.5), 16.0 + 224.0 * (cb + 0.5)) / 255.0;
  output.o_color = float4(saturate(ycc), 1.0);
  return output;
}
