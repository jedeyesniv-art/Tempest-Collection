// Original radial point coverage; alternate grid rows use constant blend inside the disc.
struct PSInput{float4 color:TEXCOORD0;float2 pos:TEXCOORD1;};
float4 main(PSInput input):SV_Target{
 bool flat=input.pos.x>2;float2 uv=input.pos-float2(flat?4:0,0);
 float r2=dot(uv*2-1,uv*2-1);if(r2>=1)discard;
 return float4(input.color.rgb,input.color.a*(flat?1:1-r2));
}
