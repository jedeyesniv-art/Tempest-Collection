import importlib.util,json,tempfile,unittest
from pathlib import Path
from unittest.mock import patch
MODULE=Path(__file__).resolve().parents[1]/'tools/t3k_timing_inventory.py'
spec=importlib.util.spec_from_file_location('timing_inventory',MODULE);timing=importlib.util.module_from_spec(spec);spec.loader.exec_module(timing)
class TimingInventoryTests(unittest.TestCase):
 def test_virtual_defaults_and_combined_qualifiers_are_discovered(self):
  with tempfile.TemporaryDirectory() as d:
   path=Path(d)/'sample.h';path.write_text('struct Services{virtual unsigned rate()const{return 1;} virtual bool pure()=0; static inline bool reset(){return true;} };')
   self.assertEqual([u[0] for u in timing.units(path)],['rate','reset'])
 def test_methods_and_callback_aliases_are_discovered(self):
  with tempfile.TemporaryDirectory() as d:
   path=Path(d)/'sample.h';path.write_text('struct Animation { bool run(){ ++phase; return true; } };\ninline bool execute_callback(int x){switch(x){case 0x8001:case 0x8002:return true;}return false;}')
   names=[u[0] for u in timing.units(path)]
   self.assertEqual(names,['run','execute_callback:0x8001','execute_callback:0x8002'])
 def test_implicit_emitter_and_default_arguments_are_not_omitted(self):
  with tempfile.TemporaryDirectory() as d:
   root=Path(d);folder=root/'src/t3k_runtime/game';folder.mkdir(parents=True)
   (folder/'sample.h').write_text('struct Choice{}; inline Choice select(Choice input = {}){return input;} inline bool emit(){return send_event();}')
   with patch.object(timing,'ROOT',root):
    rows=timing.inventory({})
    self.assertEqual({r['function'] for r in rows},{'select','emit'})
    self.assertTrue(all(r['evidence']==['no_lexical_tick_signal'] for r in rows))
 def test_comments_are_not_cycle_evidence(self):
  text='// picture++\n/* countdown-- */\nbool stable(){const char* s="birth timer";return true;}'
  code=timing.lexical(text)
  self.assertEqual(code.count('\n'),text.count('\n'))
  self.assertNotIn('picture',code);self.assertNotIn('birth',code)
 def test_review_becomes_stale_on_semantic_edit(self):
  with tempfile.TemporaryDirectory() as d:
   root=Path(d);folder=root/'src/t3k_runtime/game';folder.mkdir(parents=True)
   path=folder/'sample.h';path.write_text('inline bool advance(){ ++picture; return true; }')
   with patch.object(timing,'ROOT',root):
    row=timing.inventory({})[0];self.assertEqual(row['status'],'unreviewed')
    review={row['id']:{'body_sha256':row['body_sha256'],'domain':'shared','decision':'needs-consumers','intent_and_adaptation':'Resolve phase destination.'}}
    self.assertEqual(timing.inventory(review)[0]['status'],'reviewed')
    path.write_text('inline bool advance(){ picture+=2; return true; }')
    self.assertEqual(timing.inventory(review)[0]['status'],'stale')
if __name__=='__main__':unittest.main()
