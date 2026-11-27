"""Evidence-reader checks; synthetic memory only, no game assets."""
import csv
import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('coverage_reader', Path(__file__).parents[1] / 'tools/t3k_render_coverage.py')
reader = importlib.util.module_from_spec(spec)
spec.loader.exec_module(reader)


class CoverageReaderTests(unittest.TestCase):
    def fixture(self, folder):
        data = bytearray(0x800000)
        for module in range(19):
            source = 0x80010000 + module * 0x2000
            load = 0x20300000 if module == 18 else 0x203002D0
            base, count = reader.PRIMARY.get(module, (0x20100E00, 2))
            words = [source, load, 0x1000, 0, source + 0x1100, base, count * 4 + 4, 0]
            struct.pack_into('>8I', data, 0xB14A0 + module * 32, *words)
            for mode in range(count):
                struct.pack_into('>I', data, (source & 0x7fffff) + 0x1100 + mode * 4, load + mode * 4)
            # The exclusive code end must never be classified as a function.
            struct.pack_into('>I', data, (source & 0x7fffff) + 0x1100 + count * 4, load + 0x1000)
        path = folder / 'memory.bin'
        path.write_bytes(data)
        return path

    def test_module_and_pointer_boundaries(self):
        with tempfile.TemporaryDirectory() as name:
            folder = Path(name)
            result = reader.inventory(self.fixture(folder), folder / 'out')
            self.assertEqual(len(result['modules']), 19)
            module = result['modules'][0]
            self.assertEqual(len(module['primary_modes']), 6)
            self.assertEqual(len(module['candidate_tables'][0]['targets']), 6)
            self.assertNotIn('203012D0', module['candidate_tables'][0]['targets'])

    def test_out_of_segment_dispatch_target_rejected(self):
        with tempfile.TemporaryDirectory() as name:
            folder = Path(name)
            path = self.fixture(folder)
            data = bytearray(path.read_bytes())
            struct.pack_into('>I', data, 0x11100, 0x203012D0)
            path.write_bytes(data)
            with self.assertRaisesRegex(ValueError, 'outside its code segment'):
                reader.inventory(path, folder / 'out')

    def test_incomplete_trace_cannot_inflate_missing_command_count(self):
        with tempfile.TemporaryDirectory() as name:
            folder = Path(name)
            path = self.fixture(folder)
            entries = folder / 'entries.csv'
            entries.write_text('field,scene,w15\n0,1,0010\n')
            with self.assertRaisesRegex(ValueError, 'Incomplete entry trace'):
                reader.inventory(path, folder / 'out', entry_traces=[entries])


if __name__ == '__main__':
    unittest.main()
