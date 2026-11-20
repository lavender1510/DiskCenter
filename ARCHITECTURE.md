# Architektur

DiscCenter verwendet C++17, CMake und eine native Unicode-Win32-Oberfläche. Der Anzeigename ist die CMake-Einstellung `DISCCENTER_NAME`. Produktversion, PE-Ressourcen, Setup-Dateinamen und Release-Tag beziehen ihre Version aus `VERSION`. Dateinamen, Installationsregistrierung und Win32-Identitäten dienen als stabile Paketkennungen.

| Modul | Aufgabe |
| --- | --- |
| `core` | Fehlercodes und Übersetzungen, Performance-Richtlinien, Einstellungen, Pfad- und Kapazitätsprüfungen |
| `iso9660` | Blockweises Parsen von ISO 9660 und Joliet, Eintragsgrenzen, Erkennung einer UDF-Struktur |
| `iso_writer` | Planung der Verzeichnis- und Pfadtabellen, blockweises Erzeugen einer ISO 9660 mit Joliet |
| `platform` | Win32-Dateien, atomare Ausgaben, SHA-256 via CNG, Verzeichnisse, Protokoll, Speicherstatistik |
| `optical` | Laufwerksgeräte, TOC und Sektorlesen, COM-Aktivierung von IMAPI2, Medienprüfung und Schreibfortschritt |
| `ui` und `ui_actions` | GDI-Layout, native Steuerelemente, Assistenten für Dateiauswahl und Arbeitsthread |

## Verarbeitung und Fehler

Ein Hintergrundvorgang pro Prozess begrenzt die Geräte- und Plattenlast. Die UI liest einen kleinen Fortschrittssnapshot, Ergebnisse werden erst nach Abschluss des Threads übernommen. Optionen für laufende Vorgänge werden beim Start kopiert. Für das Schreiben reserviert IMAPI2 das Laufwerk exklusiv und prüft das Medium erneut direkt vor der Schreiboperation. Abbruchsignale werden zwischen Blöcken geprüft. Der Brennsink reicht den Abbruch bei geeigneten IMAPI2-Ereignissen an die Engine weiter.

Die dateibasierte Verarbeitung schreibt in eine temporäre Datei neben dem Ziel. Ein abgeschlossenes und auf den Datenträger geleertes Ergebnis wird per Umbenennen übernommen. Existierende Nutzdateien werden dabei geschützt. Nur Einstellungen dürfen ihren eigenen bisherigen Zustand atomar ersetzen. Ein Prozessabsturz kann eine klar erkennbare `.partial`-Datei hinterlassen.

Der Image-Reader validiert Endianness, Längen, Extents und Namen vor der Nutzung. ISO-Verzeichnisse werden seitenweise gelesen, der ListView enthält nur Indizes. Bei einer Extraktion wird die Dateiidentität des erneut geöffneten Images geprüft. Das Blocklesen verwendet 64-Bit-Offsets. Die standardmäßige ISO-Erstellung braucht kein IMAPI2 und speichert nur Metadaten sowie einen Transferpuffer im Prozess. Für UDF und die direkte Verarbeitung eines Ordners beim Brennen wird die Windows-Engine erst während der Aufgabe aktiviert.

Einstellungen sind kleine, versionsverträgliche Schlüssel-Wert-Dateien im INI-Format. Damit bleibt der Startpfad frei von einer Datenbank- oder JSON-Laufzeitabhängigkeit. Für die geplante Bibliothek sieht die Roadmap SQLite vor. Vorgangsprotokolle liegen lokal und rotieren ab 256 KiB.
