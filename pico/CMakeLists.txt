cmake_minimum_required(VERSION 3.13)
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)
project(pqc_wsn_bench_pico C CXX ASM)
set(CMAKE_C_STANDARD 11)

# ---------------------------------------------------------------------------
# OPTIMISATION LEVEL — must match the ESP32 build or cross-platform ratios are
# not comparable.
#
# The ESP32 side is uniformly -Os: every component sets it explicitly and the
# project default is CONFIG_COMPILER_OPTIMIZATION_SIZE=y. This build was NOT:
# CMake's Release default is -O3, and only the per-algorithm libraries carried
# an explicit -Os. That left pqclean_common (fips202.c/SHAKE and sha2.c/SHA-256
# — the hash layer every scheme depends on), the harness, and pico_mbedtls at
# -O3 while the scheme code around them was at -Os.
#
# Consequence: RP2040/ESP32 cycle ratios were flattered on the RP2040 side in
# proportion to how hash-dominated each scheme is, and mbedTLS ECDSA was
# compared -O3 against -Os — which is why ECDSA was the only operation in the
# set to come out FASTER per cycle on the M0+ than on the ESP32 (0.80x sign,
# 0.84x verify) against a PQC trend of 1.13x - 2.26x.
#
# Forcing -Os here makes the whole image match the ESP32. Anything captured
# before 2026-07-28 on this platform predates the fix.
# ---------------------------------------------------------------------------
set(CMAKE_C_FLAGS_RELEASE   "-Os -DNDEBUG" CACHE STRING "" FORCE)
set(CMAKE_CXX_FLAGS_RELEASE "-Os -DNDEBUG" CACHE STRING "" FORCE)

pico_sdk_init()

# PQClean lives outside this dir (shared with the ESP32 build).
set(PQCLEAN ${CMAKE_CURRENT_SOURCE_DIR}/../vendor/pqclean)
set(PQCOMMON ${PQCLEAN}/common)

# ---- shared PQClean common (Keccak/SHAKE, SHA-2) + RP2040 randombytes ----
add_library(pqclean_common STATIC
    ${PQCOMMON}/fips202.c
    ${PQCOMMON}/sha2.c
    src/randombytes_pico.c
)
target_include_directories(pqclean_common PUBLIC ${PQCOMMON})
target_link_libraries(pqclean_common pico_stdlib hardware_structs)
# -Os explicitly, belt-and-braces with CMAKE_C_FLAGS_RELEASE above. This target
# holds the SHAKE and SHA-256 cores shared by every scheme; it was the one that
# silently sat at -O3 while the schemes calling it were at -Os.
target_compile_options(pqclean_common PRIVATE -w -Os)

# ---- one static lib per algorithm (their clean headers collide otherwise) ----
function(add_pqc_lib name path)
    file(GLOB SRCS "${path}/*.c")
    add_library(${name} STATIC ${SRCS})
    target_include_directories(${name} PUBLIC ${path} ${PQCOMMON})
    target_link_libraries(${name} pqclean_common)
    target_compile_options(${name} PRIVATE -w -Os)
endfunction()

add_pqc_lib(mlkem512    ${PQCLEAN}/crypto_kem/ml-kem-512/clean)
add_pqc_lib(mldsa44     ${PQCLEAN}/crypto_sign/ml-dsa-44/clean)
add_pqc_lib(falcon512   ${PQCLEAN}/crypto_sign/falcon-512/clean)
add_pqc_lib(sphincs128s ${PQCLEAN}/crypto_sign/sphincs-sha2-128s-simple/clean)

# ---- benchmark firmware ----
# 192 KB stack: large enough to PAINT past the worst case (ML-DSA-87 needs
# ~120 KB) so the stack measurement can never silently saturate — this is the
# fix for the arXiv-v1 bug. Stack lives in main RAM via the custom linker script.
add_executable(bench_pico src/bench_pico.c)
target_include_directories(bench_pico PRIVATE src)
target_link_libraries(bench_pico
    pico_stdlib
    hardware_clocks      # clock_get_hz(clk_sys) — the harness measures the clock
    pico_mbedtls
    pqclean_common
    mlkem512 mldsa44 falcon512 sphincs128s
)
target_compile_definitions(bench_pico PRIVATE PICO_STACK_SIZE=0x30000)
pico_set_linker_script(bench_pico ${CMAKE_CURRENT_SOURCE_DIR}/memmap_custom.ld)
pico_add_extra_outputs(bench_pico)
pico_enable_stdio_usb(bench_pico 1)
pico_enable_stdio_uart(bench_pico 0)

# blink: quick toolchain/flash sanity check before the real runs
add_executable(blink src/blink.c)
target_link_libraries(blink pico_stdlib)
pico_add_extra_outputs(blink)
pico_enable_stdio_usb(blink 1)
pico_enable_stdio_uart(blink 0)
