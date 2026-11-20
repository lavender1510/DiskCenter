# Testen

## Automatisierte Tests

`ctest --test-dir build --output-on-failure` führt Kern- und bei Windows-Builds Integrationstests aus. Der Kern deckt Profilwahl, Einstellungen, Pfadprüfung, Sektorkapazität und beschädigte ISO-Datensätze ab. Die Windows-Tests vergleichen einen bekannten SHA-256, kopieren Daten blockweise, prüfen Abbruch und Schutz vorhandener Dateien, prüfen den Schreibschutz bei einem erst während des Transfers entstandenen Ziel und lesen bei Unterstützung für Sparse-Dateien oberhalb von 4 GiB. Sie exportieren Verzeichnisdaten und erzeugen ein ISO9660/Joliet-Image mit einem Unicode-Namen und einer leeren Datei.

Die Paketprüfung kontrolliert Dateinamen, PE-Maschine, ZIP-Inhalt und SHA-256. `packaging/smoke.ps1` startet die portable und die installierte Version, prüft ihre gemeldete Architektur und führt die Deinstallation aus. In einer Linux-Umgebung kann die Ausführung von Windows-Programmen unter Wine geprüft werden.

## Ergebnisse für 0.1.0-alpha

| Prüfung | Ergebnis |
| --- | --- |
| Kern auf Debian 13 | Bestanden |
| Windows-Integrationstests x64 unter Wine 11.22 | Bestanden, einschließlich des Dateioffsets oberhalb von 4 GiB |
| Unabhängige Prüfung des Test-Images mit PyCdlib 1.22 | Primärer ISO- und Joliet-Baum lesbar, Dateiinhalte stimmen überein |
| Setup und Portable für x86 und x64 | Alle vier gebaut. PE-Architektur, SHA-256 und ZIP-Integrität stimmen. Die mit 7-Zip extrahierte Setup-Nutzlast ist bytegleich zum jeweiligen portablen Programm |
| Portable Start, Installation, installierter Start und Deinstallation x64 unter Wine 11.22 | Bestanden |
| Laufzeittests x86 | Offen. Die Cloud-Umgebung unterstützt keine 32-Bit-Prozesse. Zwei Wine-Konfigurationen konnten die x86-Anwendung dort nicht starten |
| GitHub-Actions-Run | Offen. Der Workflow besteht die statische Prüfung mit actionlint 1.7.9 und benötigt ein Zielrepository |
| Native Windows-Matrix, IMAPI2 und optische Laufwerke | Offen |

## Native Windows- und Hardwaretests

1. Auf Windows 7 SP1 x86 und x64 sowie Windows 8/8.1, 10 und 11 die passende Version als Standardbenutzer installieren. App, Portable, Sprache, Themes, Navigation und Deinstallation prüfen.
2. Interne SATA- und externe USB-Laufwerke mit leerer Lade, CD-R/RW, DVD±R/RW, DL, DVD-RAM und BD-R/RE prüfen, soweit das Laufwerk sie unterstützt. Firmware, Anschluss, Medien-ID und Treiber protokollieren.
3. Ein bekanntes ISO und einen Ordner schreiben. Kapazitätsgrenze, nicht leeres Medium, Schreibgeschwindigkeiten, Medienwechsel, Finalisierung und vollständige IMAPI2-Prüfung nachstellen. Quell- und Zielinhalte unabhängig per Hash vergleichen.
4. Auf Testrohlingen Abbruch, getrenntes USB-Laufwerk und Medienfehler erproben. Eine Daten-Disc als ISO sichern und Prüfsumme, Dateisystem und Inhalte mit der Quelle vergleichen. Audio- und Multisession-Medien müssen mit der passenden Einschränkung gemeldet werden.
5. Auf einem älteren Testrechner mehrere Stunden laufen lassen, Profile wechseln und Bilder und Ordner verschiedener Größen öffnen. RAM-Spitzen und Freigabe protokollieren. [PERFORMANCE.md](PERFORMANCE.md)
