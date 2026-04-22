import os
import csv
import urllib.request
import io
import json

CSV_URL = "https://data.opentransportdata.swiss/dataset/service-point-v2/resource_permalink/actual-date-swiss-service-point.csv"
OUT_FILE = os.path.join("src", "stations.h")

print("Downloading latest station data from SBB OpenData...")

try:
    # Fetch the CSV from the live URL
    req = urllib.request.Request(CSV_URL, headers={'User-Agent': 'Mozilla/5.0'})
    with urllib.request.urlopen(req) as response:
        csv_content = response.read().decode('utf-8')
        
    print("Download successful! Parsing stations...")
    
    # Auto-detect the delimiter (Swiss open data often switches between , and ;)
    first_line = csv_content.split('\n')[0]
    delimiter = ';' if ';' in first_line else ','
    
    # Treat the downloaded string exactly like a local file
    csv_file = io.StringIO(csv_content)
    reader = csv.DictReader(csv_file, delimiter=delimiter) 
    
    stations = set()
    for row in reader:
        # Extract the exact column you confirmed
        station_name = row.get('designationOfficial')
        
        # Only add valid, non-empty station names
        if station_name:
            stations.add(station_name.strip())

    # Sort the station list alphabetically (great for the web UI search dropdown!)
    sorted_stations = sorted(list(stations))
    
    # Compress the list into a clean JSON array string
    # ensure_ascii=False keeps the beautiful Swiss Umlauts (ä, ö, ü) intact!
    json_array_str = json.dumps(sorted_stations, ensure_ascii=False)
    
    # Generate the C++ header file exactly as the ESP32 expects it
    cpp_code = f"""#ifndef STATIONS_H
#define STATIONS_H

#include <Arduino.h>

// Auto-generated JSON array from remote SBB OpenData Service Point CSV
const char STATIONS_JSON[] PROGMEM = R"====({json_array_str})====";

#endif
"""

    # Ensure the src folder exists, then write the file
    os.makedirs(os.path.dirname(OUT_FILE), exist_ok=True)
    with open(OUT_FILE, "w", encoding="utf-8") as f:
        f.write(cpp_code)
        
    print(f"Successfully generated {OUT_FILE} with {len(sorted_stations)} stations!")

except Exception as e:
    print(f"Failed to fetch or parse the CSV: {e}")
    exit(1)