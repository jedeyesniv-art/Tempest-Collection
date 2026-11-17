"""Find a Space Giraffe setting in memory: snapshots of the running game's global data, then the bytes that flip
with the player's toggles.

python -I tools/sg_memwatch.py snap <dir> [seconds] [interval]   - snapshots of SpaceGiraffePC.exe's .data
                                                                    ($4D9000..$559C68) every interval s
python -I tools/sg_memwatch.py find <dir> [min_flips]            - bytes with exactly two values that change at
                                                                    least min_flips times and hold each value for
                                                                    2+ snapshots
"""
import ctypes
import ctypes.wintypes as wt
import glob
import os
import sys
import time

BASE, SIZE = 0x4D9000, 0x80C68
k32 = ctypes.WinDLL('kernel32', use_last_error=True)


def find_pid(name='SpaceGiraffePC.exe'):
    class PE(ctypes.Structure):
        _fields_ = [('dwSize', wt.DWORD), ('cntUsage', wt.DWORD), ('th32ProcessID', wt.DWORD),
                    ('th32DefaultHeapID', ctypes.c_void_p), ('th32ModuleID', wt.DWORD), ('cntThreads', wt.DWORD),
                    ('th32ParentProcessID', wt.DWORD), ('pcPriClassBase', ctypes.c_long), ('dwFlags', wt.DWORD),
                    ('szExeFile', ctypes.c_wchar * 260)]
    snap = k32.CreateToolhelp32Snapshot(2, 0)
    pe = PE()
    pe.dwSize = ctypes.sizeof(PE)
    ok = k32.Process32FirstW(snap, ctypes.byref(pe))
    while ok:
        if pe.szExeFile.lower() == name.lower():
            k32.CloseHandle(snap)
            return pe.th32ProcessID
        ok = k32.Process32NextW(snap, ctypes.byref(pe))
    k32.CloseHandle(snap)
    return None


def snap(d, seconds, interval):
    os.makedirs(d, exist_ok=True)
    pid = find_pid()
    if not pid:
        sys.exit('SpaceGiraffePC.exe is not running')
    h = k32.OpenProcess(0x0010 | 0x0400, False, pid)   # VM_READ | QUERY_INFORMATION
    buf = ctypes.create_string_buffer(SIZE)
    got = ctypes.c_size_t()
    n = int(seconds / interval)
    for i in range(n):
        if not k32.ReadProcessMemory(h, ctypes.c_void_p(BASE), buf, SIZE, ctypes.byref(got)):
            print('read failed at %d (error %d)' % (i, ctypes.get_last_error()))
            break
        open(os.path.join(d, 's%03d.bin' % i), 'wb').write(buf.raw[:got.value])
        time.sleep(interval)
    print('%d snapshots in %s' % (i + 1, d))


def find(d, min_flips):
    files = sorted(glob.glob(os.path.join(d, 's*.bin')))
    snaps = [open(f, 'rb').read() for f in files]
    n = min(len(s) for s in snaps)
    hits = []
    for a in range(n):
        col = [s[a] for s in snaps]
        if len(set(col)) != 2:
            continue
        runs = []
        for v in col:
            if runs and runs[-1][0] == v:
                runs[-1][1] += 1
            else:
                runs.append([v, 1])
        flips = len(runs) - 1
        if flips >= min_flips and all(r[1] >= 2 for r in runs[1:-1]):
            hits.append((a, flips, runs))
    print('%d snapshots, %d candidates' % (len(snaps), len(hits)))
    for a, flips, runs in hits[:40]:
        print('$%08X flips %d: %s' % (BASE + a, flips, ' '.join('%02x*%d' % (v, c) for v, c in runs)))


if __name__ == '__main__':
    if sys.argv[1] == 'snap':
        snap(sys.argv[2], float(sys.argv[3]) if len(sys.argv) > 3 else 60, float(sys.argv[4]) if len(sys.argv) > 4 else 2)
    else:
        find(sys.argv[2], int(sys.argv[3]) if len(sys.argv) > 3 else 3)
