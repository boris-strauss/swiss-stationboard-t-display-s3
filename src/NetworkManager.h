#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

#include <Arduino.h>

void setupNetwork();
void handleNetworkLoop();
void triggerCaptivePortal();
String getTargetStation();
String getOperationMode();

#endif