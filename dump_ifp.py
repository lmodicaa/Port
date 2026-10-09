import argparse
from pathlib import Path

def main():
    parser = argparse.ArgumentParser(description="Find a SEQU marker near the start of an IFP file.")
    parser.add_argument("filepath", type=Path, help="Path to allanims.ifp")
    path = parser.parse_args().filepath
    with path.open("rb") as f:
        data = f.read(1024)
    idx = data.find(b"SEQU")
    if idx != -1:
        print("SEQU found at", idx)
        print([hex(c) for c in data[idx:idx + 64]])
    else:
        print("SEQU not found in first 1024 bytes")

if __name__ == "__main__":
    main()
