#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compile the actual USB hub driver with host stubs and run its port error tests."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source', type=Path,
                    help='Circle tree whose driver is tested, default: this one')
args = parser.parse_args()
here = Path(__file__).resolve().parent
source = args.source.resolve() if args.source else here.parents[1]
compiler = os.environ.get('CXX', 'c++')
sanitizers = ['-fsanitize=address,undefined', '-fno-sanitize-recover=undefined']
common = ['-std=c++17', '-g', '-fno-omit-frame-pointer', *sanitizers,
          '-DAARCH=64', '-DRASPPI=4',
          '-I' + str(here / 'stubs'), '-isystem', str(source / 'include')]
# The driver builds with the warnings Circle's Rules.mk sets, the test with the strictest.
driver_warnings = ['-Wall', '-Werror']
test_warnings = ['-Wall', '-Wextra', '-Werror', '-Wshadow', '-Wconversion',
                 '-Wsign-conversion']
units = [(source / 'lib/usb/usbstandardhub.cpp', driver_warnings),
         (here / 'stubs/stubs.cpp', test_warnings),
         (here / 'test.cpp', test_warnings)]
with tempfile.TemporaryDirectory(prefix='circle-hub-') as temp:
    objects = []
    for unit, warnings in units:
        obj = Path(temp) / (unit.stem + '.o')
        subprocess.run([compiler, *common, *warnings, '-c', str(unit), '-o', str(obj)],
                       check=True)
        objects.append(str(obj))
    exe = Path(temp) / 'test'
    subprocess.run([compiler, *sanitizers, *objects, '-o', str(exe)], check=True)
    raise SystemExit(subprocess.run([str(exe)]).returncode)
