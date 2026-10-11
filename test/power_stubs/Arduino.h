// Host stand-ins for the Arduino calls board_power.cpp makes.
#pragma once
#include <cstdint>
#include <cstdio>

extern uint32_t g_fake_millis;
inline uint32_t millis() { return g_fake_millis; }

struct FakeSerial {
    template <typename... Args> int printf(const char *, Args...) { return 0; }
    void println(const char * = "") {}
};
extern FakeSerial Serial;
