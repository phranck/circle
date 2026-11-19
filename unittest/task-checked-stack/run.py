#!/usr/bin/env python3
"""Run the checked task stack test against the real CHeapAllocator and CTask."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

here = Path(__file__).resolve().parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source', type=Path, default=here.parents[1])
args = parser.parse_args()


def without_includes(path):
    """The file's text without its #include lines, whose names test.cpp stands in for."""
    text = (args.source / path).read_text()
    return '\n'.join(line for line in text.splitlines() if not line.startswith('#include')) + '\n'


def without_registers(text):
    """task.cpp without InitializeRegs(), which sets up Arm registers; test.cpp stands in for it."""
    start = text.index('#if AARCH == 32\n\nvoid CTask::InitializeRegs')
    end = text.index('void CTask::TaskEntry')
    return text[:start] + text[end:]


with tempfile.TemporaryDirectory() as tmp:
    folder = Path(tmp)
    (folder / 'heapallocator.h.inc').write_text(without_includes('include/circle/heapallocator.h'))
    (folder / 'heapallocator.cpp.inc').write_text(without_includes('lib/heapallocator.cpp'))
    (folder / 'task.h.inc').write_text(without_includes('include/circle/sched/task.h'))
    (folder / 'task.cpp.inc').write_text(without_registers(without_includes('lib/sched/task.cpp')))
    exe = folder / 'test'
    # CTask's list fields are used by the real scheduler, which the test stands in for
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
                    '-Wno-unused-parameter', '-Wno-unused-private-field', '-I', tmp, str(here / 'test.cpp'),
                    '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True, timeout=60)
