/* blink.c — minimal toolchain + flashing sanity check.
 * Blinks the onboard LED and prints over USB serial once per second. */
#include <stdio.h>
#include "pico/stdlib.h"

int main(void) {
    stdio_init_all();
    const uint LED = PICO_DEFAULT_LED_PIN;
    gpio_init(LED);
    gpio_set_dir(LED, GPIO_OUT);
    int i = 0;
    while (true) {
        gpio_put(LED, 1); sleep_ms(250);
        gpio_put(LED, 0); sleep_ms(750);
        printf("# blink %d — Pico toolchain OK\n", ++i);
    }
}
