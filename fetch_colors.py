import csv
import urllib.request
import os

def hex_to_rgb565(hex_str):
    if not hex_str or not hex_str.startswith('#'): return "0x0000"
    hex_str = hex_str.lstrip('#')
    if len(hex_str) != 6: return "0x0000"
    r, g, b = int(hex_str[0:2], 16), int(hex_str[2:4], 16), int(hex_str[4:6], 16)
    rgb565 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
    return f"0x{rgb565:04X}"

print("Fetching latest Swiss transit line colors from GitHub...")
url = "https://raw.githubusercontent.com/Traewelling/line-colors/main/line-colors-CH.csv"

try:
    response = urllib.request.urlopen(url)
    lines = [l.decode('utf-8') for l in response.readlines()]
except Exception as e:
    print(f"Failed to download from GitHub: {e}")
    # Fallback to local file if GitHub is unreachable
    with open("line-colors-CH.csv", "r", encoding="utf-8") as f:
        lines = f.readlines()

reader = csv.DictReader(lines)
structs = []

for row in reader:
    # Use shortOperatorName first because it contains the searchable acronyms (zvv-vbz, tnw-pag)
    op = row.get("shortOperatorName", "").strip()
    if not op: op = row.get("GTFSAgencyName", "").strip()
    
    line = row.get("lineName", "").strip()
    
    if op and line:
        bg = hex_to_rgb565(row.get("backgroundColor", ""))
        fg = hex_to_rgb565(row.get("textColor", "#FFFFFF"))
        
        # Format for C++ struct
        structs.append(f'{{"{op}", "{line}", {bg}, {fg}}}')

out_path = os.path.join("src", "line_colors_data.h")
with open(out_path, "w", encoding="utf-8") as f:
    f.write("#pragma once\n#include <Arduino.h>\n\n")
    f.write("struct TransitColor {\n    const char* opName;\n    const char* lineNum;\n    uint16_t bgColor;\n    uint16_t fgColor;\n};\n\n")
    f.write("const TransitColor TRANSIT_COLORS[] PROGMEM = {\n")
    f.write(",\n".join(structs))
    f.write("\n};\n\n")
    f.write("const size_t NUM_TRANSIT_COLORS = sizeof(TRANSIT_COLORS) / sizeof(TRANSIT_COLORS[0]);\n")

print(f"Successfully generated {out_path} with {len(structs)} color definitions!")