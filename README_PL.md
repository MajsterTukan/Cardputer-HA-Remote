# Cardputer ADV Smart Home Remote v0.2.0

[English](README.md) | **Polski**

Przenośny pilot do Home Assistant dla M5Stack Cardputer ADV. Firmware pokazuje pomieszczenia i bieżące stany urządzeń, steruje światłami, przełącznikami, roletami, scenami, skryptami i przyciskami oraz zachowuje ostatnie stany w pamięci urządzenia.

Projekt korzysta z MQTT. Cardputer nie przechowuje tokenu administratora Home Assistant i nie wywołuje jego API bezpośrednio. Automatyzacje Home Assistant publikują wybrane stany i wykonują wyłącznie polecenia dotyczące encji znajdujących się na jawnej liście.

## Najważniejsze funkcje

- pomieszczenia i urządzenia konfigurowane w jednym pliku `config/devices.json`;
- elegancki ekran startowy z numerem wersji i adresem projektu;
- stały, wyśrodkowany pasek temperatury i wilgotności dla każdego pokoju;
- płynne odświeżanie dzięki rysowaniu kompletnej klatki w buforze RAM;
- bieżące stany przesyłane z Home Assistant przez MQTT;
- światła i przełączniki: włączanie oraz wyłączanie;
- rolety: otwieranie, zatrzymanie i zamykanie;
- uruchamianie scen, skryptów i encji typu `button`;
- podgląd sensorów i binary sensorów;
- cache ostatnich danych w NVS; znak `~` oznacza stan z cache, jeszcze niepotwierdzony po połączeniu;
- automatyczne ponowne łączenie Wi‑Fi i MQTT;
- LWT MQTT: `online`/`offline`;
- wygaszanie ekranu bez rozłączania pilota;
- generator konfiguracji zapobiegający rozjechaniu się mapowania Cardputera i Home Assistant.

## Sterowanie

| Klawisz                    | Działanie                                                                 |
| -------------------------- | ------------------------------------------------------------------------- |
| `Fn` + strzałka lewo/prawo | Poprzednie/następne pomieszczenie                                         |
| `Fn` + strzałka góra/dół   | Poprzednie/następne urządzenie                                            |
| `A` / `D`                  | Alternatywnie: zmiana pomieszczenia                                       |
| `W` / `S`                  | Alternatywnie: wybór urządzenia                                           |
| `Enter`                    | Światło/przełącznik: ON/OFF; scena/skrypt/przycisk: uruchom; roleta: STOP |
| `1` / `2` / `3`            | Dla rolety: góra / stop / dół                                             |
| `R`                        | Wymuszenie ponownego pobrania wszystkich stanów                           |

Pierwszy klawisz po wygaszeniu wyłącznie budzi ekran. Nie wykonuje polecenia, dzięki czemu przypadkowe naciśnięcie w kieszeni nie włączy światła ani nie ruszy rolety.

## Wymagania

- M5Stack Cardputer ADV;
- Home Assistant z działającą integracją MQTT i brokerem Mosquitto;
- PlatformIO w Visual Studio Code albo PlatformIO Core;
- dostęp Cardputera do sieci Wi‑Fi 2,4 GHz i brokera MQTT;
- rzeczywiste identyfikatory encji Home Assistant.

## 1. Uzupełnienie urządzeń

Otwórz `config/devices.json`. Dostarczone wpisy są przykładowe i należy zastąpić je rzeczywistymi `entity_id` z Home Assistant. To jedyna część projektu, której nie da się poprawnie zgadnąć bez danych z Twojej instalacji.

Na początku pliku możesz również zmienić napis pokazywany na ekranie startowym:

```json
"app_version": "0.2.0",
"project_url": "github.com/MajsterTukan/Cardputer-HA-Remote"
```

Przykład:

```json
{
  "name": "Lazienka 1",
  "devices": [
    { "name": "Swiatlo", "entity_id": "light.lazienka_1", "type": "light" },
    { "name": "Roleta", "entity_id": "cover.lazienka_1", "type": "cover" },
    {
      "name": "Temperatura",
      "entity_id": "sensor.lazienka_1_temperature",
      "type": "sensor",
      "climate_role": "temperature"
    },
    {
      "name": "Wilgotnosc",
      "entity_id": "sensor.lazienka_1_humidity",
      "type": "sensor",
      "climate_role": "humidity"
    }
  ]
}
```

Pole `climate_role` jest opcjonalne. Wskazuje czujnik używany w pasku klimatu
danego pokoju. Jeśli czujnika nie przypisano albo nie ma aktualnego stanu,
zamiast wartości pojawi się `--`, a układ ekranu pozostanie bez zmian.

Obsługiwane typy:

| `type`          | Wymagana domena encji | Sterowanie      |
| --------------- | --------------------- | --------------- |
| `light`         | `light.*`             | ON/OFF          |
| `switch`        | `switch.*`            | ON/OFF          |
| `cover`         | `cover.*`             | OPEN/STOP/CLOSE |
| `scene`         | `scene.*`             | aktywacja       |
| `script`        | `script.*`            | aktywacja       |
| `button`        | `button.*`            | naciśnięcie     |
| `sensor`        | `sensor.*`            | tylko odczyt    |
| `binary_sensor` | `binary_sensor.*`     | tylko odczyt    |

Generator odrzuci duplikaty, błędne domeny, niedozwolone typy i przekroczenie limitu 64 urządzeń lub 24 pomieszczeń.

Nazwy ekranowe najlepiej zapisywać bez polskich znaków. Domyślna mała czcionka M5GFX nie renderuje ich poprawnie.

## 2. Dane Wi‑Fi i MQTT

Najpierw skopiuj plik wzorcowy:

```bash
cp include/secrets.example.h include/secrets.h
```

W PowerShell możesz użyć:

```powershell
Copy-Item include/secrets.example.h include/secrets.h
```

Następnie edytuj `include/secrets.h`:

```cpp
inline constexpr char WIFI_SSID[]     = "NAZWA_WIFI";
inline constexpr char WIFI_PASSWORD[] = "HASLO_WIFI";

inline constexpr char MQTT_HOST[]     = "192.168.1.50";
inline constexpr unsigned MQTT_PORT   = 1883;
inline constexpr char MQTT_USER[]     = "cardputer";
inline constexpr char MQTT_PASSWORD[] = "MOCNE_ODDZIELNE_HASLO";
```

Utwórz dla pilota osobnego użytkownika MQTT. Nie używaj konta administratora Home Assistant. Plik `secrets.h` jest ignorowany przez Git.

## 3. Wygenerowanie pakietu Home Assistant

Uruchom w katalogu projektu:

```bash
python scripts/generate_config.py
```

Powstaną lub zostaną zaktualizowane:

- `include/generated_config.h` — konfiguracja firmware;
- `home_assistant/cardputer_remote.yaml` — automatyzacje Home Assistant.

Podczas każdej kompilacji PlatformIO generator uruchamia się automatycznie.

## 4. Instalacja pakietu w Home Assistant

### Wariant A — katalog `packages` (zalecany dla wielu pakietów)

Jeżeli nie używasz jeszcze pakietów, dodaj do istniejącej sekcji `homeassistant:` w `/config/configuration.yaml`:

```yaml
homeassistant:
  packages: !include_dir_named packages
```

Nie twórz drugiej sekcji `homeassistant:` — należy dopisać `packages` do już istniejącej.

Następnie:

1. Utwórz katalog `/config/packages`, jeżeli go nie ma.
2. Skopiuj `home_assistant/cardputer_remote.yaml` jako `/config/packages/cardputer_remote.yaml`.
3. W Home Assistant uruchom sprawdzenie konfiguracji.
4. Uruchom ponownie Home Assistant.

Po każdej zmianie `config/devices.json` ponownie wygeneruj pliki i ponownie skopiuj pakiet YAML.

### Wariant B — pojedynczy plik w `/config`

Jeżeli `cardputer_remote.yaml` leży bezpośrednio w `/config`, wpis musi nadać
mu nazwę pakietu:

```yaml
homeassistant:
  packages:
    cardputer_remote: !include cardputer_remote.yaml
```

Nie używaj `packages: !include cardputer_remote.yaml`. Taki zapis wczytuje
zawartość o jeden poziom za wysoko i Home Assistant potraktuje `automation`
jako nazwę pakietu, kończąc komunikatem `expected a mapping`.

## 5. Kompilacja i wgranie

W terminalu PlatformIO:

```bash
pio run
pio run --target upload
```

Można też użyć przycisków **Build** i **Upload** rozszerzenia PlatformIO w VS Code.

Jeżeli komputer nie widzi Cardputera w trybie wgrywania:

1. Ustaw boczny przełącznik Cardputer ADV w pozycji `OFF`.
2. Przytrzymaj przycisk `G0`.
3. Podłącz zasilanie USB lub ponownie podłącz przewód.
4. Zwolnij `G0` po wykryciu urządzenia.
5. Wgraj firmware, a następnie ustaw przełącznik w pozycji `ON`.

Logi diagnostyczne:

```bash
pio device monitor
```

Gotowy plik po kompilacji znajduje się domyślnie w `.pio/build/cardputer-adv/firmware.bin`.

## Tematy MQTT

| Temat                              | Kierunek           | Znaczenie                          |
| ---------------------------------- | ------------------ | ---------------------------------- |
| `home/cardputer/command`           | Cardputer → HA     | Polecenie JSON                     |
| `home/cardputer/state/<entity_id>` | HA → Cardputer     | Stan JSON, retained                |
| `home/cardputer/request_state`     | Cardputer → HA     | Prośba o pełny snapshot            |
| `home/cardputer/status`            | Cardputer → broker | `online`/`offline`, retained + LWT |

Przykładowe polecenie:

```json
{
  "entity_id": "light.salon_glowne",
  "command": "turn_on",
  "source": "cardputer-adv-remote-A12B",
  "sequence": 7
}
```

## Bezpieczeństwo

- Nie przekierowuj portu MQTT `1883` na internet.
- Trzymaj broker w sieci lokalnej; docelowo umieść Cardputera w VLAN IoT z dostępem tylko do DNS, DHCP i brokera MQTT.
- Używaj osobnego użytkownika MQTT i ogranicz jego ACL do `home/cardputer/#`, jeżeli Twój broker obsługuje własne ACL.
- Pakiet Home Assistant zawiera allowlistę encji. Podmienienie `entity_id` w wiadomości MQTT nie pozwala sterować encją spoza listy.
- Polecenia ON/OFF są idempotentne zamiast `toggle`, więc ponowiona wiadomość nie odwróci stanu drugi raz.
- Wersja startowa używa zwykłego MQTT w LAN. Nie jest przeznaczona do działania przez publiczny internet.

## Rozwiązywanie problemów

| Objaw                                           | Najczęstsza przyczyna                                                                        |
| ----------------------------------------------- | -------------------------------------------------------------------------------------------- |
| `BRAK KONFIGURACJI`                             | W `secrets.h` pozostało `CHANGE_ME`                                                          |
| `WF`, ale brak `MQ`                             | Błędny adres, port, login lub hasło brokera                                                  |
| Stan ma prefiks `~`                             | Wyświetlany jest cache; nie dotarł aktualny stan MQTT                                        |
| Encje są `UNKNOWN`                              | Błędne `entity_id`, brak pakietu HA albo brak retained snapshotu                             |
| Polecenie jest wysyłane, ale nic się nie dzieje | Encja nie istnieje lub nie pasuje do typu w `devices.json`                                   |
| Klawisze nie reagują                            | Sprawdź, czy używana jest biblioteka M5Cardputer 1.2.0 i czy firmware jest dla Cardputer ADV |
| Home Assistant odrzuca YAML                     | Uruchom generator ponownie i sprawdź wcięcia w `configuration.yaml`                          |

## Struktura projektu

```text
config/devices.json                    jedna lista pomieszczeń i encji
home_assistant/cardputer_remote.yaml   wygenerowany pakiet HA
include/generated_config.h             wygenerowana konfiguracja C++
include/secrets.example.h               wzór prywatnej konfiguracji sieciowej
scripts/generate_config.py              walidator i generator
src/main.cpp                            firmware pilota
platformio.ini                          środowisko kompilacji
README.md                               dokumentacja angielska
README_PL.md                            dokumentacja polska
```
