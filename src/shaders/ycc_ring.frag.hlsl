// Generate the reference ring image on its original 2x sampling grid.
cbuffer Params : register(b0, space3) { float4 bounds; float4 shape; float4 mapping; float4 centre; };
Texture2D u_texture : register(t0, space2);
SamplerState u_sampler : register(s0, space2);
struct PSInput { float4 v_color:TEXCOORD0; float2 v_uv:TEXCOORD1; };
float4 main(PSInput input):SV_Target {
  float2 p=bounds.xy+input.v_uv*bounds.zw;
  float col=-(abs(p.x-centre.x)+0.5), row=p.y-0.5-centre.y;
  float d=col*col*shape.x+row*row*shape.y;
  float ev=shape.z-abs(d-(1-shape.z));
  if(d<=1-2*shape.z || ev<=0)return 0;
  float a=saturate(ev*shape.w/16384.0);
  float2 uv=abs(mapping.xy*float2(col,row)+mapping.zw)/65536.0;
  int2 at=int2(floor(uv)); float2 f=frac(uv);
  float3 c00=u_texture.Load(int3(at&int2(15,7),0)).rgb;
  float3 c10=u_texture.Load(int3((at+int2(1,0))&int2(15,7),0)).rgb;
  float3 c01=u_texture.Load(int3((at+int2(0,1))&int2(15,7),0)).rgb;
  float3 c11=u_texture.Load(int3((at+1)&int2(15,7),0)).rgb;
  float3 c=floor(lerp(lerp(c00,c10,f.x),lerp(c01,c11,f.x),f.y)*255+0.5)/255;
  return floor(float4(c*a,a)*255+0.5)/255;
}
