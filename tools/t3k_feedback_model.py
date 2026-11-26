#!/usr/bin/env python3
"""Asset-free feedback experiment, not a model of all T3K GPU operations.
Compare elapsed-time trail properties without demanding identical pixels.
"""
import argparse,json,math
from pathlib import Path
WIDTH=1024

def run(rate,policy,fade=.80,opacity=.35,speed=120.0,seconds=2.0):
 dt=24/rate;pixels=[0.0]*WIDTH
 f=fade if policy=='naive' else fade**dt
 a=opacity if policy=='naive' else 1-(1-opacity)**dt
 for i in range(round(seconds*rate)):
  center=80+speed*(i+1)/rate
  pixels=[p*f for p in pixels]
  for x in range(max(0,int(center)-16),min(WIDTH,int(center)+17)):
   strength=a*math.exp(-.5*((x-center)/3)**2)
   pixels[x]+=strength*(1-pixels[x])
 center=80+speed*seconds
 energy=sum(pixels);lags=[max(0,center-x) for x in range(WIDTH)]
 mean=sum(p*l for p,l in zip(pixels,lags))/energy
 spread=math.sqrt(sum(p*(l-mean)**2 for p,l in zip(pixels,lags))/energy)
 cumulative=0;tail=0
 for lag,power in sorted(zip(lags,pixels)):
  cumulative+=power
  if cumulative>=energy*.95:tail=lag;break
 return {'energy':energy,'mean_trail_pixels':mean,'trail_spread_pixels':spread,'trail_95_pixels':tail,'peak':max(pixels)},pixels

def main():
 parser=argparse.ArgumentParser();parser.add_argument('--output',type=Path);args=parser.parse_args()
 results={};images={}
 for name,rate,policy in [('reference_24',24,'time_scaled'),('naive_120',120,'naive'),('time_scaled_120',120,'time_scaled')]:results[name],images[name]=run(rate,policy)
 ref=results['reference_24']
 results['relative_to_reference']={name:{key:value/ref[key] for key,value in result.items()} for name,result in list(results.items()) if name!='reference_24'}
 results['scope']='Synthetic 1D darkening feedback plus alpha-blended moving Gaussian. Not a T3K renderer validation or approved tuning.'
 results['policy']='fade**dt, opacity=1-(1-opacity)**dt; moving source evaluated at each real step'
 results['important_limit']='Independent fade/opacity scaling does not exactly preserve coupled fixed-point operators, deposits, ripple, clipping, or layer order. Measure game fixtures separately.'
 if args.output:
  args.output.mkdir(parents=True,exist_ok=True);(args.output/'feedback-model.json').write_text(json.dumps(results,indent=2)+'\n')
  with (args.output/'feedback-profile.csv').open('w') as out:
   out.write('pixel,reference_24,naive_120,time_scaled_120\n')
   for x in range(WIDTH):out.write(str(x)+','+','.join(str(images[k][x]) for k in images)+'\n')
 print(json.dumps(results,indent=2))
 return 0
if __name__=='__main__':raise SystemExit(main())
