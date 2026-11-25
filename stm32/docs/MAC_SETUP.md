# Mac setup (MacBook Air M1, macOS, Apple silicon)

One-time setup. Do steps 1–4 before the first Claude Code session. Steps 5–7 can wait until you
start working with the board.

## 1. Where to put the project folder
- Unzip `MTP_XDP_STM32.zip` by double-clicking it in Finder. You get a folder `MTP_XDP_STM32`.
- Move it to a plain folder in your home directory, e.g. **`~/Projects/MTP_XDP_STM32`**
  (in Finder: Go → Home, create `Projects`).
- **Avoid Downloads, Desktop and Documents.** If iCloud "Desktop & Documents" sync is on, build
  files and git can get corrupted or offloaded. macOS also asks apps for extra permission to
  access those folders.
- Move your STM32CubeIDE project into it as `stm32_project/` (back it up first).

## 2. Command-line basics (make, clang, git)
Open **Terminal** and run:
```
xcode-select --install
```
This installs Apple's Command Line Tools: `make`, `git`, and `clang` (on a Mac, `gcc` is clang).
Check that the host tests run:
```
cd ~/Projects/MTP_XDP_STM32
make -C tests/host          # expect: 33 passed, 0 failed
make -C tests/host asan     # same, with AddressSanitizer/UBSan
```

## 3. Homebrew and the tools
If Homebrew isn't installed, install it from https://brew.sh (one command). Then:
```
brew install --cask gcc-arm-embedded   # official Arm GNU Toolchain (arm-none-eabi-gcc), native on M1
brew install python iperf3 tcpreplay
python3 -m venv ~/Projects/mtp-venv && source ~/Projects/mtp-venv/bin/activate
pip install scapy pyserial matplotlib pandas
```
- `arm-none-eabi-gcc`: command-line cross-compile checks (CubeIDE also bundles its own copy).
- `scapy` builds test packets; `tcpreplay` replays them at a fixed packet rate (floods); `iperf3`
  measures legitimate throughput; `pyserial` reads the board's UART; `matplotlib`/`pandas` make plots.
- Sending raw packets (scapy, tcpreplay) needs `sudo` on macOS.

## 4. STM32 tools
- **Rosetta 2** (needed for STM32CubeIDE): `softwareupdate --install-rosetta --agree-to-license`
- **STM32CubeIDE for macOS:** ST ships an Intel (x86_64) build that runs on M1 through Rosetta 2.
  There is no native Apple-silicon build yet.
- **STM32CubeProgrammer:** has a macOS version (recent releases support Apple silicon). CubeIDE also
  bundles `STM32_Programmer_CLI` inside the app. Claude Code can locate it with
  `find /Applications/STM32CubeIDE.app -name STM32_Programmer_CLI`.
- **lwIP for STM32H5** is not in CubeMX: `git clone --recursive https://github.com/STMicroelectronics/stm32h5-classic-coremw-apps`

## 5. Connecting the board (USB)
- USB cable to the **ST-LINK** USB port of the NUCLEO-H563ZI. No driver is needed on macOS.
- The board's serial console (virtual COM port) appears as `/dev/cu.usbmodem*`. List it with
  `ls /dev/cu.usbmodem*`. To view it yourself: `screen /dev/cu.usbmodemXXXX 115200`
  (quit: Ctrl-A, then K, then Y). Claude Code should read it with a short pyserial script with a
  timeout, not with `screen`.

## 6. Connecting the board (Ethernet)
The MacBook Air M1 **has no Ethernet port.** You need a **USB-C to Ethernet adapter** (any
gigabit adapter works; the board runs at 100 Mbit/s).
- Direct cable Mac ↔ Nucleo RJ45 (no router needed).
- Give the adapter a fixed address: System Settings → Network → (USB LAN adapter) → Details →
  TCP/IP → Configure IPv4: **Manually**, IP `192.168.10.1`, subnet `255.255.255.0`.
  Give the board a static IP of `192.168.10.2` in its lwIP config.
- Test: `ping 192.168.10.2`. If an iperf3 server on the Mac gets no traffic, check
  System Settings → Network → Firewall.
- Alternatively, plug both into the same router and use DHCP. A direct cable gives cleaner measurements.

## 7. Limits of a Mac for the evaluation
- Development, functional tests and **moderate floods** (thousands of packets/s with tcpreplay)
  from the Mac are fine.
- **Line-rate tests** (~148,800 small frames/s at 100 Mbit/s) need a **Linux PC** with a real
  Ethernet port (`pktgen`/`trafgen`). A USB adapter on macOS can't do that reliably. Plan to use a
  lab PC for the final evaluation runs (Feb 2027 in the roadmap).
- Don't run rate or timing measurements from a virtual machine; the VM distorts timing.
- Always record the sender machine, OS, adapter and tool versions with each result.
