#!/usr/bin/env python3
"""Provision over USB without embedding or saving credentials in project files."""
import argparse
import getpass
import json
import time
import serial

p=argparse.ArgumentParser()
p.add_argument('--port',default='/dev/cu.usbmodem2101')
p.add_argument('--wifi',action='store_true')
a=p.parse_args()
device=serial.Serial(baudrate=115200,timeout=.3,write_timeout=5)
device.port=a.port
device.dtr=True
device.rts=False
device.open()
with device:
    time.sleep(2)
    if a.wifi:
        command={'cmd':'wifi','ssid':input('Wi-Fi SSID: '),'password':getpass.getpass('Wi-Fi password: ')}
        device.write((json.dumps(command)+'\n').encode())
    end=time.monotonic()+15
    while time.monotonic()<end:
        device.write(b'{"cmd":"status"}\n')
        until=time.monotonic()+2
        while time.monotonic()<until:
            line=device.readline().decode(errors='replace').strip()
            if line:print(line)
