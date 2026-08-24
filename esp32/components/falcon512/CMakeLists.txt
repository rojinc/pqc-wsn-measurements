# FN-DSA-512 / FALCON-512 PQClean clean reference C, isolated component.
# NOTE: PQClean ships "falcon-512" (round-3 Falcon), which predates final
# FIPS 206 / FN-DSA. Report it as "Falcon-512 (FN-DSA draft)" in the paper.
# fpr.c is the floating-point emulation layer — on ESP32 the FPU is used for
# single-precision, but Falcon needs doubles, so this still runs in software.
set(PQCLEAN "${CMAKE_SOURCE_DIR}/../vendor/pqclean")
set(DIR "${PQCLEAN}/crypto_sign/falcon-512/clean")
file(GLOB SRCS "${DIR}/*.c")

idf_component_register(
    SRCS ${SRCS}
    INCLUDE_DIRS "${DIR}"
    REQUIRES pqcrypto
)
target_compile_options(${COMPONENT_LIB} PRIVATE -w -Os)
