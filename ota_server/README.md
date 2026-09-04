# ReflowCtrl OTA-Update

Mit dem OTA-Update (Over the Air) kann eine neue Firmware über das lokale WLAN auf den ESP32
übertragen werden. Der Python-Server läuft auf dem Entwicklungsrechner und teilt dem ESP seine
erreichbare LAN-Adresse beim Auslösen des Updates direkt mit.

Der ESP32 ist im lokalen Netz als `reflow-ctrl.local` erreichbar und stellt einen minimalistischen
HTTP-Server bereit. Beim Start sendet der OTA-Server einen Webhook an `POST
http://reflow-ctrl.local/ota`. Daraufhin lädt der ESP die angebotene Firmware sofort herunter,
prüft und installiert sie. Danach bestätigt er dem Server das erfolgreiche Update und startet neu.
Der Server beendet sich nach dieser Bestätigung automatisch. Ohne Webhook führt der ESP keine
regelmäßigen OTA-Anfragen aus.

## Voraussetzungen

- Entwicklungsrechner und ESP32 befinden sich im selben IPv4-Netzwerk.
- mDNS über UDP-Port 5353 ist im Netzwerk erlaubt.
- TCP-Port 8070 ist in der Firewall des Entwicklungsrechners erlaubt.
- `uv`, Python und ESP-IDF sind in der Entwicklungsumgebung verfügbar.
- Die lokale Datei `main/Credentials.hpp` enthält die WLAN-Zugangsdaten.

## 1. WLAN-Zugangsdaten einrichten

Beim ersten Checkout die Beispielkonfiguration kopieren:

```sh
cp main/Credentials.example.hpp main/Credentials.hpp
```

Danach `main/Credentials.hpp` bearbeiten:

```cpp
#pragma once

namespace reflowCtrl::credentials {

inline constexpr char WIFI_SSID[] = "Mein WLAN";
inline constexpr char WIFI_PASSWORD[] = "Mein Passwort";

}  // namespace reflowCtrl::credentials
```

`main/Credentials.hpp` wird durch `.gitignore` ausgeschlossen. Die echten Zugangsdaten gelangen
dadurch nicht versehentlich in Git.

## 2. Erstinstallation über die Programmierschnittstelle

Vor dem ersten OTA-Update müssen Bootloader, Partitionstabelle, OTA-Metadaten und Anwendung einmal
vollständig seriell installiert werden:

```sh
tools/dev build
idf.py flash
```

Falls der serielle Port explizit angegeben werden muss:

```sh
idf.py -p /dev/ttyUSB0 flash
```

> **Lebensgefahr:** Die Programmierschnittstelle niemals verwenden, während die Relaisplatine mit
> Netzspannung versorgt wird. Die Platine muss vollständig vom Stromnetz getrennt und ausschließlich
> über eine geeignete Niederspannungs-Programmierumgebung versorgt werden.

Nach dieser Erstinstallation können zukünftige Anwendungsupdates über WLAN erfolgen.

## 3. Neue Firmware bauen

Im Projektverzeichnis ausführen:

```sh
tools/dev build
```

Das zu übertragende Image liegt danach unter:

```text
build/reflowCtrl.bin
```

## 4. OTA-Server starten

Der Server wird im Devcontainer ausgeführt. Docker Engine läuft dabei innerhalb von WSL. Damit
WSL die Windows-LAN-Schnittstelle einschließlich Multicast verwendet, muss in
`%UserProfile%\.wslconfig` der gespiegelte Netzwerkmodus aktiviert sein:

```ini
[wsl2]
networkingMode=mirrored
```

Danach in PowerShell `wsl --shutdown` ausführen und WSL sowie den Devcontainer neu starten. Die
Datei `.devcontainer/devcontainer.json` startet den Container bereits mit `--network=host`, sodass
er anschließend das gespiegelte WSL-Netzwerk verwendet.

Einmalig müssen in einer als Administrator gestarteten Windows-PowerShell passende Regeln in der
Hyper-V-Firewall von WSL angelegt werden:

```powershell
$wslId = "{40E0AC32-46A5-438A-A0B2-2B479E8F2E90}"
New-NetFirewallHyperVRule -Name "ReflowCtrlOtaHttp" -DisplayName "ReflowCtrl OTA HTTP (WSL)" -Direction Inbound -VMCreatorId $wslId -Protocol TCP -LocalPorts 8070 -Action Allow
New-NetFirewallHyperVRule -Name "ReflowCtrlOtaMdns" -DisplayName "ReflowCtrl OTA mDNS (WSL)" -Direction Inbound -VMCreatorId $wslId -Protocol UDP -LocalPorts 5353 -Action Allow
```

Nach einer Änderung an der Devcontainer-Netzwerkkonfiguration muss der Container neu gebaut oder
neu geöffnet werden.

Die Python-Abhängigkeiten müssen beim ersten Mal beziehungsweise nach Änderungen an `uv.lock`
installiert werden:

```sh
uv sync
```

Danach den Server im Devcontainer aus dem Projektverzeichnis starten:

```sh
uv run python ota_server/server.py
```

Der Server erkennt die aktiven IPv4-Netzwerkschnittstellen automatisch. Er entdeckt den HTTP-Dienst
des ESP direkt per mDNS und ist deshalb nicht
darauf angewiesen, dass der Devcontainer `.local`-Namen über den System-DNS-Resolver auflösen kann.
Über die Netzwerkroute zum ESP bestimmt er seine erreichbare LAN-Adresse und sendet sie im
Webhook. Der OTA-Server selbst benötigt daher keinen mDNS-Namen. Docker-Bridge- und virtuelle
Container-Schnittstellen werden ignoriert. Eine feste oder beim Start angegebene IP-Adresse ist
nicht erforderlich.

Der Server verwendet standardmäßig `build/reflowCtrl.bin` und TCP-Port 8070. Eine typische
Ausgabe sieht so aus:

```text
Serving .../build/reflowCtrl.bin at http://192.168.1.20:8070/firmware.bin
Notifying reflow-ctrl.local; server exits after ESP confirmation
Waiting to discover reflow-ctrl.local via mDNS
Found reflow-ctrl.local at 192.168.1.57
ESP accepted OTA trigger at http://192.168.1.57/ota; firmware URL is http://192.168.1.20:8070/firmware.bin
```

Nun sind keine weiteren Eingaben erforderlich. Spätestens beim nächsten Ein-Sekunden-Intervall
erreicht der Webhook den ESP und startet das Update. Während der Übertragung zeigt der Server den
Fortschritt als Prozentwert, übertragene Datenmenge, exakte Byte-Anzahl,
Durchschnittsgeschwindigkeit, Laufzeit und geschätzte Restzeit an. Nach erfolgreicher Übertragung
erscheint beispielsweise:

```text
Downloading firmware: 100.00% | 1.18 MiB / 1.18 MiB (1,234,567 / 1,234,567 bytes) | 602.82 KiB/s | elapsed 00:02 | ETA 00:00
Firmware transferred: 1.18 MiB (1,234,567 bytes) in 00:02 at an average of 602.77 KiB/s; waiting for ESP confirmation
ESP confirmed the update; server stopped
```

Der Serverprozess endet anschließend selbstständig.

Der Status-Endpunkt des ESP ist unter `GET http://reflow-ctrl.local/` verfügbar. Ein Update kann
bei laufendem OTA-Server außerdem manuell ausgelöst werden:

```sh
curl -X POST --data '192.168.1.20' http://reflow-ctrl.local/ota
```

Jeder angenommene Webhook installiert das vom Server gelieferte Image. Die App-Version wird dabei
nicht mit der laufenden Version verglichen; dasselbe Image kann daher erneut installiert werden.

## Eigenes Firmware-Image verwenden

Ein anderer Pfad kann als erstes Argument übergeben werden:

```sh
uv run python ota_server/server.py pfad/zur/firmware.bin
```

Einen abweichenden Port auswählen:

```sh
uv run python ota_server/server.py --port 8080
```

Der Port muss dann auch unter `ReflowCtrl network configuration` in `idf.py menuconfig` auf
denselben Wert gesetzt und diese Änderung zunächst auf dem ESP installiert werden.

## Update abbrechen

Solange noch kein Update übertragen wird, kann der Server mit `Ctrl+C` beendet werden. Während der
ESP die Firmware schreibt, sollten weder Server noch ESP ausgeschaltet werden.

## Fehlersuche

### Der ESP findet den Server nicht

- Prüfen, ob Rechner und ESP im selben WLAN beziehungsweise lokalen Netz sind.
- UDP-Port 5353 und TCP-Port 8070 in der Firewall freigeben.
- In der Serverausgabe prüfen, ob die korrekte LAN-Adresse in der Firmware-URL aufgeführt wird.
- Prüfen, ob der Server `reflow-ctrl.local` per mDNS findet.
- Die serielle ESP-Ausgabe auf WLAN- oder OTA-Fehler kontrollieren.

### `Firmware not found`

Zuerst `tools/dev build` ausführen oder den korrekten Pfad zur Binärdatei als Argument übergeben.

### Der Server wartet auf die ESP-Bestätigung

Die Datei wurde über HTTP übertragen, aber der ESP konnte sie möglicherweise nicht vollständig
prüfen oder in die OTA-Partition installieren. Die serielle Ausgabe des ESP kontrollieren. Der
Server beendet sich absichtlich erst nach der erfolgreichen Bestätigung durch den ESP.

## Sicherheit

Der aktuelle Entwicklungsablauf verwendet unverschlüsseltes HTTP und authentifiziert den
Update-Server nicht. Er darf deshalb nur in einem vertrauenswürdigen lokalen Entwicklungsnetz
verwendet werden. Für einen produktiven Einsatz sollten HTTPS, signierte Firmware und ein
geeignetes Rollback-Verfahren ergänzt werden.
