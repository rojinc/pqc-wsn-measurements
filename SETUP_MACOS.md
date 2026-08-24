# macOS Setup — toolchains for RP2040 + ESP32

Do this once. **Step 0 first**, then only install what's missing.

## Step 0 — check what you already have

```bash
cd ~/Desktop/research/pqc-wsn-bench
bash scripts/check_env.sh
```

Paste the whole output back to me. Everything below is only needed for the ✗ items.

---

## A. Common tools

```bash
# Homebrew (if the check said it's missing)
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

brew install cmake git python3
pip3 install pyserial
```

## B. RP2040 / Pico toolchain

```bash
# ARM bare-metal compiler
brew install --cask gcc-arm-embedded     # provides arm-none-eabi-gcc
# (alternative: brew install arm-none-eabi-gcc)

# Pico SDK (v2.2.0 to match the published paper)
git clone https://github.com/raspberrypi/pico-sdk.git ~/pico-sdk
cd ~/pico-sdk && git checkout 2.2.0 && git submodule update --init && cd -

# Make it available to every shell
echo 'export PICO_SDK_PATH="$HOME/pico-sdk"' >> ~/.zshrc
export PICO_SDK_PATH="$HOME/pico-sdk"
```

We also need PQClean (the reference C crypto), pinned to the same commit as the paper:

```bash
git clone https://github.com/PQClean/PQClean.git ~/Desktop/research/pqc-wsn-bench/pico/pqclean
cd ~/Desktop/research/pqc-wsn-bench/pico/pqclean && git checkout 3730b32a && cd -
```

**Flashing a Pico:** hold the BOOTSEL button while plugging in USB → it mounts as a
`RPI-RP2` drive → copy the `.uf2` onto it. It reboots and runs immediately.

## C. ESP32 / ESP-IDF toolchain

```bash
# Prereqs
brew install cmake ninja dfu-util

# ESP-IDF v5.x
mkdir -p ~/esp && cd ~/esp
git clone -b v5.2.1 --recursive https://github.com/espressif/esp-idf.git
cd esp-idf && ./install.sh esp32 && cd -

# Activate ESP-IDF in a shell BEFORE building (do this each new terminal):
. ~/esp/esp-idf/export.sh
```

Handy alias so you don't retype the export:

```bash
echo "alias get_idf='. $HOME/esp/esp-idf/export.sh'" >> ~/.zshrc
# then just run:  get_idf
```

**Flashing an ESP32:** it appears as `/dev/cu.usbserial-*` or `/dev/cu.SLAB_USBtoUART`.
`idf.py -p <port> flash monitor` builds, flashes, and opens the serial monitor.
If the port doesn't appear, install the USB-UART driver for your board's chip
(CP210x → Silicon Labs; CH340 → WCH).

---

## Sanity checks (optional but recommended)

- **Pico:** we'll build a `blink`/hello target first to confirm the toolchain + flashing.
- **ESP32:** we'll build ESP-IDF's `hello_world` example first for the same reason.

Once `check_env.sh` looks clean (or you've installed the missing pieces), tell me and
Then build the firmware as described in RUNBOOK.md.
