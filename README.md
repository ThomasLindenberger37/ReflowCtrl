# ReflowCtrl

ReflowCtrl ist eine ESP32-Firmware für einen Reflow-Ofen mit eingebetteter
Weboberfläche. Das Projekt verwendet C++ und ESP-IDF und befindet sich in Entwicklung.

Aktuell unterstützt die Firmware Temperaturmessung mit einem MAX6675,
Ofencharakterisierung, einen browserbasierten Debug-Terminal und Firmware-Updates
über WLAN. Die automatische Regelung anhand von Reflow-Profilen ist noch nicht
implementiert; die entsprechenden Bedienelemente sind deaktiviert.

## Hardware

- Platine: LC Technology ESP32 Relay AC X1
- Modul: ESP32-WROOM-32E
- ESP-IDF-Target: `esp32`
- Versorgungseingang der Platine: 90–250 V AC

| Funktion | GPIO | Kabelfarbe |
| --- | --- | --- |
| Onboard-Relais | 16 | — |
| Onboard-LED (`LedLink`) | 23 | — |
| MAX6675 SO / MISO | 25 | Gelb |
| MAX6675 SCK | 32 | Orange |
| MAX6675 CS | 27 | Braun |

Die Firmware konfiguriert zusätzlich GPIO0 als Tastereingang. Die Pinbelegung
steht in [hardware_configuration.cpp](main/hardware/hardware_configuration.cpp).

> **Lebensgefahr durch Netzspannung:** Die Programmierschnittstelle niemals
> anschließen oder verwenden, solange die Platine mit Netzspannung versorgt wird.
> Zum Programmieren und Debuggen muss sie vollständig vom Stromnetz getrennt sein
> und ausschließlich über eine geeignete Niederspannungs-Programmierumgebung
> versorgt werden.

## Entwicklungsumgebung

Die Konfiguration unter [.devcontainer](.devcontainer/devcontainer.json) stellt
eine Entwicklungsumgebung für VS Code bereit. Sie enthält ESP-IDF v6.1, die
ESP32-Toolchain, CMake, Ninja, Python, `uv` sowie Formatierungs- und Analysewerkzeuge.
Das Repository in VS Code öffnen und **Dev Containers: Reopen in Container** ausführen.

Alternativ kann eine lokale ESP-IDF-Installation verwendet werden.
`bash tools/idf` verwendet ein verfügbares `idf.py` oder lädt die Umgebung aus
`${IDF_PATH:-/opt/esp-idf}/export.sh`.

Alle folgenden Befehle werden im Stammverzeichnis des Repositorys ausgeführt.

## WLAN und Build

Beim ersten Einrichten die Beispielzugangsdaten kopieren:

```sh
cp main/Credentials.example.hpp main/Credentials.hpp
```

In `main/Credentials.hpp` die Werte für `WIFI_SSID` und `WIFI_PASSWORD` eintragen.
Diese Datei ist von Git ausgeschlossen.

```sh
bash tools/idf build
```

Das Firmware-Image wird unter `build/reflowCtrl.bin` erzeugt. Weitere Einstellungen
sind über `bash tools/idf menuconfig` verfügbar.

## Erstinstallation und Weboberfläche

Nur in der oben beschriebenen, vom Netz getrennten Programmierumgebung flashen.
Den seriellen Port an die eigene Umgebung anpassen:

```sh
bash tools/idf -p /dev/ttyUSB0 flash monitor
```

Nach dem Start und der WLAN-Verbindung ist die Oberfläche unter
<http://reflow-ctrl.local> erreichbar. Sie zeigt die gemessene Temperatur und
Firmware-Logs. Die Texte der eingebetteten Weboberfläche sind auf Englisch.

Die Ofencharakterisierung schaltet das Heizrelais während einer Messsequenz und
zeichnet Temperaturdaten auf. Im Charakterisierungsdialog lassen sich CSV-Daten
exportieren und CSV- oder Konfigurations-JSON-Dateien laden. **Save to controller**
speichert die analysierte Konfiguration dauerhaft im Flash; sie wird beim Öffnen
des Dialogs auch nach einem Neustart wieder geladen. Rohdaten als CSV bleiben im
Browser und müssen bei Bedarf heruntergeladen werden.

Profilsteuerung, Profilverwaltung und Einstellungen sind in der Firmware noch
nicht verfügbar. Der separate FastAPI-Mock bildet zusätzliche Entwicklungsfunktionen
ab und entspricht nicht vollständig dem Funktionsumfang der Firmware.

## Updates über WLAN

Nach der ersten seriellen Installation können weitere Firmware-Updates über das
lokale Netzwerk erfolgen:

```sh
bash tools/idf build
uv sync --locked
uv run python ota_server/server.py
```

Der OTA-Server entdeckt den Controller per mDNS und stößt das Update an.
Rechner und Controller müssen im selben IPv4-Netz erreichbar sein. Der aktuelle
OTA-Ablauf nutzt unverschlüsseltes HTTP ohne Authentifizierung und ist für ein
vertrauenswürdiges lokales Entwicklungsnetz vorgesehen.

Details zu Netzwerk, WSL, Firewall und Fehlersuche stehen in der
[OTA-Anleitung](ota_server/README.md).

## Tests und Codequalität

Die portablen C++-Tests laufen ohne angeschlossenen ESP32. Beim ersten Konfigurieren
lädt CMake GoogleTest und gegebenenfalls cJSON herunter.

```sh
cmake -S tests -B build/unit-tests -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build/unit-tests
ctest --test-dir build/unit-tests --output-on-failure
```

Weitere Prüfungen:

```sh
python3 tools/analyze_message_bus_subscriptions.py
uv run python -m unittest discover -s tests -p '*_test.py'
node --test tests/characterization_ui_test.js
```

Für die UI-Tests wird Node.js mit Unterstützung für `node --test` benötigt;
Node.js ist derzeit nicht im Devcontainer enthalten.

`bash tools/dev format-check` prüft die Formatierung,
`bash tools/dev lint` führt die konfigurierten Qualitätsprüfungen aus.
Die ESP32-Analyse mit clang-tidy benötigt eine zur Cross-Toolchain passende
Konfiguration; die Systemversion kann an ESP32-spezifischen Compileroptionen scheitern.
Projektregeln sind in [AGENTS.md](AGENTS.md) dokumentiert.

## Projektstruktur

| Verzeichnis | Inhalt |
| --- | --- |
| `main/` | Firmware, Netzwerk, Webserver und persistente Konfiguration |
| `main/components/` | Temperaturerfassung, Relais, LED und Charakterisierungsablauf |
| `main/hardware/` | GPIO-Anbindung und Hardwarekonfiguration |
| `webserver/` | Eingebettetes Frontend, API-Dokumentation und lokaler Backend-Mock |
| `ota_server/` | Python-Server für Firmware-Updates |
| `tests/` | C++-, Python- und JavaScript-Tests |
| `tools/` | Build- und Entwicklungswerkzeuge |

Weitere Informationen: [Weboberfläche und Mock](webserver/README.md),
[API-Dokumentation](webserver/API.md).

## Lizenz

Dieses Projekt steht unter der [MIT-Lizenz](LICENSE).
Copyright © 2026 Thomas Lindenberger.
