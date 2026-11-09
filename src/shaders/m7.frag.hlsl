// The bonus round's mode7 ceiling / ground (remaster DrawM7): a texture row a scanline, dimmed with distance the way
// the blitter's SRCSHADE does to a CRY pixel (the brightness less the drop, the colour kept; the drop arrives in the
// vertex colour: 1 - drop). Far lines squeeze many texels into a pixel: four taps across the pixel's footprint
// along the row average them (the plain linear filter shimmered).

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
  float2 d = ddx(input.v_uv);
  float3 c = 0;
  [unroll] for (int k = 0; k < 4; ++k) c += u_texture.Sample(u_sampler, input.v_uv + d * ((k - 1.5) / 4.0)).rgb;
  c *= 0.25;
  float drop = 1.0 - input.v_color.r;
  float m = max(c.r, max(c.g, c.b));
  float f = m > 0.0001 ? max(m - drop, 0.0) / m : 0.0;
  output.o_color = float4(c * f, 1.0);
  return output;
}
