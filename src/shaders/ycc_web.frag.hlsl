// The original left/right scanline edge walk, evaluated at the fragment.
cbuffer Params:register(b0,space3){float4 q[64];float4 options;};
Texture2D tex:register(t0,space2);SamplerState sampler0:register(s0,space2);
struct PSInput{float4 color:TEXCOORD0;float2 pos:TEXCOORD1;};
float4 main(PSInput input):SV_Target{
 float2 location=input.pos*float2(360,240);
 int base=int(input.color.r*255+0.5)*4;
 float3 left=float3(1e20,0,0),right=float3(-1e20,0,0);int count=0;
 [unroll]for(int k=0;k<4;++k){float4 a=q[base+k],b=q[base+(k+1)%4];
  if(a.y==b.y||location.y<min(a.y,b.y)||location.y>max(a.y,b.y))continue;
  float f=(location.y-a.y)/(b.y-a.y);float3 hit=lerp(a.xzw,b.xzw,f);
  if(hit.x<left.x)left=hit;if(hit.x>right.x)right=hit;++count;
 }
 if(count<2)discard;
 float f=right.x-left.x>1e-4?saturate((location.x-left.x)/(right.x-left.x)):0;
 float2 uv=lerp(left.yz,right.yz,f);float alpha=saturate(1-uv.y/32);
 uv+=options.yz+float2(0,options.x);int2 at=int2(floor(uv));float2 fracuv=frac(uv);
 float3 a=tex.Load(int3(at&15,0)).rgb,b=tex.Load(int3((at+int2(1,0))&15,0)).rgb;
 float3 c=tex.Load(int3((at+int2(0,1))&15,0)).rgb,d=tex.Load(int3((at+1)&15,0)).rgb;
 return float4(lerp(lerp(a,b,fracuv.x),lerp(c,d,fracuv.x),fracuv.y),alpha);
}
