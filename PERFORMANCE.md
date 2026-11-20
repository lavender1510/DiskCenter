# Arbeitsspeicher und Leistung

| Szenario | RAM-Ziel |
| --- | --- |
| Leerlauf | Unter 80 MB |
| Disc-Explorer | Unter 120 MB |
| Brennen einer Daten-CD/DVD | Unter 150 MB |
| Audio-CD-Wiedergabe | Unter 120 MB |
| Bibliothek | Unter 150 MB |
| Datenrettung | Möglichst unter 256 MB |

Die aktuelle Programmoberfläche zeigt das Working Set, die Private Bytes und den Transferpuffer. Der Paket-Smoketest meldet zusätzlich belegten oder reservierten virtuellen Adressraum per `VirtualQuery`. Für native Referenzmessungen nach dem Start warten, anschließend Working Set und Private Bytes als Verlauf messen. Die jeweiligen Höchstwerte, Endwerte, Betriebssystem, RAM, CPU, Architektur und Medium protokollieren.

Classic nutzt üblicherweise 1 MiB Transferpuffer und Modern 4 MiB. x86 begrenzt den Transferpuffer auf höchstens 4 MiB. Bei weniger als 512 MiB verfügbarem RAM oder mindestens 92 Prozent Speicherauslastung nutzen neue Aufgaben höchstens 1 MiB. Bereits laufende Transfers behalten ihren Puffer. Die automatische Wahl berücksichtigt installierten und freien RAM, Prozessorzahl, Prozessarchitektur, Windows-Version, Display und Last. Der Displayzustand stammt aus der Windows-Geräteerkennung und dient als Näherung.

Die Oberfläche aktiviert COM-Engines beim jeweiligen Vorgang, liest Dateiinhalte blockweise und verwendet einen virtuellen ListView. Die Verzeichnisdatei des ISO-Readers wird sektorenweise verarbeitet. Image-Dateiinhalte werden vollständig gestreamt. Bis zu 100.000 Metadateneinträge können gleichzeitig indiziert werden. Lastmessungen auf Windows und mit optischer Hardware stehen aus.

## Erste Messung

Debian 13 x86-64, Wine 11.22 mit gemeldeter Windows-Version 10.0.19045, AMD EPYC 9V74 mit drei sichtbaren logischen Prozessoren und 8 GiB RAM-Limit für die Cloud-Umgebung. Der Startbildschirm war sichtbar und die Laufwerkserkennung abgeschlossen. Der Test las die Werte rund eine Sekunde nach dem Öffnen. Es war kein optisches Laufwerk verfügbar.

| x64-Szenario | Working Set | Belegter oder reservierter virtueller Adressraum |
| --- | ---: | ---: |
| Portable, Classic | 28,70 MiB | 47,41 MiB |
| Portable, Modern | 28,61 MiB | 47,41 MiB |
| Installiert, Classic | 29,02 MiB | 47,41 MiB |

Wine meldete für Private Bytes den Wert null. Native Private-Bytes-Messungen und x86-Laufzeitmessungen stehen aus. Die drei Messpunkte dienen als Ausgangswerte und enthalten keine Messung unter Brenn- oder Leselast.
