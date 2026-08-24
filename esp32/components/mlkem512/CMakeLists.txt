# ML-KEM-512 (FIPS 203) PQClean clean reference C, isolated component.
set(PQCLEAN "${CMAKE_SOURCE_DIR}/../vendor/pqclean")
set(DIR "${PQCLEAN}/crypto_kem/ml-kem-512/clean")
file(GLOB SRCS "${DIR}/*.c")

idf_component_register(
    SRCS ${SRCS}
    INCLUDE_DIRS "${DIR}"
    REQUIRES pqcrypto
)
target_compile_options(${COMPONENT_LIB} PRIVATE -w -Os)
