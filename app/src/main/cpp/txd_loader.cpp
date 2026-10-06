// txd_loader.cpp — parser de RenderWare TXD (PC/D3D8)
// target: Android NDK, C++17
// *Manhunt 1 usa RW 3.6, plataforma D3D8 (id=8)*

#include "txd_loader.h"
#include <cstring>
#include <android/log.h>

#define TAG "Manhunt/TXD"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

// ── RenderWare chunk types ───────────────────────────────────────────────────
static constexpr uint32_t RW_STRUCT          = 0x0001;
static constexpr uint32_t RW_EXTENSION       = 0x0003;
static constexpr uint32_t RW_TEXTURE_NATIVE  = 0x0015;
static constexpr uint32_t RW_TEX_DICTIONARY  = 0x0016;

// ── Struct helpers ───────────────────────────────────────────────────────────
struct ChunkHeader {
    uint32_t type;
    uint32_t size;
    uint32_t version;
};

struct Reader {
    const uint8_t* base;
    size_t         total;
    size_t         pos = 0;

    bool ok()                  const { return pos <= total; }
    bool can_read(size_t n)    const { return pos + n <= total; }

    template<typename T>
    T read() {
        T v{};
        if (can_read(sizeof(T))) {
            memcpy(&v, base + pos, sizeof(T));
            pos += sizeof(T);
        }
        return v;
    }

    void skip(size_t n) { pos += n; }

    ChunkHeader read_chunk() { return read<ChunkHeader>(); }

    // Avanza hasta el siguiente chunk del tipo buscado dentro de [end]
    bool find_chunk(uint32_t type, size_t end, ChunkHeader& out) {
        while (pos + sizeof(ChunkHeader) <= end) {
            out = read_chunk();
            if (out.type == type) return true;
            skip(out.size);           // saltar chunk que no nos interesa
        }
        return false;
    }
};

// ── DXT1 decompressor ────────────────────────────────────────────────────────
// Convierte un bloque 4x4 DXT1 (8 bytes) a RGBA
static void decode_dxt1_block(const uint8_t* src, uint8_t* dst, int dst_stride) {
    uint16_t c0 = src[0] | (src[1] << 8);
    uint16_t c1 = src[2] | (src[3] << 8);

    // RGB565 → RGB888
    uint8_t r[4], g[4], b[4], a[4];
    r[0] = (c0 >> 11) * 255 / 31;
    g[0] = ((c0 >> 5) & 0x3F) * 255 / 63;
    b[0] = (c0 & 0x1F) * 255 / 31;
    a[0] = 255;

    r[1] = (c1 >> 11) * 255 / 31;
    g[1] = ((c1 >> 5) & 0x3F) * 255 / 63;
    b[1] = (c1 & 0x1F) * 255 / 31;
    a[1] = 255;

    if (c0 > c1) {
        r[2] = (2*r[0] + r[1]) / 3;  g[2] = (2*g[0] + g[1]) / 3;  b[2] = (2*b[0] + b[1]) / 3;  a[2] = 255;
        r[3] = (r[0] + 2*r[1]) / 3;  g[3] = (g[0] + 2*g[1]) / 3;  b[3] = (b[0] + 2*b[1]) / 3;  a[3] = 255;
    } else {
        r[2] = (r[0] + r[1]) / 2;    g[2] = (g[0] + g[1]) / 2;    b[2] = (b[0] + b[1]) / 2;    a[2] = 255;
        r[3] = 0; g[3] = 0; b[3] = 0; a[3] = 0;  // transparente
    }

    uint32_t bits = src[4] | (src[5] << 8) | (src[6] << 16) | (src[7] << 24);
    for (int py = 0; py < 4; py++) {
        for (int px = 0; px < 4; px++) {
            int idx = (bits >> (2 * (py * 4 + px))) & 0x3;
            uint8_t* p = dst + py * dst_stride + px * 4;
            p[0] = r[idx]; p[1] = g[idx]; p[2] = b[idx]; p[3] = a[idx];
        }
    }
}

// ── DXT5 decompressor ────────────────────────────────────────────────────────
static void decode_dxt5_block(const uint8_t* src, uint8_t* dst, int dst_stride) {
    // Alpha block (8 bytes)
    uint8_t a[8];
    a[0] = src[0]; a[1] = src[1];
    if (a[0] > a[1]) {
        for (int i = 2; i < 8; i++) a[i] = (uint8_t)((a[0]*(8-i) + a[1]*(i-1)) / 7);
    } else {
        for (int i = 2; i < 6; i++) a[i] = (uint8_t)((a[0]*(6-i) + a[1]*(i-1)) / 5);
        a[6] = 0; a[7] = 255;
    }
    uint64_t abits = 0;
    for (int i = 0; i < 6; i++) abits |= ((uint64_t)src[2+i] << (i*8));

    // Color block (next 8 bytes)
    decode_dxt1_block(src + 8, dst, dst_stride);

    // Overlay alpha
    for (int py = 0; py < 4; py++) {
        for (int px = 0; px < 4; px++) {
            int bit_idx = py * 4 + px;
            int a_idx   = (abits >> (bit_idx * 3)) & 0x7;
            dst[py * dst_stride + px * 4 + 3] = a[a_idx];
        }
    }
}

// ── DXT3 decompressor ────────────────────────────────────────────────────────
static void decode_dxt3_block(const uint8_t* src, uint8_t* dst, int dst_stride) {
    // Color block (next 8 bytes)
    decode_dxt1_block(src + 8, dst, dst_stride);

    // Alpha block (8 bytes: 16 x 4-bit)
    for (int py = 0; py < 4; py++) {
        for (int px = 0; px < 4; px++) {
            int idx = py * 4 + px;
            uint8_t a = src[idx / 2];
            if (idx % 2 == 0) a &= 0x0F;
            else              a >>= 4;
            // Expand 4-bit to 8-bit (e.g. 0xF -> 0xFF)
            dst[py * dst_stride + px * 4 + 3] = a | (a << 4);
        }
    }
}

// ── Descomprimir imagen DXT completa ─────────────────────────────────────────
static std::vector<uint8_t> decompress_dxt(const uint8_t* src, int w, int h, int dxt_type) {
    std::vector<uint8_t> out(w * h * 4, 0);
    int block_size = (dxt_type == 1) ? 8 : 16;
    int bw = (w + 3) / 4;
    int bh = (h + 3) / 4;

    for (int by = 0; by < bh; by++) {
        for (int bx = 0; bx < bw; bx++) {
            const uint8_t* block = src + (by * bw + bx) * block_size;
            uint8_t tmp[4 * 4 * 4] = {};
            
            if (dxt_type == 1)      decode_dxt1_block(block, tmp, 4 * 4);
            else if (dxt_type == 3) decode_dxt3_block(block, tmp, 4 * 4);
            else if (dxt_type == 5) decode_dxt5_block(block, tmp, 4 * 4);

            for (int py = 0; py < 4; py++) {
                int oy = by * 4 + py;
                if (oy >= h) break;
                for (int px = 0; px < 4; px++) {
                    int ox = bx * 4 + px;
                    if (ox >= w) break;
                    memcpy(&out[(oy * w + ox) * 4], &tmp[(py * 4 + px) * 4], 4);
                }
            }
        }
    }
    return out;
}

// ── Parsear una textura nativa PC ────────────────────────────────────────────
static TXDTexture parse_texture_native(Reader& r, size_t chunk_end) {
    TXDTexture tex;

    // Buscar el chunk struct dentro del TextureNative
    ChunkHeader hdr;
    if (!r.find_chunk(RW_STRUCT, chunk_end, hdr)) {
        LOGE("TextureNative: no struct chunk");
        return tex;
    }
    size_t struct_end = r.pos + hdr.size;

    uint32_t platform   = r.read<uint32_t>();
    uint32_t filter     = r.read<uint32_t>();

    // RenderWare guarda FilterAddress en un único DWORD:
    // bits 0..7 = filtro, 8..11 = U, 12..15 = V.
    tex.filter_address = filter;
    tex.filter_mode = static_cast<uint8_t>(filter & 0xFFu);
    tex.address_u = static_cast<uint8_t>((filter >> 8) & 0x0Fu);
    tex.address_v = static_cast<uint8_t>((filter >> 12) & 0x0Fu);

    char name[33] = {}; memcpy(name, r.base + r.pos, 32); r.skip(32);
    char mask[33] = {}; memcpy(mask, r.base + r.pos, 32); r.skip(32);

    uint32_t raster_fmt = r.read<uint32_t>();
    uint32_t d3d_fmt    = r.read<uint32_t>();   // FourCC: DXT1=0x31545844 DXT5=0x35545844
    uint16_t width      = r.read<uint16_t>();
    uint16_t height     = r.read<uint16_t>();
    uint8_t  depth      = r.read<uint8_t>();
    uint8_t  mip_count  = r.read<uint8_t>();
    uint8_t  type       = r.read<uint8_t>();
    uint8_t  flags      = r.read<uint8_t>();    // bit3 = compressed

    tex.name   = name;
    tex.width  = width;
    tex.height = height;

    LOGI("Texture: '%s' %dx%d depth=%d mips=%d d3dfmt=0x%08X platform=%d",
         name, width, height, depth, mip_count, d3d_fmt, platform);

    bool compressed = (flags & 0x08) != 0;
    int dxt_type = 0;

    if (d3d_fmt == 0x31545844) dxt_type = 1; // DXT1
    else if (d3d_fmt == 0x33545844) dxt_type = 3; // DXT3
    else if (d3d_fmt == 0x35545844) dxt_type = 5; // DXT5

    // Leer solo el mip 0 (el más grande)
    uint32_t data_size = r.read<uint32_t>();
    if (!r.can_read(data_size)) { LOGE("TXD: data truncado"); return tex; }
    
    // Manhunt PC hack: Texturas DXT no marcadas con FourCC
    if (dxt_type == 0 && data_size > 0) {
        if (data_size == (width * height) / 2) {
            dxt_type = 1; // 0.5 bytes per pixel -> DXT1
        } else if (data_size == (width * height) && (raster_fmt & 0x6000) == 0) {
            dxt_type = 3; // 1 byte per pixel sin paleta -> DXT3 (o 5)
        }
    }

    const uint8_t* pixels = r.base + r.pos;

    if (dxt_type > 0) {
        tex.rgba = decompress_dxt(pixels, width, height, dxt_type);
        LOGI("DXT%d decompressed %dx%d", dxt_type, width, height);
    } else {
        tex.rgba.resize(width * height * 4);
        size_t expected_size = (depth == 16) ? (width * height * 2) : (width * height * (depth / 8));
        if (data_size < expected_size) {
            LOGE("TXD: data_size (%d) es menor que el tamaño esperado (%d)", data_size, (int)expected_size);
            tex.valid = false;
            return tex;
        }

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
            LOGI("16-bit convertido %dx%d (raster_fmt=0x%08X) size=%d", width, height, raster_fmt, data_size);
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
    }

    tex.valid = !tex.rgba.empty();
    return tex;
}

// ── Entry point público ──────────────────────────────────────────────────────
std::vector<TXDTexture> txd_load_all(const uint8_t* data, size_t size) {
    Reader r{ data, size };
    std::vector<TXDTexture> result;

    // Chunk raíz: TexDictionary
    ChunkHeader root = r.read_chunk();
    if (root.type != RW_TEX_DICTIONARY) {
        LOGE("No es un TXD (type=0x%X)", root.type);
        return result;
    }
    size_t txd_end = r.pos + root.size;

    // Struct del TexDictionary: número de texturas
    ChunkHeader sh = r.read_chunk();
    if (sh.type != RW_STRUCT) { LOGE("TXD: falta struct"); return result; }
    uint16_t tex_count = r.read<uint16_t>();
    uint16_t device_id = r.read<uint16_t>();
    LOGI("TXD: %d texturas, device=%d", tex_count, device_id);

    while (r.pos < txd_end) {
        ChunkHeader tn;
        if (!r.find_chunk(RW_TEXTURE_NATIVE, txd_end, tn)) {
            break;
        }
        size_t tn_end = r.pos + tn.size;
        
        TXDTexture tex = parse_texture_native(r, tn_end);
        if (tex.valid) {
            result.push_back(tex);
        }
        
        r.pos = tn_end;
    }

    return result;
}
