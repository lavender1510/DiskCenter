# Kompatibilität

| Zielsystem | x86 | x64 | Stand |
| --- | --- | --- | --- |
| Windows 7 SP1 | Ziel | Ziel | Native VM- und Hardwaretests offen |
| Windows 8 und 8.1 | Ziel | Ziel | Native VM- und Hardwaretests offen |
| Windows 10 | Ziel | Ziel | Native VM- und Hardwaretests offen |
| Windows 11 | — | Ziel | Native VM- und Hardwaretests offen |
| Debian 13 mit Wine 11.22 | PE und Installer-Inhalt geprüft | Laufzeittests bestanden | Siehe [TESTING.md](TESTING.md) |

Die Windows-Zieldateien werden separat als PE x86 und x64 gebaut. `WINVER` und `_WIN32_WINNT` sind auf 0x0601 eingestellt. Die Release-Toolchain nutzt statische C++-Laufzeitkomponenten und das von Windows mitgelieferte `msvcrt.dll`. Geprüft werden auch die tatsächlichen PE-Importe. Wine liefert ergänzende Laufzeitprüfungen. Der Kompatibilitätsnachweis auf einer echten Windows-7-SP1-Installation ist weiterhin offen.

IMAPI2, Laufwerk, Firmware und Medienzustand bestimmen, welche Rohlinge gelesen und geschrieben werden können. Audio-CDs, Multisession und mehrere Spuren fallen außerhalb des aktuellen sektorweisen ISO-Lesepfads. Die integrierte Image-Ansicht ist auf ISO 9660 und Joliet begrenzt. Für ein eingelegtes UDF-Medium verwendet der Disc-Explorer das eingehängte Windows-Dateisystem. Bei einem reinen UDF-Image prüft der Image-Manager Erkennungssequenz und Anker, eine vollständige UDF-Dateisystemprüfung steht aus.

Windows-7-Tests bitte mit SP1, Standardbenutzer, 1 bis 2 GiB Arbeitsspeicher und echten internen oder USB-Laufwerken durchführen. Die Anwendung benötigt im aktuellen Umfang keine Netzwerkverbindung.
