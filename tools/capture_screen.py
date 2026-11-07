#!/usr/bin/env python3
"""Read the device framebuffer over USB."""
import argparse
import json
import time
from pathlib import Path
import serial
import struct
import zlib

parser=argparse.ArgumentParser()
parser.add_argument('--port',default='/dev/cu.usbmodem2101')
parser.add_argument('--output',default='.local/screen.png')
args=parser.parse_args()
device=serial.Serial(baudrate=115200,timeout=1,write_timeout=5)
device.port=args.port
device.dtr=True
device.rts=False
device.open()
with device:
    time.sleep(2)
    device.reset_input_buffer()
    device.write(b'{"cmd":"screen"}\n')
    deadline=time.monotonic()+15
    while time.monotonic()<deadline:
        line=device.readline()
        if line.startswith(b'SCREEN_RGB565 '):
            _,width,height=line.split()
            width,height=int(width),int(height)
            data=bytearray()
            deadline=time.monotonic()+30
            while len(data)<width*height*2 and time.monotonic()<deadline:
                data.extend(device.read(width*height*2-len(data)))
            if len(data)!=width*height*2:raise RuntimeError('Incomplete screenshot')
            rgb=bytearray()
            for i in range(0,len(data),2):
                v=data[i]|data[i+1]<<8
                rgb.extend((((v>>11)&31)*255//31,((v>>5)&63)*255//63,(v&31)*255//31))
            path=Path(args.output)
            path.parent.mkdir(parents=True,exist_ok=True)
            def chunk(kind,data):
                return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data)&0xffffffff)
            pixels=b''.join(b'\0'+bytes(rgb[y*width*3:(y+1)*width*3]) for y in range(height))
            path.write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',width,height,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(pixels))+chunk(b'IEND',b''))
            print(path.resolve())
            break
    else:raise RuntimeError('Device did not return a screenshot')
