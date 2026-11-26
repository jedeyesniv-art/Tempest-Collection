"""Encode equal-duration blur captures and measure old-cadence phase variation.
The 24 Hz column is original-rate native rendering, not a hardware recording.
"""
import csv,json,sys
from pathlib import Path
from fractions import Fraction
root=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(root/'research/tempest-profile/deps/reference-video'))
import av,numpy as np
from PIL import Image,ImageDraw
base=root/'research/tempest-profile/runs'
out=root/'deliverables/tempest-motion-blur';out.mkdir(parents=True,exist_ok=True)
arrays={}
for name,count in [('reference',126),('pinned',630),('integrated',630)]:
 p=base/f'motion-blur-{name}/opening.rgba'
 assert p.stat().st_size==count*240*360*4
 arrays[name]=np.memmap(p,dtype=np.uint8,mode='r',shape=(count,240,360,4))
metrics={}
for name in ('pinned','integrated'):
 rgb=arrays[name][:310,:,:,:3].astype(np.float32)
 # Movement phase only; normalize each five-sample block to remove slow
 # variations in the scene's speed/brightness before comparing phase means.
 delta=np.abs(rgb[1:]-rgb[:-1]).mean(axis=(1,2,3))
 blocks=delta[49:299].reshape(-1,5);normalized=blocks/np.maximum(blocks.mean(axis=1,keepdims=True),1e-6)
 phase=normalized.mean(axis=0)
 metrics[name]={'phase_normalized_changes':phase.tolist(),'phase_span':float(phase.max()-phase.min()),'movement_mean_rgb':float(rgb.mean())}
for fps in (60,120):
 path=out/f'opening-comparison-{fps}.mp4';container=av.open(str(path),'w');stream=container.add_stream('libx264',rate=fps)
 stream.width=1080;stream.height=264;stream.pix_fmt='yuv420p';stream.options={'crf':'14','preset':'medium'}
 for sample in range(0,630,120//fps):
  image=Image.new('RGB',(1080,264),'black');d=ImageDraw.Draw(image)
  for col,(name,label) in enumerate([('reference','Original-rate rendering: 24 Hz'),('pinned','Previous 120 Hz: pinned history'),('integrated','New 120 Hz: elapsed-time accumulation')]):
   i=sample//5 if name=='reference' else sample
   image.paste(Image.fromarray(np.asarray(arrays[name][i,:,:,:3])),(col*360,24));d.text((col*360+5,5),label,fill='white')
  frame=av.VideoFrame.from_image(image)
  for packet in stream.encode(frame):container.mux(packet)
 for packet in stream.encode():container.mux(packet)
 container.close();print(path,flush=True)
metrics['meaning']='Normal-speed 5.25 s sequences. Reference pictures are held, not interpolated, in the 120 Hz comparison. Phase variation is a cadence diagnostic, not a perceptual quality score.'
(out/'opening-motion-metrics.json').write_text(json.dumps(metrics,indent=2)+'\n');print(json.dumps(metrics,indent=2))

# A separate normal-speed score/level-clear comparison, with enlarged actual
# framebuffer crops so ghost separation can be followed from frame to frame.
transition={}
for name in ('clocked','integrated'):
 path=base/f'motion-blur-{name}/transition.rgba'
 assert path.stat().st_size==600*240*360*4
 transition[name]=np.memmap(path,dtype=np.uint8,mode='r',shape=(600,240,360,4))
for fps in (60,120):
 path=out/f'transition-comparison-{fps}.mp4';container=av.open(str(path),'w');stream=container.add_stream('libx264',rate=fps)
 stream.width=720;stream.height=464;stream.pix_fmt='yuv420p';stream.options={'crf':'14','preset':'medium'}
 for sample in range(0,600,120//fps):
  image=Image.new('RGB',(720,464),'black');d=ImageDraw.Draw(image)
  for col,(name,label) in enumerate([('clocked','Previous: clocked echo deposits'),('integrated','New: elapsed-time accumulation')]):
   frame=Image.fromarray(np.asarray(transition[name][sample,:,:,:3]));image.paste(frame,(col*360,24));d.text((col*360+5,5),label,fill='white')
   image.paste(frame.crop((0,0,120,60)).resize((360,180),Image.Resampling.NEAREST),(col*360,284));d.text((col*360+5,267),'Score area (3x, same captured picture)',fill='white')
  for packet in stream.encode(av.VideoFrame.from_image(image)):container.mux(packet)
 for packet in stream.encode():container.mux(packet)
 container.close();print(path,flush=True)
checks={}
for name in ('frames','values','effects'):
 a=(base/f'motion-blur-clocked/trace.{name}.csv').read_bytes();b=(base/f'motion-blur-integrated/trace.{name}.csv').read_bytes();checks[name+'_identical']=a==b
(out/'transition-state-comparison.json').write_text(json.dumps(checks,indent=2)+'\n');print(checks)

travelMetrics={}
for name,data in transition.items():
 roi=data[:269,:80,:160,:3].astype(np.float32)
 delta=np.abs(roi[1:]-roi[:-1]).mean(axis=(1,2,3))
 blocks=delta[11:251].reshape(-1,6);blocks=blocks[blocks.mean(axis=1)>1e-6]
 norm=blocks/blocks.mean(axis=1,keepdims=True);phase=norm.mean(axis=0)
 travelMetrics[name]={'phase_normalized_score_changes':phase.tolist(),'phase_span':float(phase.max()-phase.min())}
(out/'score-motion-metrics.json').write_text(json.dumps(travelMetrics,indent=2)+'\n');print(json.dumps(travelMetrics,indent=2))
