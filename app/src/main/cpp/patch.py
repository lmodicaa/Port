import sys
with open(r'C:\Users\IK\Documents\Port\app\src\main\cpp\txd_loader.cpp', 'r', encoding='utf-8', errors='ignore') as f:
    lines = f.readlines()

new_lines = []
skip = False
for line in lines:
    if "} else {" in line and "ARGB8" in "".join(lines[lines.index(line):lines.index(line)+3]) or "resize(width" in "".join(lines[lines.index(line):lines.index(line)+3]):
        # This is a bit brittle, let's just do a string replace on the whole file
        pass

content = "".join(lines)
import re
# find the block
match = re.search(r'} else \{\s*//.*?\s*tex\.rgba\.resize.*?LOGI.*?\}', content, re.DOTALL)
if match:
    old = match.group(0)
    new = '''} else {
        tex.rgba.resize(width * height * 4);
        if (depth == 16) {
            uint32_t fmt = raster_fmt & 0x0F00;
            const uint16_t* px16 = reinterpret_cast<const uint16_t*>(pixels);
            for (int i = 0; i < width * height; i++) {
                uint16_t c = px16[i];
                if (fmt == 0x0100) {
                    tex.rgba[i*4+0] = ((c >> 10) & 0x1F) * 255 / 31;
                    tex.rgba[i*4+1] = ((c >> 5)  & 0x1F) * 255 / 31;
                    tex.rgba[i*4+2] = ((c)       & 0x1F) * 255 / 31;
                    tex.rgba[i*4+3] = (c & 0x8000) ? 255 : 0;
                } else if (fmt == 0x0300) {
                    tex.rgba[i*4+0] = ((c >> 8) & 0xF) * 255 / 15;
                    tex.rgba[i*4+1] = ((c >> 4) & 0xF) * 255 / 15;
                    tex.rgba[i*4+2] = ((c)      & 0xF) * 255 / 15;
                    tex.rgba[i*4+3] = ((c >> 12)& 0xF) * 255 / 15;
                } else {
                    tex.rgba[i*4+0] = ((c >> 11) & 0x1F) * 255 / 31;
                    tex.rgba[i*4+1] = ((c >> 5)  & 0x3F) * 255 / 63;
                    tex.rgba[i*4+2] = ((c)       & 0x1F) * 255 / 31;
                    tex.rgba[i*4+3] = 255;
                }
            }
            LOGI("16-bit convertido %dx%d (fmt=0x%04X)", width, height, fmt);
        } else if (depth == 32 || depth == 24) {
            int bpp = depth / 8;
            for (int i = 0; i < width * height; i++) {
                if (bpp == 4) {
                    tex.rgba[i*4+0] = pixels[i*4+2];
                    tex.rgba[i*4+1] = pixels[i*4+1];
                    tex.rgba[i*4+2] = pixels[i*4+0];
                    tex.rgba[i*4+3] = pixels[i*4+3];
                } else {
                    tex.rgba[i*4+0] = pixels[i*3+2];
                    tex.rgba[i*4+1] = pixels[i*3+1];
                    tex.rgba[i*4+2] = pixels[i*3+0];
                    tex.rgba[i*4+3] = 255;
                }
            }
            LOGI("24/32-bit convertido %dx%d", width, height);
        } else {
            LOGE("Unsupported depth: %d", depth);
            tex.valid = false;
            return tex;
        }
    }'''
    content = content.replace(old, new)
    with open(r'C:\Users\IK\Documents\Port\app\src\main\cpp\txd_loader.cpp', 'w', encoding='utf-8') as f:
        f.write(content)
    print("Patched successfully")
else:
    print("Match not found")
