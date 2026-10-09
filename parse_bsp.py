#!/usr/bin/env python3
"""Inspect the RW_WORLD header in a RenderWare BSP.

For the observed Manhunt scene1.bsp, RW_WORLD's RW_STRUCT is 64 bytes.
It contains geometry counts and a bounding box, not ambient/directional light.
"""
import argparse
from pathlib import Path
import struct

CHUNK_HEADER_SIZE = 12
RW_WORLD = 0x000B
RW_STRUCT = 0x0001
RW_WORLD_STRUCT_SIZE = 64


def read_chunk(data: bytes, pos: int):
    if pos < 0 or pos + CHUNK_HEADER_SIZE > len(data):
        raise ValueError(f"Chunk header out of bounds at 0x{pos:X}")
    ctype, size, version = struct.unpack_from("<III", data, pos)
    payload = pos + CHUNK_HEADER_SIZE
    end = payload + size
    if end > len(data):
        raise ValueError(
            f"Chunk 0x{ctype:04X} at 0x{pos:X} exceeds file "
            f"(size={size}, file={len(data)})"
        )
    return ctype, size, version, payload, end


def parse_bsp(filepath: Path):
    data = filepath.read_bytes()
    ctype, _, _, world_payload, world_end = read_chunk(data, 0)
    if ctype != RW_WORLD:
        raise ValueError(f"Expected RW_WORLD 0x000B, got 0x{ctype:04X}")

    stype, ssize, _, struct_payload, struct_end = read_chunk(data, world_payload)
    if stype != RW_STRUCT:
        raise ValueError(f"Expected RW_STRUCT 0x0001, got 0x{stype:04X}")
    if ssize != RW_WORLD_STRUCT_SIZE:
        raise ValueError(
            f"Expected RW_WORLD struct size 64, got {ssize}; "
            "refusing to apply this layout to an unknown variant"
        )
    if struct_end > world_end:
        raise ValueError("RW_WORLD struct extends beyond RW_WORLD chunk")

    # Layout (all fields little-endian):
    # +0  uint32 rootIsWorldSector
    # +4  float invWorldOrigin[3]
    # +16 uint32 numTriangles
    # +20 uint32 numVertices
    # +24 uint32 numPlaneSectors
    # +28 uint32 numAtomicSectors
    # +32 uint32 colSectorSize
    # +36 uint32 format
    # +40 float bboxSup[3]
    # +52 float bboxInf[3]
    values = struct.unpack_from("<I3f6I6f", data, struct_payload)
    (root_is_world_sector, *rest) = values
    inv_world_origin = rest[0:3]
    num_triangles, num_vertices, num_plane_sectors, num_atomic_sectors, col_sector_size, fmt = rest[3:9]
    bbox_sup = rest[9:12]
    bbox_inf = rest[12:15]

    print(f"File: {filepath}")
    print(f"RW_WORLD struct size: {ssize} bytes")
    print(f"+00 rootIsWorldSector: {root_is_world_sector}")
    print(f"+04 invWorldOrigin: {inv_world_origin}")
    print(f"+16 numTriangles: {num_triangles}")
    print(f"+20 numVertices: {num_vertices}")
    print(f"+24 numPlaneSectors: {num_plane_sectors}")
    print(f"+28 numAtomicSectors: {num_atomic_sectors}")
    print(f"+32 colSectorSize: {col_sector_size}")
    print(f"+36 format: 0x{fmt:08X} ({fmt})")
    print(f"+40 bboxSup: {bbox_sup}")
    print(f"+52 bboxInf: {bbox_inf}")
    print("Lighting fields in RW_WORLD: unavailable (not present in this layout).")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Inspect the 64-byte RenderWare RW_WORLD struct in a BSP."
    )
    parser.add_argument("filepath", type=Path, help="Path to scene1.bsp or another BSP")
    parse_bsp(parser.parse_args().filepath)
