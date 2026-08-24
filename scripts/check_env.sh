#!/usr/bin/env bash
# check_env.sh — report the state of the macOS toolchains we need.
# Safe & read-only: installs nothing, just prints what's present.
# Run:  bash scripts/check_env.sh

set +e
bold() { printf "\033[1m%s\033[0m\n" "$1"; }
ok()   { printf "  \033[32m✔\033[0m %s\n" "$1"; }
bad()  { printf "  \033[31mx\033[0m %s\n" "$1"; }

echo "=================================================="
echo " PQC-WSN-BENCH environment check  ($(date))"
echo "=================================================="
echo "Host: $(uname -s) $(uname -m) | macOS $(sw_vers -productVersion 2>/dev/null)"
echo

bold "[1] General build tools"
for t in git cmake make python3 pip3; do
  if command -v "$t" >/dev/null 2>&1; then ok "$t -> $(command -v $t) ($($t --version 2>&1 | head -1))"; else bad "$t NOT found"; fi
done
echo

bold "[2] Homebrew"
if command -v brew >/dev/null 2>&1; then ok "brew $(brew --version | head -1)"; else bad "brew NOT found (needed for easy installs)"; fi
echo

bold "[3] RP2040 / Pico toolchain"
if command -v arm-none-eabi-gcc >/dev/null 2>&1; then ok "arm-none-eabi-gcc ($(arm-none-eabi-gcc --version | head -1))"; else bad "arm-none-eabi-gcc NOT found"; fi
if [ -n "$PICO_SDK_PATH" ]; then
  if [ -d "$PICO_SDK_PATH" ]; then ok "PICO_SDK_PATH=$PICO_SDK_PATH (exists)"; else bad "PICO_SDK_PATH set but missing: $PICO_SDK_PATH"; fi
else
  bad "PICO_SDK_PATH not set"
  for p in "$HOME/pico-sdk" "$HOME/pico/pico-sdk" "/opt/pico-sdk"; do
    [ -d "$p" ] && echo "      (found a pico-sdk at $p — you can: export PICO_SDK_PATH=$p)"
  done
fi
command -v picotool >/dev/null 2>&1 && ok "picotool present" || echo "  (picotool optional — drag-drop UF2 works without it)"
echo

bold "[4] ESP32 / ESP-IDF toolchain"
if command -v idf.py >/dev/null 2>&1; then
  ok "idf.py on PATH ($(idf.py --version 2>&1 | head -1))"
else
  bad "idf.py not on PATH (ESP-IDF not exported in this shell)"
  for p in "$HOME/esp/esp-idf" "$HOME/esp-idf" "$HOME/.espressif"; do
    [ -d "$p" ] && echo "      (found ESP-IDF-ish dir at $p — activate with: . $p/export.sh)"
  done
fi
command -v xtensa-esp32-elf-gcc >/dev/null 2>&1 && ok "xtensa-esp32-elf-gcc present" || echo "  (xtensa gcc appears after ESP-IDF install/export)"
echo

bold "[5] Python serial capture"
if python3 -c "import serial; print(serial.__version__)" >/dev/null 2>&1; then
  ok "pyserial $(python3 -c 'import serial; print(serial.__version__)')"
else
  bad "pyserial NOT installed  (fix: pip3 install pyserial)"
fi
echo

bold "[6] Connected boards / serial ports"
ls /dev/cu.* 2>/dev/null | grep -Ei 'usb|acm|modem|slab|wch' && echo "  (^ candidate board ports)" || bad "no obvious USB serial ports found — plug a board in and re-run"
echo "  All /dev/cu.* devices:"; ls /dev/cu.* 2>/dev/null | sed 's/^/    /'
echo
echo "=================================================="
echo " Done. Copy everything above and paste it back."
echo "=================================================="
