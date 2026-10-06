import struct

def parse_dff_hanim(filepath):
    with open(filepath, 'rb') as f:
        data = f.read()

    size = len(data)

    def read_u32(offset):
        if offset + 4 > size: return 0
        return struct.unpack('<I', data[offset:offset+4])[0]

    def dump_node(pos, end):
        while pos + 12 <= end:
            ctype = read_u32(pos)
            csize = read_u32(pos+4)
            data_start = pos + 12
            data_end = data_start + csize
            
            if ctype == 0x011E: # HAnim
                hanim_ver = read_u32(data_start)
                hanim_id = read_u32(data_start+4)
                nodeCount = read_u32(data_start+8)
                if nodeCount > 0:
                    print(f"Found HAnim! ID={hanim_id}, NodeCount={nodeCount}")
                    np = data_start + 20
                    for n in range(nodeCount):
                        nodeId = read_u32(np)
                        nodeIdx = read_u32(np+4)
                        nodeFlags = read_u32(np+8)
                        print(f"  Node {n}: ID={nodeId} Index={nodeIdx}")
                        np += 12
            
            if ctype in [0x0010, 0x000E, 0x000F, 0x0003, 0x001A]:
                # if it's a container chunk, or framelist, parse its children
                # Wait, FrameList (0x000E) contains a Struct (0x000F), followed by Extensions!
                # Extensions (0x0003) contain plugins.
                pass
            
            # Just do a brute force search for 0x011E magic!
            pos += 12 + csize

    # Brute force search for 0x011E
    pos = 0
    while pos + 12 <= size:
        ctype = read_u32(pos)
        csize = read_u32(pos+4)
        if ctype == 0x011E:
            print("Found HAnim Plugin!")
            data_start = pos + 12
            hanim_ver = read_u32(data_start)
            hanim_id = read_u32(data_start+4)
            nodeCount = read_u32(data_start+8)
            if nodeCount > 0:
                print(f"HAnim ID={hanim_id}, NodeCount={nodeCount}")
                np = data_start + 20
                for n in range(nodeCount):
                    nodeId = read_u32(np)
                    nodeIdx = read_u32(np+4)
                    print(f"  Node {n}: ID={nodeId} Index={nodeIdx}")
                    np += 12
        pos += 4

if __name__ == "__main__":
    parse_dff_hanim("app/src/main/assets/cash_pc.dff")
