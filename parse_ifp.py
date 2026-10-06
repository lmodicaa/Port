import struct
import sys

def parse_ifp(filepath):
    with open(filepath, 'rb') as f:
        data = f.read()

    pos = 0
    size = len(data)

    def read_fourcc(offset):
        if offset + 4 > size: return ""
        return data[offset:offset+4].decode('ascii', errors='ignore')

    def read_u32(offset):
        if offset + 4 > size: return 0
        return struct.unpack('<I', data[offset:offset+4])[0]

    magic = read_fourcc(pos)
    if magic != "ANCT": return
    pos += 8

    while pos + 8 <= size:
        type_magic = read_fourcc(pos)
        sz = read_u32(pos + 4)
        pos += 8
        
        if type_magic == "BLOC":
            pos += sz
            pos = (pos + 3) & ~3
        elif type_magic == "ANPK":
            anpk_end = pos + sz
            
            subtype = read_fourcc(pos)
            subsz = read_u32(pos + 4)
            pos += 8
            if subtype == "NAME":
                name = data[pos:pos+subsz].decode('ascii', errors='ignore').strip('\0')
                pos += subsz
            
            num_bones = read_u32(pos)
            unk_size = read_u32(pos+4)
            duration = struct.unpack('<f', data[pos+8:pos+12])[0]
            pos += 12
            
            print(f"Anim: {name} | Bones: {num_bones} | UnkSize: {unk_size} | Duration: {duration}")
            
            for _ in range(num_bones):
                if read_fourcc(pos) == "SEQU":
                    pos += 4
                    bone_id = struct.unpack('<H', data[pos:pos+2])[0]
                    k_type = data[pos+2]
                    num_frames = data[pos+3]
                    pos += 4
                    
                    print(f"  Bone {bone_id} | Type {k_type} | Frames {num_frames}")
                    
                    for i in range(num_frames):
                        # Read frame time? Or delta?
                        # Let's just dump the bytes for each frame to see the pattern.
                        # Wait, we saw 00 00 08 earlier. 
                        # Let's peek at the first 4 bytes of the frame:
                        peek = data[pos:pos+16]
                        hex_str = " ".join([f"{x:02X}" for x in peek])
                        print(f"    Frame {i}: {hex_str}")
                        
                        # Advance pos by the frame size.
                        # type 1: Quat (8 bytes) ?
                        # wait, the 1st sequence had `00 00 08 CB FE DE FE 12 00 EA 0F`
                        # That's 11 bytes!
                        # 00 00 (2 bytes) = time?
                        # 08 (1 byte)?
                        # CB FE DE FE 12 00 EA 0F (8 bytes quat).
                        # Total 11 bytes per frame!
                        if k_type == 1:
                            pos += 11
                        elif k_type == 3:
                            # 2nd sequence type 3 had 23 bytes?
                            # 00 00 08 (3) + 8 (quat) + 6 (pos) = 17?
                            # Wait, the dump had: 00 00 08 99 07 80 0D 31 FD DA 02 CB FF 63 00 20 00 CB FF 63 00 20 00
                            # Length was 23!
                            # 3 + 8 + 6 + 6 = 23!
                            # It seems there are TWO pos vectors?
                            pass
                    
                    # Instead of manual parsing, we just skip to the next SEQU or NAME
                    next_sequ_idx = data.find(b'SEQU', pos)
                    next_name_idx = data.find(b'NAME', pos)
                    
                    next_idx = size
                    if next_sequ_idx != -1: next_idx = next_sequ_idx
                    if next_name_idx != -1 and next_name_idx < next_idx: next_idx = next_name_idx
                    
                    pos = next_idx
                else:
                    break
            
            pos = anpk_end
            pos = (pos + 3) & ~3
        else:
            pos += sz
            pos = (pos + 3) & ~3

if __name__ == "__main__":
    parse_ifp("app/src/main/assets/levels/asylum/allanims.ifp")
