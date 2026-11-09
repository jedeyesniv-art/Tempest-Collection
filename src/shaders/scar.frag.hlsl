// The native 'scar' feedback (antelope.gas scale and rotate with SRCSHADE, see remaster ScarMap): the last frame
// drawn through the map with its intensity lowered as the blitter does to a CRY pixel: the brightness (here the
// brightest channel) less `dim`, the colour kept (a CRY pixel is colour x intensity).

cbuffer Params : register(b0, space3) {
  float4 p;   // x: intensity drop (0..1 of full), y z w: 0
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

PSOutput main(PSInput input) {
  PSOutput output;
  float3 c = u_texture.Sample(u_sampler, input.v_uv).rgb;
  float m = max(c.r, max(c.g, c.b));
  float k = m > 0.0001 ? max(m - p.x, 0.0) / m : 0.0;
  output.o_color = float4(c * k, 1.0);
  return output;
}
