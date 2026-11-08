#include "CoreSerial.h"

void CoreSerial::begin(unsigned long baudrate)
{
    Serial.begin(baudrate);
}

void CoreSerial::flush()
{
    Serial.flush();
}

bool CoreSerial::available()
{
    return Serial.available();
}

int CoreSerial::read()
{
    return Serial.read();
}

void CoreSerial::println()
{
    Serial.println();
    _newLine = true;
}

void CoreSerial::ensurePrefix()
{
    if (_newLine)
    {
        printCorePrefix();
        _newLine = false;
    }
}

void CoreSerial::printCorePrefix()
{
    Serial.print("[");
    Serial.print(xPortGetCoreID());
    Serial.print("] ");
}

// Define the global instance
CoreSerial SerialCore;
