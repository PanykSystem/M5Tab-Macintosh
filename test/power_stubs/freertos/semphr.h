// Host stand-ins for the FreeRTOS mutex board_power.cpp uses.
#pragma once
typedef void *SemaphoreHandle_t;
#define portMAX_DELAY 0
inline SemaphoreHandle_t xSemaphoreCreateMutex() { static int token; return &token; }
inline int xSemaphoreTake(SemaphoreHandle_t, int) { return 1; }
inline int xSemaphoreGive(SemaphoreHandle_t) { return 1; }
