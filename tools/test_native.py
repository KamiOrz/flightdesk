#!/usr/bin/env python3
"""Run firmware logic tests on the host, after PlatformIO resolves ArduinoJson."""
import os, subprocess, tempfile
from pathlib import Path
root=Path(__file__).resolve().parents[1]
arduinojson=root/'.pio/libdeps/t-display-s3/ArduinoJson/src'
if not arduinojson.is_dir():
    raise SystemExit('Run pio run first to install the pinned ArduinoJson dependency.')
with tempfile.TemporaryDirectory(prefix='flightdesk-tests-') as directory:
    for source in sorted((root/'tests').glob('test_*.cpp')):
        binary=Path(directory)/source.stem
        subprocess.run([os.environ.get('CXX','c++'),'-std=c++17','-I'+str(root/'include'),'-I'+str(arduinojson),str(source),'-o',str(binary)],check=True)
        subprocess.run([str(binary)],check=True)
