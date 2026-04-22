#ifndef TRANSPORT_API_H
#define TRANSPORT_API_H

#include <Arduino.h>
#include <vector>

// Define the structure of a single departure
struct Departure {
    String category;
    String number;
    String destination;
    long scheduledTimestamp; // Planned departure time
    long actualTimestamp;    // Actual departure time (scheduled + delay)
    int delayMinutes;        // Delay in minutes (0 = on time)
    bool isCancelled;
    String operatorName;
};

// opMode: "tram" sorts by actual time, "train" sorts by scheduled time
std::vector<Departure> fetchDepartures(const char* station, const String& opMode);

#endif