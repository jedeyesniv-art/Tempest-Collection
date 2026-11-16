// Tempest 3000 native renderer: a YCrCb picture (R = Y, G = Cr, B = Cb, bytes / 255) -> RGB as Nuance shows it
// (BT.601, video range; the same as t3knative::YccToRgb).

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
  float3 s = u_texture.SampleLevel(u_sampler, input.v_uv, 0).rgb * 255.0;
  float y = saturate((s.x - 16.0) / 219.0);
  float cr = saturate((s.y - 16.0) / 224.0) - 0.5;
  float cb = saturate((s.z - 16.0) / 224.0) - 0.5;
  float3 rgb = float3(y + 1.402 * cr, y - 0.34413 * cb - 0.714136 * cr, y + 1.772 * cb);
  output.o_color = float4(saturate(rgb), 1.0) * input.v_color;
  return output;
}
