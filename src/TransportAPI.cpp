#include "TransportAPI.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <algorithm>
#include <set> // Required for our new deduplication tracker

// Helper function to URL-encode the plain-text station name
String urlEncode(String str) {
    String encodedString = "";
    char c;
    char code0;
    char code1;
    for (int i = 0; i < str.length(); i++){
        c = str.charAt(i);
        if (c == ' ') {
            encodedString += "%20";
        } else if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            encodedString += c;
        } else {
            code1 = (c & 0xf) + '0';
            if ((c & 0xf) > 9) code1 = (c & 0xf) - 10 + 'A';
            c = (c >> 4) & 0xf;
            code0 = c + '0';
            if (c > 9) code0 = c - 10 + 'A';
            encodedString += '%';
            encodedString += code0;
            encodedString += code1;
        }
    }
    return encodedString;
}

std::vector<Departure> fetchDepartures(const char* station, const String& opMode) {
    HTTPClient http;
    
    // ENCODE THE STATION NAME HERE
    String encodedStation = urlEncode(String(station));

    // We fetch 20 to ensure we have enough data even if some are delayed
    String url = "http://transport.opendata.ch/v1/stationboard?station=" + encodedStation + "&limit=20";
    
    Serial.print("\nFetching data from: ");
    Serial.println(url);

    // CRITICAL FIX: Force HTTP/1.0 to disable chunked transfer encoding
    http.useHTTP10(true); 
    
    http.begin(url);
    http.setTimeout(10000); // 10-second timeout
    
    int httpCode = http.GET();

    if (httpCode == 200) { 
        
        // RAM Saver: Only parse the exact fields we need
        JsonDocument filter;
        filter["stationboard"][0]["category"] = true;
        filter["stationboard"][0]["number"] = true;
        filter["stationboard"][0]["to"] = true;
        filter["stationboard"][0]["operator"] = true;
        filter["stationboard"][0]["stop"]["departureTimestamp"] = true;
        filter["stationboard"][0]["stop"]["delay"] = true;

        JsonDocument doc; 
        
        // Parse directly from the live network stream using the filter
        DeserializationError error = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));

        if (error) {
            Serial.print("JSON Parsing failed: ");
            Serial.println(error.c_str());
            http.end();
            return {}; // Return empty list on failure
        }

        std::vector<Departure> departuresList;
        JsonArray stationboard = doc["stationboard"];
        
        // ==========================================
        // DEDUPLICATION TRACKER
        // Stores "Timestamp_Destination" to catch ghost trains
        // ==========================================
        std::set<String> seenDepartures;

        // Extract the data from the JSON
        for (JsonObject departure : stationboard) {
            long scheduledTimestamp = departure["stop"]["departureTimestamp"] | 0;       
            int delay = departure["stop"]["delay"] | 0; 
            
            // If the API glitched and missed the timestamp, skip it
            if (scheduledTimestamp == 0) continue;
            
            String dest = departure["to"].as<String>();
            
            // Generate a unique fingerprint for this specific physical train
            String uniqueFingerprint = String(scheduledTimestamp) + "_" + dest;
            
            // If we have already seen this exact train, skip it!
            if (seenDepartures.find(uniqueFingerprint) != seenDepartures.end()) {
                continue; 
            }
            
            // Mark this train as seen
            seenDepartures.insert(uniqueFingerprint);

            Departure dep;
            dep.category = departure["category"].as<String>(); 
            
            // BUGFIX: HAFAS "Null" Fallback
            if (departure["number"].isNull() || departure["number"].as<String>() == "null" || departure["number"].as<String>() == "") {
                dep.number = dep.category; 
            } else {
                dep.number = departure["number"].as<String>();
            }
            
            dep.destination = dest;
            dep.operatorName = departure["operator"].as<String>();    
            dep.scheduledTimestamp = scheduledTimestamp;
            dep.delayMinutes = delay;
            dep.actualTimestamp = scheduledTimestamp + (delay * 60); // Apply the delay
            dep.isCancelled = false;
            departuresList.push_back(dep);
        }

        // Sort: train mode by scheduled time, tram mode by actual time
        if (opMode == "train") {
            std::sort(departuresList.begin(), departuresList.end(), [](const Departure& a, const Departure& b) {
                return a.scheduledTimestamp < b.scheduledTimestamp;
            });
        } else {
            std::sort(departuresList.begin(), departuresList.end(), [](const Departure& a, const Departure& b) {
                return a.actualTimestamp < b.actualTimestamp;
            });
        }

        // Get the current UTC time from the ESP32
        time_t now;
        time(&now); 

        std::vector<Departure> topDepartures;
        int displayedCount = 0; 
        
        // Filter out old vehicles and grab the top 6
        for (const Departure& dep : departuresList) {
            int diffSeconds = dep.actualTimestamp - now;
            
            // Skip vehicles that departed more than 60 seconds ago
            if (diffSeconds < -60) {
                continue; 
            }

            topDepartures.push_back(dep);
            displayedCount++;
            
            // Stop once we have 12 clean, sorted entries
            if (displayedCount >= 12) {
                break; 
            }
        }
        
        http.end();
        return topDepartures; 
        
    } else {
        Serial.printf("HTTP Request failed, error code: %d\n", httpCode);
        http.end();
        return {}; 
    }
}