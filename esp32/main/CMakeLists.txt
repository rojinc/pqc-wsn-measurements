idf_component_register(
    SRCS "bench_esp32.c"
    INCLUDE_DIRS "."
    REQUIRES pqcrypto mldsa44 mlkem512 sphincs128s falcon512
             esp_timer esp_hw_support mbedtls
)
