# Veröffentlichungen

Die Versionsnummer in `VERSION` bestimmt die nummerierte CMake-Version, die Produktanzeige, die Setup- und ZIP-Dateinamen sowie den erwarteten Tag. Alpha, Beta und RC verwenden einen SemVer-Zusatz und werden als GitHub-Pre-release markiert.

## Paketierung

`packaging/package.py build` erstellt und prüft Setup und Portable für genau eine Architektur. Nach beiden Builds erzeugt `python packaging/package.py checksums --out dist` die SHA-256-Datei. Die ZIP-Prüfung kontrolliert Inhalt und Integrität. Setup und enthaltenes Programm haben dieselbe Architektur. Für den x64-Build benötigt NSIS die amd64-Unicode-Stubs und -Plugins.

Das Setup erlaubt ein benutzerdefiniertes Verzeichnis und optionale Verknüpfungen. Die ISO-Integration bietet DiscCenter unter „Öffnen mit“ an. Beim Entfernen werden Setup-Dateien und die zugehörigen Registrierungseinträge entfernt. Persönliche Einstellungen im Benutzerprofil bleiben erhalten.

## GitHub

`.github/workflows/windows.yml` baut und testet x86 und x64 auf Windows-Runnern, prüft die portable und die installierte Anwendung, sammelt die vier Dateien und erzeugt `SHA256SUMS.txt`. Ein Versions-Tag passend zu `VERSION` löst nach beiden erfolgreichen Jobs einen GitHub-Release aus. Ein fehlgeschlagener Upload bleibt im Entwurfsstatus. Alle früheren Veröffentlichungen bleiben unter ihren Tags bestehen.

Für einen stabilen Release wird zusätzlich ein Testnachweis unter `test-evidence/vX.Y.Z.md` erwartet. Er dokumentiert die Windows-Matrix, physische Laufwerke und Medien, Verifikation, Abbruch, Lastmessungen und bekannte Einschränkungen. Der Ablauf in [TESTING.md](TESTING.md) ergänzt die CI.

Eine tatsächliche Veröffentlichung erfordert ein GitHub-Zielrepository mit Schreibzugriff und laufenden Actions. Der lokale Stand ist ein experimenteller Alpha-Build.
