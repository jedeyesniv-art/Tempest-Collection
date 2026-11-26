#!/usr/bin/env python3
"""Conservative native cycle-use inventory. Candidates are not all timers.

Reviewed intent is explicit and tied to a body hash. A source edit makes the
review stale. This is a lexical discovery aid, not proof of transitive coverage.
"""
import argparse,hashlib,json,re,collections
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
RULES={
 'picture_or_age':r'\b(?:picture|birth|elapsed)\b|0x(?:806B991C|80110AA0|801131B8)\b',
 'timer_or_countdown':r'\b(?:timer|countdown)\b',
 'per_call_update':r'\+\+|--|\+=|\-=',
 'per_call_random':r'\b(?:next_driver_random|callback_random|preparation_rng|advance_object_random)\s*\(',
 'dynamics':r'\b(?:movement_step|approach_parameter|callback_motion|command_mul)\s*\(',
 'fixed_arithmetic':r'[-+]\s*(?:0x[0-9a-fA-F]+|[0-9]+)\b',
 'input_sampling':r'\b(?:prepare_driver_controls|sample_driver_rotary|poll_driver_menu_axis|poll_driver_menu_action)\s*\(',
 'frame_driver':r'\b(?:gameplay_frame|menu_frame|advance_driver_color|advance_driver_counters)\s*\('
}
def blank(match):return ''.join('\n' if c=='\n' else ' ' for c in match.group())
def lexical(text):return re.sub(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',blank,text)
def units(path):
 text=path.read_text();code=lexical(text)
 signature=r'(?m)(?:^|[;{}\n])\s*(?:template\s*<[^{};]*>\s*)?(?:(?:inline|static|virtual)\s+)*(?:constexpr\s+)?(?:std::array<[^;{}]*?>|[\w:<>]+)\s+(\w+)\s*\('
 for match in re.finditer(signature,code):
  if match.group(1) in {'if','for','while','switch','catch'}:continue
  head=code[match.start():match.end()].strip().split()
  if head and head[0] in {'return','else','case','goto'}:continue
  depth=1;close=match.end()
  while close<len(code) and depth:
   depth+=(code[close]=='(')-(code[close]==')');close+=1
  suffix=re.match(r'\s*(?:(?:const|override|noexcept)\s*)*\{',code[close:])
  if depth or not suffix:continue
  start=close+suffix.end()-1;depth=1;end=start+1
  while end<len(code) and depth:
   depth+=(code[end]=='{')-(code[end]=='}');end+=1
  if depth:raise ValueError(f'Unbalanced function {path}:{match.group(1)}')
  name=match.group(1);body=code[start:end]
  if name=='execute_callback':
   cases=list(re.finditer(r'\bcase\s+(0x[0-9a-fA-F]+)\s*:',body))
   for i,case in enumerate(cases):
    a=start+case.start();b=start+(cases[i+1].start() if i+1<len(cases) else len(body))
    yield name+':'+case.group(1).upper().replace('X','x'),a,b,True,text,code
  else:yield name,match.start(),end,False,text,code

def inventory(review):
 result=[];paths=sorted((ROOT/'src/t3k_runtime/game').glob('*.h'))
 for path in paths:
  relative=path.relative_to(ROOT).as_posix()
  for name,a,b,callback,text,code in units(path):
   evidence=[key for key,pattern in RULES.items() if re.search(pattern,code[a:b],re.I)]
   if callback:evidence.append('callback_dispatch_case')
   if not evidence:evidence=['no_lexical_tick_signal']
   key=relative+'::'+name;digest=hashlib.sha256(code[a:b].encode()).hexdigest();r=review.get(key,{})
   valid_review=r.get('domain') in {'gameplay','effect','shared','structural','audio','input'} and bool(r.get('decision')) and bool(r.get('intent_and_adaptation'))
   status='unreviewed' if not r or not valid_review else 'reviewed' if r.get('body_sha256')==digest else 'stale'
   result.append({'id':key,'file':relative,'function':name,'line':text.count('\n',0,a)+1,'body_sha256':digest,'evidence':evidence,'status':status,**{k:v for k,v in r.items() if k not in {'body_sha256','status','id','file','line','function','evidence'}}})
 return result

def main():
 parser=argparse.ArgumentParser();parser.add_argument('--output',type=Path);parser.add_argument('--require-reviewed',action='store_true');args=parser.parse_args()
 path=ROOT/'docs/t3k-cycle-intents.json';reviews=json.loads(path.read_text()) if path.exists() else {}
 rows=inventory(reviews)
 duplicate_symbols=sorted(k for k,v in collections.Counter(r['id'] for r in rows).items() if v>1)
 orphaned=sorted(set(reviews)-{r['id'] for r in rows})
 report={'duplicate_symbols':duplicate_symbols,'orphaned_reviews':orphaned,'scope':'all discovered semantic functions/methods and callback cases in reconstructed game headers, including units with no lexical tick signal; aliases, command data and host/renderer consumers still need transitive review','candidate_count':len(rows),'reviewed':sum(r['status']=='reviewed' for r in rows),'stale':sum(r['status']=='stale' for r in rows),'open_decisions':sum(r.get('decision')=='needs-consumers' for r in rows),'unreviewed':sum(r['status']=='unreviewed' for r in rows),'entries':rows}
 if args.output:args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(json.dumps(report,indent=2)+'\n')
 print(json.dumps({k:v for k,v in report.items() if k!='entries'}))
 return 1 if report['stale'] or orphaned or duplicate_symbols or (args.require_reviewed and (report['unreviewed'] or report['open_decisions'])) else 0
if __name__=='__main__':raise SystemExit(main())
