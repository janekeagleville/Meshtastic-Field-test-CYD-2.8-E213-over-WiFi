CYD Field TCP for HeltecE213

Zestaw dwóch płytek połączonych **bez kabla, przez WiFi**:

- **Heltec Vision Master E213** (e-ink) robi pomiary radiowe swoim SX1262, jak w projekcie „CYD Field for HeltecE213”. Dodatkowo tworzy własną sieć WiFi i serwer **API Meshtastic po TCP (port 4403)**.
- **CYD 2,8" (ESP32-2432S028)** łączy się z E213 tak, jak w projekcie „CYD Field for HeltecV4” łączył się kablem z Heltec V4. Pokazuje kolorowy interfejs: listę węzłów, mapę, test łącza, czat, widmo i log na kartę SD.




> Uwaga: oba firmware są przetestowane na hoście (symulacje E213 i CYD, test zgodności protokołu między nimi) i skompilowane dla ESP32 (rdzeń Arduino 3.x). **Nie były jeszcze uruchamiane na płytkach ani budowane na platformie `espressif32@6.9.0` z `platformio.ini`.**

## Jak to działa
```
 sieć Meshtastic ))) LoRa ))) E213  ---- WiFi "MeshField-E213" ----  CYD 2,8"
                              192.168.4.1:4403 (API Meshtastic)     klient API, ekran kolorowy
```
1. E213 uruchamia punkt dostępowy WiFi (nazwa i hasło w `include/link.h`, domyślnie `MeshField-E213` / `mazowsze213`) i nasłuchuje na porcie 4403.
2. CYD łączy się z tą siecią i otwiera połączenie TCP do `192.168.4.1:4403`. Wysyła `want_config_id` jak do zwykłego węzła.
3. E213 odpowiada jak firmware Meshtastic:
   - `my_info`, czyli swój identyfikator;
   - `NodeInfo` siebie: nazwa, pozycja wpisana w konfiguratorze, bateria, zajętość eteru;
   - `NodeInfo` wszystkich słyszanych węzłów;
   - konfigurację LoRa: preset, SF, BW i dokładną częstotliwość;
   - kanał 0;
   - `config_complete_id`.
4. Potem każdy pakiet LoRa odebrany przez E213 trafia do CYD z RSSI, SNR, hopami i czasem. Kanał z domyślnym kluczem idzie odszyfrowany: nazwy, pozycje, wiadomości, ACK. Inne kanały idą zaszyfrowane, ale z RSSI i SNR, więc ich nadawcy też są na liście CYD.
5. Pakiety od CYD (test łącza z `want_ack` po wybraniu węzła) E213 szyfruje domyślnym kluczem kanału i nadaje swoim radiem. ACK z węzła docelowego wraca do CYD jak każdy odebrany pakiet.
6. Gdy E213 nie może nadać, odsyła błąd routingu jak Meshtastic, a CYD liczy próbę jako nieudaną:
   - limit wypełnienia: `DUTY_CYCLE_LIMIT`;
   - E213 na ekranie SCAN: `NO_INTERFACE`;
   - radio zajęte dłużej niż 5 s: `TIMEOUT`;
   - komenda do samego E213, np. włączenie GPS: `NO_RESPONSE`.
7. Co 60 s E213 wysyła swoją telemetrię (bateria, zajętość eteru, czas pracy). Kafelek E213 na CYD pokazuje stan E213.

## Obsługa
**E213** działa tak jak w projekcie „CYD Field for HeltecE213”, z kilkoma dodatkami:
- SETUP ma nową pozycję **WiFi link (CYD)**: `OFF` / `ON waiting` / `CYD 192.168.4.2` (adres podłączonego CYD). Ustawienie jest zapisywane, domyślnie włączone.
- W nagłówku obok nazwy ekranu jest `AP` (sieć włączona, CYD niepodłączony) albo odwrócone `CYD` (CYD połączony).
- Na ekranie SCAN radio przemiata pasmo, więc do CYD nie płyną pakiety. Wróć na HOME albo MESH.
- Stan BLE dla konfiguratora ma dwa nowe pola: `wifi` i `cyd`.

**CYD** wygląda i działa jak w projekcie „CYD Field for HeltecV4”. Zmiany:
- Na ekranie oczekiwania widać stan łącza: `searching WiFi` / `WiFi ok, no E213 API` / `E213 linked`, wraz z podpowiedziami.
- Kolory i układ są te same co w pierwszym projekcie CYD: ciemnozielone tło, turkusowe i bursztynowe akcenty, zielone przyciski (ta sama paleta w `src/cyd/main.cpp`).
- Kafelek TRACKER/DEVICE nazywa się teraz **E213** i zamiast prędkości portu („115200 baud”) pokazuje siłę sygnału WiFi.
- Po zerwaniu połączenia CYD sam łączy się ponownie: z WiFi co 15 s, z TCP co 3 s. Po każdym połączeniu pobiera konfigurację od nowa.
- GPS: E213 nie ma odbiornika GPS. W logu CYD pozycją jest ta wpisana ręcznie w konfiguratorze E213. Przycisk włączania GPS na CYD dostaje odmowę.

## Budowanie (VS Code + PlatformIO)
To jeden projekt z kilkoma środowiskami. Otwórz w VS Code **ten folder** (File → Open Folder), wybierz środowisko na dolnym pasku i wgraj każdą płytkę osobno:

| Środowisko | Płytka |
|---|---|
| `env:e213` | Heltec Vision Master E213 |
| `env:cyd_st7789_noinv` | CYD 2,8" z panelem ST7789 bez inwersji (jak w „CYD Field for HeltecV4”) |
| `env:cyd_st7789`, `env:cyd_ili9341` | inne panele CYD |

Z terminala: `pio run -e e213 -t upload`, potem `pio run -e cyd_st7789_noinv -t upload`. Przed wgraniem rozłącz monitor portu (np. ESP Decoder). Gdyby E213 nie dał się wgrać, przytrzymaj PRG, naciśnij RST i puść PRG.

**Zmiana nazwy lub hasła sieci:** edytuj `include/link.h` i wgraj **obie** płytki (hasło musi mieć co najmniej 8 znaków).

## Struktura
| Ścieżka | Zawartość |
|---|---|
| `include/link.h` | wspólne ustawienia sieci: SSID, hasło, port, kanał WiFi, adres E213 |
| `src/e213/`, `include/e213/` | firmware E213; `api.h` = serwer API Meshtastic (kodowanie FromRadio, dekodowanie ToRadio) |
| `src/cyd/`, `include/cyd/` | firmware CYD; `tcplink.h` = połączenie WiFi/TCP z E213 |
| `tools/e213/configurator.html` | konfigurator E213 przez Bluetooth (jak wcześniej) |
| `tools/cyd/` | generatory map CYD |
| `test/` | testy na hoście |

## Do sprawdzenia na sprzęcie
- WiFi i Bluetooth naraz na E213 (ESP32-S3 dzieli radio; powinno działać, ale konfigurator BLE może zwolnić).
- Zasięg WiFi między płytkami (punkt dostępowy ESP32: zwykle kilkadziesiąt metrów w terenie otwartym) i pobór prądu E213 z włączonym WiFi. Na samej baterii warto wyłączać WiFi w SETUP, gdy CYD nie jest używany.
- Czas na CYD: zegar E213 ustawiany w konfiguratorze jest czasem polskim; E213 przelicza go na UTC dla CYD.
- Pozostałe punkty z README projektów E213 i CYD V4 (panel e-ink, bateria, kalibracja dotyku itd.).

## Testy na hoście
```
./tools/host_test.sh
```
- `test/link_test.cpp`: ramki z serwera E213 czytane prawdziwym parserem CYD (konfiguracja, NodeInfo, pakiety, błędy routingu, telemetria, wszystkie 11 presetów) i ramki CYD (test łącza, `want_config`, heartbeat) czytane parserem E213.
- `test/e213/sim.cpp`: cały firmware E213 z wirtualnym radiem, punktem dostępowym i klientem TCP:
  - zrzut konfiguracji;
  - przekazywanie pakietów odszyfrowanych i zaszyfrowanych;
  - nadawanie pakietu klienta i powrót ACK;
  - limity i błędy;
  - telemetria;
  - rozłączenie, ponowne połączenie i rozłączenie po bezczynności;
  - wyłączenie WiFi w SETUP.
- `test/cyd/probe_sim.cpp`: firmware CYD z wirtualnym WiFi/TCP (test łącza, ACK, NAK, timeout, log).
- Testy jednostkowe E213 i CYD oraz testy konfiguratora (node).
