# SLH-DSA-128s (SPHINCS+-SHA2-128s-simple, FIPS 205) PQClean clean reference C.
# Hash-based: needs common/sha2.c (pure software — the ESP32 SHA accelerator is
# NOT used, so this is a like-for-like comparison with the RP2040 build).
# Signing is MEASURED at 78.95 s/sig on ESP32 — not the ~1-2 s an earlier comment
# here estimated. Use the reduced SPX_N_* in main/bench_config.h; see RUNBOOK.md.
set(PQCLEAN "${CMAKE_SOURCE_DIR}/../vendor/pqclean")
set(DIR "${PQCLEAN}/crypto_sign/sphincs-sha2-128s-simple/clean")
file(GLOB SRCS "${DIR}/*.c")

idf_component_register(
    SRCS ${SRCS} "${PQCLEAN}/common/sha2.c"
    INCLUDE_DIRS "${DIR}" "${PQCLEAN}/common"
    REQUIRES pqcrypto
)
target_compile_options(${COMPONENT_LIB} PRIVATE -w -Os)
