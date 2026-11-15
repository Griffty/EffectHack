import subprocess,json,math
from pathlib import Path
import numpy as np
R=Path('/tmp/effecthack-final'); D=Path('/home/griffty/work/coding/Projects/EffectHack/documentation')
s=json.loads((R/'capture-state.json').read_text()); ev={x['name']:x['elapsed'] for x in s['events']}; off=s['audio_offset']
# A constant level adjustment per excerpt preserves the performance dynamics.
def gain(start,dur):
 b=subprocess.check_output(['ffmpeg','-v','error','-ss',str(max(0,start-off)),'-t',str(dur),'-i',str(R/'processor-output.wav'),'-f','f32le','-ac','1','-'])
 x=np.frombuffer(b,dtype='<f4'); rms=np.sqrt(np.mean(x*x));peak=np.max(np.abs(x));return min(-22-20*np.log10(max(rms,1e-9)),-2-20*np.log10(max(peak,1e-9)))
segments=[('dry',4.55,6,6,'Your guitar. One recorded phrase.',True),('link',ev['link_start']-.25,2,3,'Paste a YouTube link.',False),('generation',ev['generation_start'],ev['generation_done']-ev['generation_start'],4,'Gemini listens, builds a tone, then refines it.  |  Sped up',False),('wet',ev['wet_start']+3.25,8,8,'Same performance. Song-inspired tone.',True),('tweak',ev['tweak_start'],5,5,'Adjust the effects. Make it yours.',True)]
files=[];subs=[];t=0
for name,start,dur,outdur,caption,audible in segments:
 cap=R/(name+'.txt');cap.write_text(caption)
 out=R/(name+'.mp4'); vf=f"setpts=(PTS-STARTPTS)*{outdur/dur},fps=30,scale=1728:972,pad=1920:1080:96:0:color=0x12171d,drawtext=fontfile=/usr/share/fonts/TTF/DejaVuSans.ttf:textfile={cap}:fontcolor=white:fontsize=30:x=(w-tw)/2:y=1008,format=yuv420p"
 cmd=['ffmpeg','-v','error','-y','-ss',str(start),'-t',str(dur),'-i',str(R/'session-recording.mkv')]
 if audible:
  cmd+=['-ss',str(max(0,start-off)),'-t',str(dur),'-i',str(R/'processor-output.wav')]
  af=f'volume={gain(start,dur):.3f}dB,afade=t=in:d=0.02,afade=t=out:st={outdur-.08}:d=0.08'
 else:
  cmd+=['-f','lavfi','-t',str(outdur),'-i','anullsrc=r=48000:cl=stereo'];af='anull'
 cmd+=['-map','0:v:0','-map','1:a:0','-vf',vf,'-af',af,'-t',str(outdur),'-c:v','libx264','-preset','veryfast','-crf','19','-threads','4','-c:a','aac','-b:a','192k',str(out)]
 subprocess.run(cmd,check=True);files.append(out);subs.append((t,t+outdur,caption));t+=outdur;print('Rendered',name,flush=True)
logo=R/'logo.mp4'
subprocess.run(['ffmpeg','-v','error','-y','-loop','1','-i',str(D/'effectshack-logo.png'),'-f','lavfi','-i','anullsrc=r=48000:cl=stereo','-t','2','-vf','scale=1350:900,pad=1920:1080:285:90:color=0x12171d,fps=30,format=yuv420p','-c:v','libx264','-preset','veryfast','-crf','19','-threads','4','-c:a','aac','-b:a','192k',str(logo)],check=True)
files.append(logo);lst=R/'concat.txt';lst.write_text(''.join("file '"+str(p)+"'\n" for p in files))
final=D/'effectshack-demo.mp4'
subprocess.run(['ffmpeg','-v','error','-y','-f','concat','-safe','0','-i',str(lst),'-c','copy','-movflags','+faststart',str(final)],check=True)
def stamp(t):
 ms=round(t*1000);return f'{ms//3600000:02d}:{ms//60000%60:02d}:{ms//1000%60:02d},{ms%1000:03d}'
(D/'effectshack-demo.srt').write_text('\n\n'.join(f'{i+1}\n{stamp(a)} --> {stamp(b)}\n{c}' for i,(a,b,c) in enumerate(subs))+'\n')
print('FINAL',final,flush=True)
