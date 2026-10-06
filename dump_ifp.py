import struct
with open('/storage/emulated/0/Manhunt/levels/asylum/allanims.ifp', 'rb') as f:
    data = f.read(1024)
    idx = data.find(b'SEQU')
    if idx != -1:
        print('SEQU found at', idx)
        print([hex(c) for c in data[idx:idx+64]])

