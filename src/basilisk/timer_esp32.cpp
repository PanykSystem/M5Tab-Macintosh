/*
 *  timer_esp32.cpp - Time Manager emulation for ESP32
 *
 *  BasiliskII ESP32 Port
 */

#include "sysdeps.h"
#include "timer.h"

#define DEBUG 0
#include "debug.h"

/*
 *  Return microseconds since boot
 */
uint64 GetTicks_usec(void)
{
    return (uint64)micros();
}

/*
 *  Delay for specified number of microseconds
 */
void Delay_usec(uint64 usec)
{
    if (usec > 0) {
        if (usec > 16000) {
            // For longer delays, use delay() to allow other tasks
            delay(usec / 1000);
        } else {
            delayMicroseconds((uint32_t)usec);
        }
    }
}

/*
 *  Suspend emulator thread, wait for wakeup
 *  (Not used in single-threaded ESP32 implementation)
 */
void idle_wait(void)
{
    // In single-threaded mode, just yield briefly
    yield();
}

/*
 *  Resume execution of emulator thread
 *  (Not used in single-threaded ESP32 implementation)
 */
void idle_resume(void)
{
    // No-op in single-threaded mode
}
