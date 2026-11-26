cbuffer Params:register(b0,space3){float4 params[7];};
Texture2D atlas:register(t0,space2);SamplerState sampler0:register(s0,space2);
struct PSInput{float4 color:TEXCOORD0;float2 uv:TEXCOORD1;};
float P(int i){return params[i/4][i%4];}
float steps(float n,float a,float d){return (n*a+0.5*n*(n-1)*d)/256;}
float4 lookup(float2 uv,int offset){
 int2 at=int2(floor(uv));float2 f=frac(uv);
 float4 a=atlas.Load(int3((at&15)+int2(offset,0),0));
 float4 b=atlas.Load(int3(((at+int2(1,0))&15)+int2(offset,0),0));
 float4 c=atlas.Load(int3(((at+int2(0,1))&15)+int2(offset,0),0));
 float4 d=atlas.Load(int3(((at+1)&15)+int2(offset,0),0));
 return lerp(lerp(a,b,f.x),lerp(c,d,f.x),f.y)*255;
}
float4 imageSample(float2 uv){
 float2 ij=clamp(uv*float2(P(20),P(21))-0.5,0,float2(P(20),P(21))-1);
 float i=ij.x,j=ij.y;
 float gu=P(0)+steps(j,P(12),P(14))+steps(i,P(4),P(6));
 float gv=P(1)+steps(j,P(13),P(15))+steps(i,P(5),P(7));
 float4 glyph=lookup(float2(gu,gv)/65536,0);
 float tu=P(2)+steps(j,P(16),P(18))+steps(i,P(8),P(10))+P(22)*(glyph.g-128)*4;
 float tv=P(3)+steps(j,P(17),P(19))+steps(i,P(9),P(11))+P(23)*(glyph.b-128)*4;
 float3 c=floor(clamp(lookup(float2(tu,tv)/65536,16).rgb,0,255)+0.5)/255;
 float a=saturate(glyph.a*P(24));return floor(float4(c*a,a)*255+0.5)/255;
}

float4 main(PSInput input):SV_Target{
 float2 size=float2(P(25),P(26)),pos=input.uv*size-0.5,at=floor(pos),f=frac(pos);
 float2 lo=clamp(at,0,size-1),hi=clamp(at+1,0,size-1);
 float4 a=imageSample((lo+0.5)/size),b=imageSample((float2(hi.x,lo.y)+0.5)/size);
 float4 c=imageSample((float2(lo.x,hi.y)+0.5)/size),d=imageSample((hi+0.5)/size);
 return lerp(lerp(a,b,f.x),lerp(c,d,f.x),f.y);
}
