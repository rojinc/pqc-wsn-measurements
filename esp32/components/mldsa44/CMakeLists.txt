# ML-DSA-44 (FIPS 204) PQClean clean reference C, isolated component.
set(PQCLEAN "${CMAKE_SOURCE_DIR}/../vendor/pqclean")
set(DIR "${PQCLEAN}/crypto_sign/ml-dsa-44/clean")
file(GLOB SRCS "${DIR}/*.c")

idf_component_register(
    SRCS ${SRCS}
    INCLUDE_DIRS "${DIR}"
    REQUIRES pqcrypto
)
target_compile_options(${COMPONENT_LIB} PRIVATE -w -Os)
