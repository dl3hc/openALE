#!/usr/bin/env bash
# scripts/build_hamlib.sh — Hamlib aus Quellcode bauen (Windows/MSYS2)
#
# Führe dieses Skript einmalig in einer MSYS2 MinGW64-Shell aus.
# Danach findet cmake die DLL + Import-Library automatisch.
#
# Voraussetzungen (MSYS2 MinGW64-Shell):
#   pacman -S git base-devel mingw-w64-x86_64-toolchain automake autoconf libtool
#
# Aufruf:
#   bash scripts/build_hamlib.sh
#   mkdir build && cd build && cmake .. && cmake --build .

set -e

HAMLIB_VERSION="4.7.2"
HAMLIB_TAG="${HAMLIB_VERSION}"
MINGW_BIN="${MINGW_PREFIX:-/mingw64}/bin"
MINGW_INCLUDE="${MINGW_PREFIX:-/mingw64}/include"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"

HAMLIB_SRC="${REPO_ROOT}/libs/hamlib-src"
HAMLIB_BUILD="${REPO_ROOT}/libs/hamlib-build"
HAMLIB_PREFIX="${REPO_ROOT}/libs/hamlib-built"

echo "=== Hamlib ${HAMLIB_VERSION} Build (shared DLL) ==="
echo ""
echo "Quellcode  : ${HAMLIB_SRC}"
echo "Build-Dir  : ${HAMLIB_BUILD}"
echo "Install    : ${HAMLIB_PREFIX}"
echo ""

# ── 1. Quellcode holen ────────────────────────────────────────────────────────
if [ -d "${HAMLIB_SRC}/.git" ]; then
    echo "[1/5] Hamlib-Quellcode bereits vorhanden — überspringe Clone"
else
    echo "[1/5] Klone Hamlib ${HAMLIB_VERSION} ..."
    git clone --depth 1 --branch "${HAMLIB_TAG}" \
        https://github.com/Hamlib/Hamlib.git "${HAMLIB_SRC}"
fi

# ── 2. Bootstrap ──────────────────────────────────────────────────────────────
echo "[2/5] Bootstrap (autoconf/automake) ..."
cd "${HAMLIB_SRC}"
./bootstrap

# ── 3. Configure (shared DLL) ─────────────────────────────────────────────────
echo "[3/5] Configure (shared DLL, keine Bindings/Tests) ..."
mkdir -p "${HAMLIB_BUILD}"
cd "${HAMLIB_BUILD}"

"${HAMLIB_SRC}/configure" \
    --prefix="${HAMLIB_PREFIX}" \
    --enable-shared \
    --disable-static \
    --without-cxx-binding \
    --without-libusb \
    --without-readline \
    CFLAGS="-O2 -DNDEBUG" \
    CXXFLAGS="-O2 -DNDEBUG" \
    --quiet

# ── 4. Build + Install ────────────────────────────────────────────────────────
echo "[4/5] Baue und installiere ($(nproc 2>/dev/null || echo 4) Jobs) ..."
make -j"$(nproc 2>/dev/null || echo 4)"
make install

# ── 5. Post-Install ───────────────────────────────────────────────────────────
echo "[5/5] Post-Install (Import-Lib, pthread-Header, Runtime-DLLs) ..."

# rigctl.exe, rotctl.exe usw. werden nicht benötigt — nur die DLL
find "${HAMLIB_PREFIX}/bin" -name "*.exe" -delete 2>/dev/null || true

# MSVC-kompatible Import-Library aus der DLL erzeugen:
# .def-Datei liegt im Build-Tree, dlltool erstellt daraus ein COFF-.lib
DEF_FILE=$(find "${HAMLIB_BUILD}" -name "libhamlib*.def" 2>/dev/null | head -1)
if [ -n "${DEF_FILE}" ]; then
    cp "${DEF_FILE}" "${HAMLIB_PREFIX}/lib/"
    dlltool \
        -D "${HAMLIB_PREFIX}/bin/libhamlib-4.dll" \
        -d "${DEF_FILE}" \
        -l "${HAMLIB_PREFIX}/lib/libhamlib.lib"
    echo "    libhamlib.lib erstellt (MSVC Import-Library)"
else
    echo "    WARNUNG: .def-Datei nicht gefunden — libhamlib.lib konnte nicht erstellt werden"
    echo "    MinGW-Linker kann libhamlib.dll.a verwenden; MSVC benötigt libhamlib.lib"
fi

# MinGW pthread-Header kopieren:
# Hamlibs rig.h inkludiert pthread.h — MSVC braucht die ECHTEN MinGW-Typen
# (struct-Größen müssen mit libwinpthread-1.dll übereinstimmen)
for hdr in pthread.h sched.h semaphore.h; do
    if [ -f "${MINGW_INCLUDE}/${hdr}" ]; then
        cp "${MINGW_INCLUDE}/${hdr}" "${HAMLIB_PREFIX}/include/"
        echo "    ${hdr} kopiert"
    fi
done

# MinGW Runtime-DLLs die libhamlib-4.dll zur Laufzeit braucht
for dll in libwinpthread-1.dll libgcc_s_seh-1.dll; do
    if [ -f "${MINGW_BIN}/${dll}" ]; then
        cp "${MINGW_BIN}/${dll}" "${HAMLIB_PREFIX}/bin/"
        echo "    ${dll} kopiert"
    fi
done

# Quellcode und Build-Artefakte nicht mehr nötig
rm -rf "${HAMLIB_SRC}" "${HAMLIB_BUILD}"

# ── Fertig ────────────────────────────────────────────────────────────────────
du -sh "${HAMLIB_PREFIX}" 2>/dev/null | awk '{print "Größe: " $1}'
echo ""
echo "=== Fertig! libs/hamlib-built/ enthält: ==="
ls "${HAMLIB_PREFIX}/bin/" 2>/dev/null | sed 's/^/  bin\//'
ls "${HAMLIB_PREFIX}/lib/"*.{a,lib,la} 2>/dev/null | xargs -I{} basename {} | sed 's/^/  lib\//'
echo ""
echo "Nächster Schritt — cmake in PowerShell/CMD:"
echo "  mkdir build && cd build && cmake .. && cmake --build ."
