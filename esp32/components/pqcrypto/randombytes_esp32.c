/*
 * randombytes_esp32.c — PQClean randombytes() backed by the ESP32 hardware RNG.
 * esp_fill_random() draws from the RNG; when RF (WiFi/BT) is off it is seeded by
 * the SAR ADC / internal noise. Adequate for benchmarking; production would use
 * a certified DRBG. Signature matches PQClean common/randombytes.h.
 */
#include <stddef.h>
#include <stdint.h>
#include "esp_random.h"
#include "randombytes.h"

int randombytes(uint8_t *output, size_t n) {
    esp_fill_random(output, n);
    return 0;
}
