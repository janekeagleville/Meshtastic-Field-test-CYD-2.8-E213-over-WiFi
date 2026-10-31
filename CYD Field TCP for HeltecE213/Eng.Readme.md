# CYD Field TCP for HeltecE213

Two boards linked **over WiFi, no cable**:

- **Heltec Vision Master E213** (e-ink) does the radio measurements with its own SX1262, as in the "CYD Field for HeltecE213" project. It also runs its own WiFi access point and a **Meshtastic client API server over TCP (port 4403)**.
- **CYD 2.8" (ESP32-2432S028)** connects to the E213 the way the "CYD Field for HeltecV4" project connected to a Heltec V4 over UART. It provides the colour UI: node list, map, link test, chat, spectrum and SD logging.

The E213 behaves like a Meshtastic node towards the CYD, so the CYD firmware is almost unchanged: TCP replaces the serial port. The two existing projects are untouched; this is a separate project.

> Note: tested on a host (E213 and CYD simulations plus a cross-protocol test) and compiled for ESP32 (Arduino core 3.x). **Not yet run on the boards, and not built with the `espressif32@6.9.0` platform pinned in `platformio.ini`.**

## How it works
1. The E213 starts an access point (SSID/password in `include/link.h`, default `MeshField-E213` / `mazowsze213`) and listens on 192.168.4.1:4403.
2. The CYD joins that network, opens TCP to 192.168.4.1:4403 and sends `want_config_id`.
3. The E213 answers like Meshtastic firmware: `my_info`, its own NodeInfo (name, manual position, battery, airtime), NodeInfo of heard nodes, LoRa config (preset, SF, BW, exact frequency), channel 0 and `config_complete_id`.
4. Every LoRa packet the E213 receives is forwarded with RSSI, SNR, hops and time. Default-key packets go decoded; other channels go encrypted but with RSSI/SNR.
5. Packets from the CYD (link test with `want_ack`) are encrypted with the default channel key and transmitted; the target's ACK flows back like any received packet.
6. If the E213 cannot transmit it returns a routing error like Meshtastic does: `DUTY_CYCLE_LIMIT`, `NO_INTERFACE` (E213 on the SCAN screen), `TIMEOUT` (radio busy over 5 s) or `NO_RESPONSE` (commands addressed to the E213 itself, e.g. GPS toggle).
7. Every 60 s the E213 sends its telemetry; the CYD E213 tile shows it.

## Usage
- **E213**: new SETUP item **WiFi link (CYD)** (`OFF` / `ON waiting` / `CYD <ip>`, stored, on by default); the header shows `AP` or an inverted `CYD` when linked. On the SCAN screen no packets reach the CYD.
- **CYD**: same UI as "CYD Field for HeltecV4"; the waiting screen shows the link state and hints, the E213 tile shows WiFi signal instead of baud rate; automatic reconnect (WiFi every 15 s, TCP every 3 s) with a fresh config download.

## Build
Open this folder in VS Code with PlatformIO, pick the environment on the status bar and upload each board: `env:e213`, `env:cyd_st7789_noinv` (or `cyd_st7789` / `cyd_ili9341`). To change the network name or password edit `include/link.h` and flash **both** boards.

## Host tests
`./tools/host_test.sh` runs the cross-protocol test (`test/link_test.cpp`), the E213 simulation with a virtual access point and TCP client, the CYD simulation with virtual WiFi/TCP, unit tests and the configurator tests.
