# Drittanbieterhinweise

DiscCenter ruft die vom Betriebssystem bereitgestellten Komponenten Win32, COM, IMAPI2 und CNG auf. Die Anwendung verteilt diese Komponenten nicht mit.

Die experimentellen Windows-Artefakte werden mit llvm-mingw (MSVCRT-Variante) erzeugt. LLVM, libc++ und compiler-rt verwenden die Apache-Lizenz 2.0 mit LLVM-Ausnahmen. Die Toolchain bündelt zudem MinGW-w64 und dessen Lizenztexte. Die vollständigen Hinweise stammen aus der Distribution. [llvm-mingw-Lizenztexte](licenses/llvm-mingw.txt)

Die Installationsdateien enthalten den NSIS-Installer einschließlich Dekompression. Der Lizenztext und die Hinweise zu den Komponenten stehen in der Paketlizenz. [NSIS-Lizenztexte](licenses/nsis.txt)

[CMake](https://cmake.org/) und [Ninja](https://ninja-build.org/) dienen dem Build und gehören nicht zur Laufzeitdistribution.
