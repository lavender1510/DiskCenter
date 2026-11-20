# Implementierungsstand 0.1.0-alpha

| Modul | Status | Umfang und verbleibende Arbeit |
| --- | --- | --- |
| Core, Einstellungen und Protokoll | Implementiert und automatisiert getestet | Profile und Speichergrenzen, atomare Speicherung, lokales Protokoll, Deutsch/Englisch |
| Oberfläche und Darstellung | Implementiert, Windows-Matrix offen | Dashboard, native Steuerelemente, System/Hell/Dunkel/Classic/Aero/Low-Memory, Akzente, Schriftgröße, Tastaturbedienung |
| Laufwerke und Disc-Zustand | Implementiert, Hardwaretest offen | Windows-Laufwerksbuchstaben, Medienwechsel, Label und Dateisystem, Auswerfen |
| Disc-Explorer | Teilweise implementiert | Virtuelle Liste, Namensfilter, Dateikopie, CSV pro Verzeichnisebene, ISO 9660/Joliet durchsuchen und extrahieren. Mediapreviews, Stapelkopie und Disc Compare fehlen |
| Image-Manager | Teilweise implementiert | ISO 9660/Joliet aus Ordnern, SHA-256, Erkennung von ISO und UDF, ISO-Sektorabbild einzelner Datenspuren. UDF/Hybriderstellung via IMAPI2 und physisches Auslesen benötigen native Tests. BIN/CUE und bootfähige Images fehlen |
| Brennengine | Implementiert, Hardwaretest offen | Ordner und ISO auf leere Rohlinge, Laufwerks- und Kapazitätsprüfung, manuelle Geschwindigkeit, Finalisierung, IMAPI2-Verifikation. Multisession, Löschen, Mehrfachkopien und Projekte fehlen |
| Medienplayer | Nicht implementiert | Eigenständige Audio-/Videowiedergabe und DVD-Navigation |
| Audio- und Video-Studio | Nicht implementiert | Audio-CD-Erstellung, Ripping, Konvertierung, DVD-Authoring und Menüs |
| Backup und Archivierung | Nicht implementiert | Planung, Komprimierung, Verschlüsselung, Disc-Spanning und Restore |
| Recovery und Disc Diagnostics | Nicht implementiert | Fehlertolerante und fortsetzbare Sektorlesevorgänge |
| Disc-Bibliothek und Cover Designer | Nicht implementiert | SQLite-Katalog, Layout, Druck und Export |
| Update-Manager | Nicht implementiert | Optionale GitHub-Abfrage und Integritätsprüfung von Downloads |
| Installer, Portable, Release | Paketierung und Workflow implementiert | Zwei native Nutzlasten, vier Pakete, SHA-256, automatisierte Tests vor Veröffentlichung. Siehe [RELEASING.md](RELEASING.md) |

Der lokale ISO-9660-/Joliet-Writer setzt Dateien bis 4 GiB minus ein Byte, acht Verzeichnisebenen und Joliet-Namen bis 64 UCS-2-Codeeinheiten einschließlich Versionssuffix voraus. Der Writer übernimmt die Originalnamen im Joliet-Baum und erzeugt im ISO-9660-Baum eindeutige ASCII-Namen. Einzelne Einträge mit Mehrfachextent oder Interleaving können in der Image-Ansicht angezeigt werden. Die Extraktion benötigt einen zusammenhängenden Extent. Schutzmaßnahmen von kommerziellen Medien werden nicht umgangen.

Zu RAM-Messungen und Testermittlung siehe [PERFORMANCE.md](PERFORMANCE.md) und [TESTING.md](TESTING.md).
