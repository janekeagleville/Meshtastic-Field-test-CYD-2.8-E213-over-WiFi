#!/usr/bin/env bash
# Testy na hoscie (bez plytek): zgodnosc lacza E213 <-> CYD, symulacje obu firmware, konfigurator BLE.
# Wymaga: g++, python3 z Pillow (podglady e-ink), node (testy konfiguratora, opcjonalnie).
set -euo pipefail
cd "$(dirname "$0")/.."
O=.cache/host; mkdir -p "$O" shots
SAN="-fsanitize=address,undefined -g -O1"
export ASAN_OPTIONS=detect_leaks=0
echo "== lacze E213 <-> CYD (ramki serwera E213 czytane parserem CYD i odwrotnie)"
g++ -std=c++17 $SAN test/link_test.cpp -o $O/link && $O/link 2>/dev/null
echo "== E213: testy jednostkowe i symulacja (radio, przyciski, e-ink, WiFi, API TCP)"
g++ -std=c++17 $SAN -Istubs/e213 -Iinclude/e213 test/e213/units.cpp -o $O/e213units && $O/e213units >/dev/null 2>&1 && echo "units OK"
g++ -std=c++17 $SAN -DHOST_SIM -DARDUINO=10800 -Istubs/e213 -Iinclude/e213 -Iinclude -Ithird_party/Adafruit_GFX test/e213/sim.cpp \
  third_party/Adafruit_GFX/Adafruit_GFX.cpp third_party/Adafruit_GFX/glcdfont.c -o $O/e213sim
$O/e213sim 2>&1 | tail -3
python3 tools/e213/render.py shots >/dev/null && echo "podglady e-ink: shots/*.png"
if command -v node >/dev/null; then node test/e213/configurator_test.js json_options.txt json_status.txt | tail -1; node test/e213/configurator_dom_test.js tools/e213/configurator.html json_options.txt json_status.txt | tail -1; fi
echo "== CYD: parser, pulpit, symulacja (lacze TCP z E213)"
for t in protocol_test dash_test probe_sim; do g++ -std=c++17 $SAN -Itest/cyd -Iinclude/cyd -Iinclude test/cyd/$t.cpp -o $O/$t && $O/$t 2>&1 | tail -1; done
echo "wszystkie testy OK"
