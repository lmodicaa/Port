import struct
import sys

def parse_bsp():
    with open("app/src/main/assets/cash_pc.dff", "rb") as f:
        data = f.read()
    
    pos = 0
    while pos < len(data):
        hdr = struct.unpack("<III", data[pos:pos+12])
        pos += 12
        if hdr[0] == 0x0E: # FrameList
            print(f"FrameList at {pos}")
            fstruct = struct.unpack("<III", data[pos:pos+12])
            pos += 12
            frameCount = struct.unpack("<I", data[pos:pos+4])[0]
            pos += 4
            for i in range(frameCount):
                mat = struct.unpack("<9f", data[pos:pos+36])
                pos += 36
                trans = struct.unpack("<3f", data[pos:pos+12])
                pos += 12
                parent = struct.unpack("<I", data[pos:pos+4])[0]
                pos += 8 # flag
                if i == 0 or i == 1:
                    print(f"Bone {i}:")
                    print(f"  Mat: {mat}")
                    print(f"  Pos: {trans}")
                    print(f"  Parent: {parent}")
            break
        else:
            pos += hdr[1]

parse_bsp()
