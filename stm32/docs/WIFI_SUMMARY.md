# Wi-Fi hook: summary for Kiran (24 Nov 2026)

Branch: `wifi-hook` (created from `main` at `26687a8`). Status: **analysis and plan only, nothing
implemented yet.** The full plan is in `docs/WIFI_PLAN.md`. This page is the short version.

## 1. Not every Wi-Fi module allows our hook

The STM32H563 has no Wi-Fi radio. Wi-Fi comes from an external module, connected over SPI or SDIO.
There are two types:

| Type | Where TCP/IP runs | What the STM32 receives | Frame hook possible? |
|---|---|---|---|
| Socket offload (AT commands, EMW3080 socket mode) | on the module | socket data only | **No** |
| Host stack ("bypass" / "network split", e.g. ESP-Hosted) | **lwIP on the STM32** | **802.3 Ethernet frames** over SPI | **Yes** |

So the project only works with a **host-stack** module.

## 2. Most of our code is reused unchanged

A host-stack module converts 802.11 to 802.3 before it sends frames to the STM32. The frames
therefore have the same Ethernet II layout, and the same offsets 12, 23 and 36 apply.

**Reused unchanged:**
- the VM, verifier, `vm_slot` and loader;
- the DDoS bytecode and the native C filter;
- `hook_common.h`, the lwIP hook, configs C0–C4;
- the traces, runner, FreeRTOS setup and the tests.

**New:**
- a thin `wifi_rx_hook` wrapper;
- a simulated Wi-Fi module plus SPI transport driver (`sim/wifi_sim/`).

## 3. The hook point is different, and that matters for the results

RX path in host-stack mode:

**data-ready IRQ → SPI transport task → SPI transfer → our hook → pbuf copy → `tcpip_input()` → mailbox → `tcpip_thread`**

- **Saved by an early drop:** the pbuf allocation and copy, the mailbox post, the task switch and
  lwIP's processing.
- **Not saved:** the SPI transfer, because the frame has already crossed the bus before the STM32 can
  look at it. Under a flood, the SPI bus becomes the limit.

Protecting the bus as well would need the filter to run on the module itself (an ESP32 could run our
VM). That goes in the thesis as future work.

## 4. Wi-Fi-specific points for the thesis
- **802.11 management attacks** (deauthentication floods) are handled inside the module. They are out
  of scope.
- **Port 1900 (SSDP)** carries legitimate multicast discovery on Wi-Fi networks, so a unicast-only rule
  would be better. Optional step W7 shows one, loaded over the control port.

## 5. Plan (steps W1–W8)
- **W1:** read the real driver source and record its facts, as we did for ST's Ethernet driver.
- **W2–W4:** the hook wrapper with tests, the module and SPI model, and the FreeRTOS build.
- **W5–W6:** measurements, an Ethernet-vs-Wi-Fi comparison, and a flood model with an SPI bandwidth
  limit.
- **W7–W8:** the optional unicast rule, then docs.

That is about one working day. Since Day 2 finished early, it fits on 25 Nov, and the report and
slides stay on 26 Nov.

## 6. Recommendation
- **Reference module: ESP32 with ESP-Hosted.** It is open source (so we can read the driver), STM32
  hosts are documented, and it delivers 802.3 frames to lwIP.
- The **EMW3080** is ST's own module, but we couldn't confirm that its bypass mode works with lwIP.
- **In the report:** a **portability chapter**, not a main result. The frozen resume bullets don't
  mention Wi-Fi.

**Open decision:** which module the simulation follows (ESP32/ESP-Hosted recommended, or EMW3080).

## Sources (web search, 24 Nov 2026; to be confirmed by reading the driver source in W1)
- [espressif/esp-hosted (GitHub)](https://github.com/espressif/esp-hosted)
- [ESP-Hosted FG documentation](https://www.github.com/espressif/esp-hosted/blob/master/esp_hosted_fg)
- [esp_hosted network_split mcu_host example](https://components.espressif.com/components/espressif/esp_hosted/versions/3.1.0/examples/network_split/station/mcu_host)
- [ST: X-WIFI-EMW3080B](https://st.com/en/development-tools/x-wifi-emw3080b.html)
- [ST community: Wi-Fi on B-U585I-IOT02A using SPI](https://community.st.com/stm32-mcus-wireless-35/wifi-on-b-u585i-iot2a-using-spi-126715)
- [ST community: EMW3080 NetXDuo driver for STM32H5](https://community.st.com/stm32-mcus-products-25/emw3080-netxduo-driver-for-stm32h5-117564)
- [ST community: mx_wifi socket non-blocking issue](https://community.st.com/stm32-mcus-embedded-software-32/mx-wifi-socket-setsockopt-rejects-mx-so-blockmode-0x1000-on-emw3080-non-blocking-sockets-unusable-select-is-a-stub-168714)
