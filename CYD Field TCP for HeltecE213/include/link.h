#pragma once
// Wspolne ustawienia lacza WiFi / TCP miedzy E213 (punkt dostepowy + serwer API) a CYD 2,8" (klient).
// Zmieniasz je tylko tutaj: obie plytki buduja sie z tym samym plikiem, wiec zawsze do siebie pasuja.
// Haslo WPA2 musi miec co najmniej 8 znakow.
#define LINK_SSID "MeshField-E213"
#define LINK_PASS "mazowsze213"
constexpr unsigned LINK_PORT=4403;          // ten sam port co API TCP w firmware Meshtastic
constexpr unsigned LINK_CHANNEL=6;          // kanal WiFi punktu dostepowego E213 (1-13)
// Adres E213 w jego wlasnej sieci (domyslny adres punktu dostepowego ESP32)
constexpr uint8_t LINK_IP[4]={192,168,4,1};
