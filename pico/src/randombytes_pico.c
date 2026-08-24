/*
 * randombytes_pico.c — Random bytes implementation for RP2040 (Pico)
 *
 * Uses the RP2040's hardware ROSC (Ring Oscillator) as entropy source.
 * This replaces PQClean's default randombytes.c which targets desktop OSes.
 *
 * Note: ROSC-based randomness is NOT cryptographically certified,
 * but is adequate for benchmarking purposes. For production use,
 * a proper TRNG or seeded DRBG would be needed.
 */

#include <stdint.h>
#include <stddef.h>
#include "pico/stdlib.h"
#include "hardware/structs/rosc.h"
#include "randombytes.h"  /* picks up #define randombytes PQCLEAN_randombytes */

/* Get one random bit from the ROSC */
static uint8_t rosc_random_bit(void) {
    return (uint8_t)(rosc_hw->randombit & 1);
}

/* Get one random byte from the ROSC (8 random bits) */
static uint8_t rosc_random_byte(void) {
    uint8_t byte = 0;
    for (int i = 0; i < 8; i++) {
        byte = (byte << 1) | rosc_random_bit();
    }
    return byte;
}

/*
 * PQClean randombytes interface — fill buf with n random bytes
 */
int randombytes(uint8_t *output, size_t n) {
    for (size_t i = 0; i < n; i++) {
        output[i] = rosc_random_byte();
    }
    return 0;
}
