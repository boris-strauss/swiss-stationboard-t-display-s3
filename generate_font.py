import os

font_file = os.path.join("src", "bitmaps_vbz_font_wgemini.txt")
out_file = os.path.join("src", "vbz_font.h")

print("Generating C++ Font from ASCII bitmaps...")

try:
    with open(font_file, 'r', encoding='utf-8') as f:
        lines = f.read().splitlines()
except FileNotFoundError:
    print(f"Error: {font_file} not found!")
    exit()

chars = {}
current_char = None
current_matrix = []

# Parse the text file
for line in lines:
    line = line.strip()
    if not line:
        continue
    
    # Check if the line is purely matrix data (+ and -)
    is_matrix = all(c in ['+', '-'] for c in line) and len(line) > 1
            
    if is_matrix:
        current_matrix.append(line)
    else:
        # Save previous character
        if current_char is not None and current_matrix:
            chars[current_char] = current_matrix
        current_char = line
        current_matrix = []

# Catch the last character
if current_char is not None and current_matrix:
    chars[current_char] = current_matrix

# Generate C++ Code
cpp_code = "#pragma once\n#include <Arduino.h>\n\n"
cpp_code += "struct VBZChar {\n    uint8_t width;\n    uint8_t height;\n    const uint16_t* data;\n};\n\n"

for char, matrix in chars.items():
    height = len(matrix)
    width = len(matrix[0])
    
    # Python's ord() automatically handles Umlauts (e.g., ord('ä') = 228)
    char_code = ord(char[0])
    array_name = f"vbz_char_{char_code}"
    
    data_lines = []
    for row in matrix:
        val = 0
        for i, ch in enumerate(row):
            if ch == '+':
                val |= (1 << (15 - i)) # Store bits left-aligned
        data_lines.append(f"0x{val:04X}")
        
    cpp_code += f"const uint16_t {array_name}_data[] PROGMEM = {{ {', '.join(data_lines)} }};\n"
    cpp_code += f"const VBZChar {array_name} = {{ {width}, {height}, {array_name}_data }};\n\n"

# Generate the lookup function
cpp_code += "const VBZChar* getVBZChar(uint8_t unicode) {\n"
cpp_code += "    switch(unicode) {\n"
for char in chars.keys():
    char_code = ord(char[0])
    if char_code < 256: # Ensure it fits in uint8_t
        cpp_code += f"        case {char_code}: return &vbz_char_{char_code};\n"
cpp_code += "        default: return nullptr;\n"
cpp_code += "    }\n}\n"

with open(out_file, 'w', encoding='utf-8') as f:
    f.write(cpp_code)

print(f"Successfully generated {out_file} with {len(chars)} characters!")