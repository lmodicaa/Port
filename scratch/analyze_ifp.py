import argparse
from pathlib import Path
import struct

def dump_duration(filepath):
    with open(filepath, "rb") as f:
        data = f.read()
    
    pos = 8
    size = len(data)
    
    while pos < size - 4:
        fourcc = data[pos:pos+4].decode('ascii', errors='ignore')
        if fourcc == 'NAME':
            sz = struct.unpack('<I', data[pos+4:pos+8])[0]
            name = data[pos+8:pos+8+sz].decode('ascii', errors='ignore').strip('\x00')
            pos += 8 + sz
            num_bones = struct.unpack('<I', data[pos:pos+4])[0]
            unk_size = struct.unpack('<I', data[pos+4:pos+8])[0]
            duration = struct.unpack('<f', data[pos+8:pos+12])[0]
            pos += 12
            
            if name == "Stand_Idle":
                print(f"{name}: duration = {duration}, unk = {unk_size}")
                break
        elif fourcc == 'ANPK':
            pos += 8
        else:
            pos += 1

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Print the duration of Stand_Idle in an IFP.")
    parser.add_argument("filepath", type=Path, help="Path to allanims.ifp")
    dump_duration(parser.parse_args().filepath)
