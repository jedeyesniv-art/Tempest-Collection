// Coupled paint/feedback temporal integration in native YCrCb data space.
// A full frozen-pose update is H' = A*W(H)+B, where A=f*(1-alpha)
// and B=paintPremultiplied+(1-alpha)*(1-f)*neutral.
// Fractional updates use A^dt and B*(A^dt-1)/(A-1).
cbuffer Params : register(b0, space3) {
  float4 clock; // elapsed authored ticks, feedback retention/gain, native brightening threshold, unused
  float4 neutral;
};
Texture2D history : register(t0, space2);
SamplerState historySampler : register(s0, space2);
Texture2D paint : register(t1, space2);
SamplerState paintSampler : register(s1, space2);
struct PSInput { float4 color:TEXCOORD0; float2 uv:TEXCOORD1; };
float4 main(PSInput input):SV_Target {
  float3 h=history.SampleLevel(historySampler,input.uv,0).rgb;
  float4 p=paint.SampleLevel(paintSampler,input.uv,0);
  float3 difference=h-neutral.rgb;
  // Keep storage noise around the exact neutral colour from becoming pigment.
  h=neutral.rgb+float3(abs(difference.x)<1.0/1020?0:difference.x,
                       abs(difference.y)<1.0/1020?0:difference.y,
                       abs(difference.z)<1.0/1020?0:difference.z);
  float3 gain=clock.y;
  if(clock.z>0.5){
    // The reference renderer truncates brightening byte deltas toward neutral.
    // Preserve its small-signal growth threshold, without quantizing the whole
    // fine picture or postponing paint deposits to original-frame boundaries.
    float3 d=abs(round((h-neutral.rgb)*255));
    gain=max(1,trunc(d*clock.y+0.001)/max(d,1));
  }
  // Signed vector weights can paint away from a colour. Negative alpha
  // therefore has uncovered > 1; clamping it would discard the gain term.
  // Coverage beyond one still has no real-valued fractional affine power.
  float uncovered=max(0,1-p.a);
  float3 a=max(0,gain*uncovered),ah=pow(a,clock.x);
  float3 q=float3(abs(a.x-1)<0.0001?clock.x:(ah.x-1)/(a.x-1),
                   abs(a.y-1)<0.0001?clock.x:(ah.y-1)/(a.y-1),
                   abs(a.z-1)<0.0001?clock.x:(ah.z-1)/(a.z-1));
  float3 b=p.rgb+uncovered*(1-gain)*neutral.rgb;
  return float4(saturate(ah*h+q*b),1);
}
