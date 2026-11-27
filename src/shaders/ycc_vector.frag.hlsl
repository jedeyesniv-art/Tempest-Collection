// Continuous stroke coverage with circular end caps, evaluated at original pixel corners.
cbuffer Params:register(b0,space3){float4 stroke[256];};
struct PSInput{float4 color:TEXCOORD0;float2 pos:TEXCOORD1;};
float4 main(PSInput input):SV_Target{
 int at=int(input.color.r*255+.5)*4;float4 ends=stroke[at];float2 d=ends.zw-ends.xy,p=input.pos*65536-32768;
 float t=dot(d,d)>1e-8?saturate(dot(p-ends.xy,d)/dot(d,d)):0;
 float coverage=saturate(1-length(p-lerp(ends.xy,ends.zw,t))/stroke[at+3].x);
 float colourT=t;
 if(stroke[at+3].y>0){
  int axis=abs(d.x)>=abs(d.y)?0:1;float radius=stroke[at+3].x;
  colourT=saturate((p[axis]-min(ends[axis],ends[axis+2])+radius)/(floor(abs(d[axis]))+1+2*radius));
  if(d[axis]<0)colourT=1-colourT;
 }
 float4 colour=lerp(stroke[at+1],stroke[at+2],colourT);
 return float4(colour.rgb,colour.a*coverage);
}
