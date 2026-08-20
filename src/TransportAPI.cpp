#include "TransportAPI.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <stdlib.h> // atoi() for the search.ch delay strings
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

// ---------------------------------------------------------------------------
// Source 1: transport.opendata.ch (primary)
//
// fields[] makes the server strip everything we never render. By default the
// stationboard response carries a full passList (every subsequent stop of every
// departure), which is ~95% of the payload. Measured for one station, limit=20:
//   without fields[] : ~112'000 bytes
//   with    fields[] :   ~2'750 bytes
// At a 20 s refresh that is ~12 MB/day instead of ~485 MB/day.
// Brackets are percent-encoded so they survive any URL handling.
// Keep this list in sync with the ArduinoJson filter below.
// ---------------------------------------------------------------------------
static bool fetchFromOpendata(const String& encodedStation, std::vector<Departure>& out) {
    static const char* API_FIELDS =
        "&fields%5B%5D=stationboard/category"
        "&fields%5B%5D=stationboard/number"
        "&fields%5B%5D=stationboard/to"
        "&fields%5B%5D=stationboard/operator"
        "&fields%5B%5D=stationboard/stop/departureTimestamp"
        "&fields%5B%5D=stationboard/stop/delay";

    String url = "http://transport.opendata.ch/v1/stationboard?station=" + encodedStation
               + "&limit=20" + API_FIELDS;

    Serial.print("\nFetching data from: ");
    Serial.println(url);

    HTTPClient http;
    // CRITICAL FIX: Force HTTP/1.0 to disable chunked transfer encoding
    http.useHTTP10(true);
    http.begin(url);
    http.setTimeout(10000); // 10-second timeout

    int httpCode = http.GET();
    if (httpCode != 200) {
        Serial.printf("opendata.ch request failed, error code: %d\n", httpCode);
        http.end();
        return false;
    }

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
        Serial.print("opendata.ch JSON parsing failed: ");
        Serial.println(error.c_str());
        http.end();
        return false;
    }

    for (JsonObject departure : doc["stationboard"].as<JsonArray>()) {
        long scheduledTimestamp = departure["stop"]["departureTimestamp"] | 0;
        // If the API glitched and missed the timestamp, skip it
        if (scheduledTimestamp == 0) continue;

        Departure dep;
        dep.category = departure["category"].as<String>();

        // BUGFIX: HAFAS "Null" Fallback
        if (departure["number"].isNull() || departure["number"].as<String>() == "null" || departure["number"].as<String>() == "") {
            dep.number = dep.category;
        } else {
            dep.number = departure["number"].as<String>();
        }

        dep.destination = departure["to"].as<String>();
        dep.operatorName = departure["operator"].as<String>();
        dep.scheduledTimestamp = scheduledTimestamp;
        dep.delayMinutes = departure["stop"]["delay"] | 0;
        dep.actualTimestamp = scheduledTimestamp + (dep.delayMinutes * 60); // Apply the delay
        dep.isCancelled = false;
        out.push_back(dep);
    }

    http.end();
    return true;
}

// "2026-08-20 17:26:00" in Swiss local time -> UTC epoch.
// main.cpp calls configTzTime("CET-1CEST,...") before this ever runs, so mktime
// resolves the wall clock with the correct DST offset.
static long parseLocalTimestamp(const char* text) {
    if (text == nullptr) return 0;
    int year, month, day, hour, minute, second;
    if (sscanf(text, "%d-%d-%d %d:%d:%d", &year, &month, &day, &hour, &minute, &second) != 6) {
        return 0;
    }
    struct tm parsed = {};
    parsed.tm_year = year - 1900;
    parsed.tm_mon  = month - 1;
    parsed.tm_mday = day;
    parsed.tm_hour = hour;
    parsed.tm_min  = minute;
    parsed.tm_sec  = second;
    parsed.tm_isdst = -1; // let the C library decide CET vs CEST
    time_t epoch = mktime(&parsed);
    return (epoch == (time_t)-1) ? 0 : (long)epoch;
}

// ---------------------------------------------------------------------------
// Source 2: search.ch (fallback)
//
// Same data, independent infrastructure - proven on 2026-08-20, when
// transport.opendata.ch returned Varnish 503s for half an hour while search.ch
// kept serving. Plain HTTP works, so no TLS stack is needed on the ESP32.
// Field mapping verified against opendata.ch at a bus stop and at Zürich HB:
//   *G -> category, *L -> number, terminal.name -> destination, operator.
// Payload is ~5'900 bytes for limit=20; subsequent stops are off by default.
// ---------------------------------------------------------------------------
static bool fetchFromSearchCh(const String& encodedStation, std::vector<Departure>& out) {
    String url = "http://search.ch/fahrplan/api/stationboard.json?stop=" + encodedStation
               + "&limit=20&show_delays=1";

    Serial.print("Falling back to: ");
    Serial.println(url);

    HTTPClient http;
    http.useHTTP10(true);
    http.begin(url);
    http.setTimeout(10000);

    int httpCode = http.GET();
    if (httpCode != 200) {
        Serial.printf("search.ch request failed, error code: %d\n", httpCode);
        http.end();
        return false;
    }

    JsonDocument filter;
    filter["connections"][0]["time"] = true;
    filter["connections"][0]["*G"] = true;
    filter["connections"][0]["*L"] = true;
    filter["connections"][0]["line"] = true;
    filter["connections"][0]["operator"] = true;
    filter["connections"][0]["dep_delay"] = true;
    filter["connections"][0]["terminal"]["name"] = true;

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
    if (error) {
        Serial.print("search.ch JSON parsing failed: ");
        Serial.println(error.c_str());
        http.end();
        return false;
    }

    for (JsonObject connection : doc["connections"].as<JsonArray>()) {
        long scheduledTimestamp = parseLocalTimestamp(connection["time"].as<const char*>());
        if (scheduledTimestamp == 0) continue;

        Departure dep;
        dep.category = connection["*G"].as<String>();

        if (!connection["*L"].isNull() && connection["*L"].as<String>() != "") {
            dep.number = connection["*L"].as<String>();
        } else if (!connection["line"].isNull() && connection["line"].as<String>() != "") {
            dep.number = connection["line"].as<String>();
        } else {
            dep.number = dep.category;
        }

        dep.destination = connection["terminal"]["name"].as<String>();
        dep.operatorName = connection["operator"].as<String>();
        dep.scheduledTimestamp = scheduledTimestamp;
        // "+3" / "+0", or absent when no realtime feed exists for this stop.
        // as<const char*>() also yields nullptr if the field is ever not a
        // string; atoi() handles the leading sign and gives 0 for anything else.
        const char* delayText = connection["dep_delay"].as<const char*>();
        dep.delayMinutes = (delayText != nullptr) ? atoi(delayText) : 0;
        dep.actualTimestamp = scheduledTimestamp + (dep.delayMinutes * 60);
        dep.isCancelled = false;
        out.push_back(dep);
    }

    http.end();
    return true;
}

std::vector<Departure> fetchDepartures(const char* station, const String& opMode) {
    // ENCODE THE STATION NAME HERE
    String encodedStation = urlEncode(String(station));

    // One source is one point of failure. Try the primary, and if it is down or
    // hands back nothing usable, ask the other provider for the same data
    // instead of showing an empty board.
    std::vector<Departure> departuresList;
    if (!fetchFromOpendata(encodedStation, departuresList) || departuresList.empty()) {
        departuresList.clear();
        fetchFromSearchCh(encodedStation, departuresList);
    }

    if (departuresList.empty()) {
        Serial.println("Both departure sources unavailable.");
        return {};
    }

    // ==========================================
    // DEDUPLICATION TRACKER
    // Stores "Timestamp_Destination" to catch ghost trains
    // ==========================================
    std::set<String> seenDepartures;
    std::vector<Departure> uniqueDepartures;
    for (const Departure& dep : departuresList) {
        // Generate a unique fingerprint for this specific physical train
        String uniqueFingerprint = String(dep.scheduledTimestamp) + "_" + dep.destination;

        // If we have already seen this exact train, skip it!
        if (seenDepartures.find(uniqueFingerprint) != seenDepartures.end()) {
            continue;
        }

        // Mark this train as seen
        seenDepartures.insert(uniqueFingerprint);
        uniqueDepartures.push_back(dep);
    }

    // Sort: train mode by scheduled time, tram mode by actual time
    if (opMode == "train") {
        std::sort(uniqueDepartures.begin(), uniqueDepartures.end(), [](const Departure& a, const Departure& b) {
            return a.scheduledTimestamp < b.scheduledTimestamp;
        });
    } else {
        std::sort(uniqueDepartures.begin(), uniqueDepartures.end(), [](const Departure& a, const Departure& b) {
            return a.actualTimestamp < b.actualTimestamp;
        });
    }

    // Get the current UTC time from the ESP32
    time_t now;
    time(&now);

    std::vector<Departure> topDepartures;
    int displayedCount = 0;

    // Filter out old vehicles and grab the top 6
    for (const Departure& dep : uniqueDepartures) {
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

    return topDepartures;
}
