#ifndef CoreSerial_h
#define CoreSerial_h

#include <Arduino.h>
#include <stdarg.h>

/**
 * @brief Provides Serial printing with core ID prefix (printed once per line).
 * This is useful for debugging in multi-core environments.
 * It does not support formatted printing (e.g., printf).
 *
 * Usage:
 * #include "CoreSerial.h"
 * #define Serial SerialCore
 * After this, use Serial.print/println as usual.
 */
class CoreSerial
{
public:
    /**
     * @brief Initializes the Serial communication.
     * @param baudrate The baud rate to use.
     */
    void begin(unsigned long baudrate);

    /**
     * @brief Prints a message with optional core ID prefix (no newline).
     * @tparam T Type of message to print.
     * @param msg The message to print.
     */
    template <typename T>
    void print(const T &msg)
    {
        ensurePrefix();
        Serial.print(msg);
    }

    /**
     * @brief Prints a message with core ID prefix (adds newline).
     * @tparam T Type of message to print.
     * @param msg The message to print.
     */
    template <typename T>
    void println(const T &msg)
    {
        ensurePrefix();
        Serial.println(msg);
        _newLine = true;
    }

    /**
     * @brief Prints a newline only (resets prefix flag).
     */
    void println();

    /**
     * @brief Flushes the Serial buffer.
     */
    void flush();

    /**
     * @brief Checks if data is available in the Serial buffer.
     * @return True if available, false otherwise.
     */
    bool available();

    /**
     * @brief Reads a byte from the Serial buffer.
     * @return The read byte.
     */
    int read();

private:
    bool _newLine = true; ///< Tracks if a new line started

    /**
     * @brief Prints the core ID prefix if starting a new line.
     */
    void ensurePrefix();

    /**
     * @brief Prints the actual [core_id] prefix.
     */
    void printCorePrefix();
};

/**
 * @brief Global instance of CoreSerial.
 */
extern CoreSerial SerialCore;

#endif
