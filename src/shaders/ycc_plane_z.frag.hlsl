// Module 0 mode 5: nearest procedural texture, signed sampled Z blend.
cbuffer Params:register(b0,space3){float4 rect;float4 base;float4 advance;float4 curve;};
Texture2D tex:register(t0,space2);SamplerState sampler0:register(s0,space2);
struct PSInput{float4 color:TEXCOORD0;float2 pos:TEXCOORD1;};
float4 main(PSInput input):SV_Target{
 float2 p=input.pos*65536-32768-rect.xy-.5;float i=p.x,j=p.y;
 float2 row=base.xy+j*advance.xy+curve.xy*j*(j-1)*.5;
 float2 delta=base.zw+j*curve.zw;
 float k=floor(max(i,0)/64),n=i-64*k;
 // Mode 5 undoes its prefetch at each chunk boundary.
 row+=64*k*delta+advance.zw*(64*64*k*(k-1)*.5+64*63*k*.5);
 delta+=64*k*advance.zw;
 float2 uv=row+n*delta+advance.zw*n*(n-1)*.5;
 float4 c=tex.Load(int3(int2(floor(uv))&15,0));int z=int(round(c.a*255));if(z>=128)z-=256;
 return float4(c.rgb,z*(1.0/256.0)*(8191.0/8192.0));
}
