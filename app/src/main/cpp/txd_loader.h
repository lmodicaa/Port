#pragma once
#include <cstdint>
#include <vector>
#include <string>

// Resultado de cargar una textura desde un TXD
struct TXDTexture {
    std::string name;
    int width  = 0;
    int height = 0;
    std::vector<uint8_t> rgba;  // siempre RGBA8 sin importar el formato origen
    // RenderWare FilterAddress: low 8 bits = filter, next nibbles = U/V addressing.
    uint32_t filter_address = 0;
    uint8_t filter_mode = 2; // rwFILTERLINEAR
    uint8_t address_u = 1;   // rwTEXTUREADDRESSWRAP
    uint8_t address_v = 1;
    bool valid = false;
};

// Carga todas las texturas de un bloque de memoria TXD (datos crudos del archivo)
std::vector<TXDTexture> txd_load_all(const uint8_t* data, size_t size);
