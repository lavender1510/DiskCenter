# DiscCenter

DiscCenter 0.1.0-alpha ist eine native Windows-Anwendung für optische Medien. Diese experimentelle Version bietet eine deutsche und englische Oberfläche, ein Laufwerksfenster und Werkzeuge für Ordner, Discs und ISO-Images. Der Quellcode steht unter der MIT-Lizenz. Die Anwendung arbeitet offline.

## Erste Schritte

- **Portable**. Die ZIP-Datei vollständig entpacken und `DiscCenter.exe` öffnen. Mit der mitgelieferten Datei `portable.flag` liegen Einstellungen und Protokoll neben dem Programm.
- **Setup**. Das Paket für die eigene Prozessorarchitektur starten. Das Setup installiert für das aktuelle Benutzerkonto. Startmenü, Desktop und das Angebot unter Windows „Öffnen mit“ sind wählbar.
- **Leistungsprofil**. Unter **Einstellungen** lassen sich die automatische Auswahl, Classic und Modern festlegen. Working Set, Private Bytes und Transferpuffer werden dort angezeigt.
- **Disc-Explorer**. Ein optisches Laufwerk, einen Ordner oder ein ISO-9660-/Joliet-Image öffnen. Ordner und ISO-Dateien lassen sich ins Hauptfenster ziehen. Nach Namen filtern, eine Datei speichern oder die aktuelle Verzeichnisebene als CSV exportieren.
- **Image-Manager**. Eine ISO aus einem Ordner erzeugen, eine unterstützte Dateisystemstruktur erkennen und SHA-256 berechnen. Optional die Prüfsumme mit einem Referenzwert abgleichen. Die Disc-Sicherung als ISO setzt eine einzelne Daten-Session mit einer Datenspur voraus.
- **Brennstudio**. Ein leeres Medium einlegen und das Laufwerk auswählen. Disc-Label, Dateisystem und gegebenenfalls die ermittelte Schreibgeschwindigkeit einstellen. Aus einem Ordner oder ISO-Image schreiben. Die vollständige Prüfung wird über Windows IMAPI2 angefordert. **Physische Lese- und Brennvorgänge sind in dieser Alpha experimentell.**

`Strg+1` bis `Strg+5` wechseln zwischen den Bereichen. `F5` aktualisiert die Laufwerksliste. Mit Tab, Pfeiltasten und Eingabetaste sind die normalen Bedienelemente erreichbar.

Windows 7 SP1 bis Windows 11 sind das Ziel. Die geprüften Umgebungen stehen in [COMPATIBILITY.md](COMPATIBILITY.md). Den Stand von Audio, Video, Archivierung und Datenrettung dokumentiert [IMPLEMENTATION_STATUS.md](IMPLEMENTATION_STATUS.md).

## Entwicklung

- [Builds](BUILDING.md)
- [Architektur](ARCHITECTURE.md)
- [Tests](TESTING.md)
- [RAM und Leistung](PERFORMANCE.md)
- [Änderungen](CHANGELOG.md)
- [Paketierung und GitHub-Releases](RELEASING.md)
- [Lizenz](LICENSE) und [Drittanbieterhinweise](THIRD_PARTY_NOTICES.md)
