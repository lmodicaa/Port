import argparse
from pathlib import Path
import struct

def parse_bsp(filepath):
    with open(filepath, 'rb') as f:
        data = f.read()

    def read_chunk(pos):
        ctype, size, ver = struct.unpack('<III', data[pos:pos+12])
        return ctype, size, pos+12

    ctype, size, pos = read_chunk(0)
    assert ctype == 0x000B, f"Expected 0x000B, got {ctype:04X}"
    
    stype, ssize, spos = read_chunk(pos)
    assert stype == 0x0001, "Expected struct"
    
    # RenderWare 3.6 RW_WORLD struct:
    # int32 rootIsWorldSector
    # float32 invWorldOrigin[3]
    # float32 ambientColor[4] or uint8[4]? usually float in RW3.4+ but let's check size
    # float32 directionalAmbientColor[4]
    # float32 lightDirection[3]
    # int32 numTriangles
    # int32 numVertices
    # int32 numPlaneSectors
    # int32 numWorldSectors
    # int32 colSectorSize
    # int32 format
    
    # Let's just unpack it as ints and floats to see
    vals = struct.unpack(f'<{ssize//4}I', data[spos:spos+ssize])
    print(f"World Struct Size: {ssize}")
    print(f"Vals as HEX:")
    for i, v in enumerate(vals):
        print(f"  {i}: {v:08X} ({v})")
        
    # The format is usually at offset... wait.
    # If size is e.g. 84 bytes:
    # 0: rootIsWorldSector (4)
    # 1,2,3: invWorldOrigin (12)
    # 4,5,6,7: ambient (16)
    # 8,9,10,11: dir ambient (16)
    # 12,13,14: light dir (12)
    # 15: numTriangles (4)
    # 16: numVertices (4)
    # 17: numPlaneSectors (4)
    # 18: numWorldSectors (4)
    # 19: colSectorSize (4)
    # 20: format (4)
    # Total = 21 * 4 = 84 bytes!

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Inspect a RenderWare BSP/World file.')
    parser.add_argument('filepath', type=Path, help='Path to scene1.bsp or another BSP file')
    parse_bsp(parser.parse_args().filepath)
