# pqcrypto = shared PQClean common code (Keccak/SHAKE + ESP32 randombytes).
# Each algorithm lives in its OWN component (mldsa44/, mlkem512/, ...) so their
# identically-named clean headers (api.h, params.h, poly.h) never collide.
set(PQCLEAN "${CMAKE_SOURCE_DIR}/../vendor/pqclean")

idf_component_register(
    SRCS "${PQCLEAN}/common/fips202.c"
         "randombytes_esp32.c"
    INCLUDE_DIRS "${PQCLEAN}/common"
    PRIV_REQUIRES esp_hw_support
)
target_compile_options(${COMPONENT_LIB} PRIVATE -w -Os)
