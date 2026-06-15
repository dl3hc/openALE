Hamlib Windows Runtime Bundle
==============================

Lege hier die Hamlib-Binaries ab, damit CMake sie automatisch
neben ale_cli.exe kopiert (kein PATH-Setup erforderlich).

Verzeichnisstruktur
-------------------
deps/hamlib/
  bin/
    libhamlib-4.dll         <- Hamlib-Laufzeit-DLL
    libwinpthread-1.dll     <- MinGW-Laufzeit (oft Hamlib-Abhängigkeit)
    *.dll                   <- weitere Abhängigkeiten (libgcc_s_seh-1.dll etc.)
  include/
    hamlib/
      rig.h                 <- Hamlib-Header
      ...
  lib/
    libhamlib.dll.a         <- Import-Library für den Linker (MinGW)
    libhamlib.lib           <- Import-Library (MSVC, falls vorhanden)

Woher bekommt man die Dateien?
-------------------------------
Option A — Hamlib-Release-Paket (empfohlen):
  https://github.com/Hamlib/Hamlib/releases
  → hamlib-w64-4.x.zip (64-bit Windows, MinGW)
  Entpacken, Dateien aus bin/ include/ lib/ hierher kopieren.

Option B — MSYS2:
  pacman -S mingw-w64-x86_64-hamlib
  DLLs liegen in C:\msys64\mingw64\bin\
  Headers in    C:\msys64\mingw64\include\hamlib\
  Libs in       C:\msys64\mingw64\lib\

Lizenz
------
Hamlib ist LGPL-2.1-lizenziert.
libwinpthread-1.dll ist Teil von MinGW-w64 (MIT/LGPL).
Beim Verteilen des Programms die jeweiligen Lizenztexte beilegen.
