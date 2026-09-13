# Gehäuse für ReflowCtrl

`ReflowOvenHousung.py` erzeugt mit Build123D das Gehäuse und einen separaten
Schnappdeckel als STL-Dateien. Alle Maße im Skript sind in Millimetern angegeben.

## Im Devcontainer ausführen

Nach Änderungen am Dockerfile den Container in VS Code mit
**Dev Containers: Rebuild Container** neu erstellen. Das Image installiert
Build123D 0.11.1 mit Python 3.12 in einer eigenen Umgebung unter `/opt/mechanics`.

Im Projektverzeichnis ausführen:

```sh
/opt/mechanics/bin/python mechanics/ReflowOvenHousung.py
```

Der Export kann einige Minuten dauern. Das Skript erstellt automatisch den
Ordner `build/mechanics` im Projekt und schreibt diese Dateien:

- `reflow_oven_controller_housing.stl`
- `reflow_oven_controller_snap_fit_lid.stl`

Der Ausgabeort ist unabhängig vom aktuellen Arbeitsverzeichnis. Vorhandene
STL-Dateien mit diesen Namen werden überschrieben. Der Ordner `build` wird von
Git ignoriert; beim Löschen des Build-Ordners werden auch die STL-Dateien entfernt.
Der Mechanikexport wird separat gestartet und ist kein Teil des Firmware-Builds.

## Geometrie anpassen

Die Werte im Abschnitt `Configuration` steuern unter anderem Platinenmaße,
Befestigungsbohrungen, Wandstärken, Lüftungsöffnungen, Kabelhalter, Schnappdeckel,
Taster und Beschriftung. Dateinamen und Ausgabeordner stehen in `EXPORT_CONFIG`;
der Ausgabeordner wird relativ zum Projektverzeichnis interpretiert.

Der Deckel wird bereits mit der Außenseite nach unten und den Schnapphaken nach
oben exportiert. Die STL-Dateien können anschließend im Slicer geöffnet werden.

Die Schrift für die Deckelbeschriftung liegt unter
`assets/fonts/StardosStencil-Bold.ttf` und stammt aus
[Google Fonts](https://github.com/google/fonts/tree/main/ofl/stardosstencil).
Die zugehörige SIL Open Font License liegt in `assets/fonts/OFL.txt`.
