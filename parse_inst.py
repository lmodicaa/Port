import struct

with open('entity.inst', 'rb') as f:
    data = f.read()

count = struct.unpack('<I', data[:4])[0]
sizes = struct.unpack('<' + 'I'*count, data[4:4+count*4])

offset = 4 + count * 4

for i in range(min(5, count)):
    block = data[offset:offset+sizes[i]]
    print(f"\nEntity {i} size {sizes[i]}:")
    
    # Try to parse string until null
    s1_end = block.find(b'\x00')
    name = block[:s1_end].decode('ascii', errors='replace')
    
    # Is there a second string? Or just padding?
    # Usually strings in RW or Manhunt are padded to 4 bytes.
    pos1 = (s1_end + 1 + 3) & ~3
    
    s2_end = block.find(b'\x00', pos1)
    if s2_end != -1 and s2_end < len(block) - 28:
        model = block[pos1:s2_end].decode('ascii', errors='replace')
        pos2 = (s2_end + 1 + 3) & ~3
        print(f"  Name: {name}, Model: {model}")
        
        # Read floats
        if pos2 + 28 <= len(block):
            floats = struct.unpack('<7f', block[pos2:pos2+28])
            print(f"  Pos: {floats[:3]}")
            print(f"  Rot: {floats[3:7]}")
            
            # Remaining data
            rem = block[pos2+28:]
            if rem:
                s3_end = rem.find(b'\x00')
                if s3_end != -1:
                    print(f"  Class: {rem[:s3_end].decode('ascii', errors='replace')}")
                print(f"  Rem hex: {rem.hex(' ')}")
    
    offset += sizes[i]
