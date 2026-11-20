# Build

Benötigt werden CMake ab 3.20, C++17 sowie unter Windows ein Windows-Ressourcencompiler. Python ab 3.10 und NSIS 3.x werden zur Paketierung verwendet. Die Anwendung selbst benötigt diese Werkzeuge nicht.

## Windows

Die Release-Pipeline nutzt llvm-mingw 20261117 in der MSVCRT-Variante. Für x86 die Compilerpräfixe `i686-w64-mingw32`, für x64 `x86_64-w64-mingw32` verwenden. Das folgende PowerShell-Beispiel setzt die Werkzeuge im `PATH` voraus.

```powershell
cmake -S . -B build-x86 -G Ninja -DCMAKE_SYSTEM_NAME=Windows -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=i686-w64-mingw32-clang++ -DCMAKE_RC_COMPILER=i686-w64-mingw32-windres
cmake --build build-x86
ctest --test-dir build-x86 --output-on-failure
python packaging/package.py build --arch x86 --exe build-x86/DiscCenter.exe --out dist
```

Für x64 das Präfix, den Build-Ordner und `--arch x64` entsprechend ersetzen. Mit Visual Studio sind `-A Win32` und `-A x64` für separate Build-Ordner verfügbar. Der dokumentierte experimentelle Binärstand wurde mit llvm-mingw kompiliert.

## Linux

Unter Linux lassen sich die portablen Kernfunktionen mit dem Systemcompiler testen.

```sh
cmake -S . -B build-host -DCMAKE_BUILD_TYPE=Release
cmake --build build-host
ctest --test-dir build-host --output-on-failure
```

Windows-Builds lassen sich mit derselben llvm-mingw-Toolchain und den Windows-Optionen auch von Linux aus erzeugen. Die Windows-Tests werden anschließend separat auf Windows oder unter Wine ausgeführt.
