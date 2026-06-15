#!/usr/bin/env bash
# scripts/build_hamlib.sh — Hamlib aus Quellcode bauen (Windows/MSYS2)
#
# Führe dieses Skript einmalig in einer MSYS2 MinGW64-Shell aus.
# Danach findet cmake die statische Library automatisch und kompiliert
# ale_cli mit Radio CAT/PTT-Unterstützung.
#
# Voraussetzungen (MSYS2 MinGW64-Shell):
#   pacman -S git base-devel mingw-w64-x86_64-toolchain automake autoconf libtool
#
# Aufruf:
#   bash scripts/build_hamlib.sh
#   mkdir build && cd build && cmake .. && cmake --build .

set -e

HAMLIB_VERSION="4.5.2"
HAMLIB_TAG="Hamlib-${HAMLIB_VERSION}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"

HAMLIB_SRC="${REPO_ROOT}/libs/hamlib-src"
HAMLIB_BUILD="${REPO_ROOT}/libs/hamlib-build"
HAMLIB_PREFIX="${REPO_ROOT}/libs/hamlib-built"

echo "=== PC-ALE: Hamlib ${HAMLIB_VERSION} Quellcode-Build ==="
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

# ── 2. Bootstrap (autotools-Makefiles generieren) ─────────────────────────────
echo "[2/5] Bootstrap (autoconf/automake) ..."
cd "${HAMLIB_SRC}"
./bootstrap

# ── 3. Out-of-tree Configure ──────────────────────────────────────────────────
echo "[3/5] Configure (statisch, Release-Optimierung, keine Bindings/Tests) ..."
mkdir -p "${HAMLIB_BUILD}"
cd "${HAMLIB_BUILD}"

"${HAMLIB_SRC}/configure" \
    --prefix="${HAMLIB_PREFIX}" \
    --enable-static \
    --disable-shared \
    --without-python-binding \
    --without-perl-binding \
    --without-tcl-binding \
    --without-lua-binding \
    --disable-tests \
    --disable-doc \
    CFLAGS="-O2 -DNDEBUG" \
    CXXFLAGS="-O2 -DNDEBUG" \
    --quiet

# ── 4. Build + Install ────────────────────────────────────────────────────────
echo "[4/5] Baue und installiere ($(nproc 2>/dev/null || echo 4) Jobs) ..."
make -j"$(nproc 2>/dev/null || echo 4)"
make install

# ── 5. Aufräumen ─────────────────────────────────────────────────────────────
echo "[5/5] Räume auf ..."
# rigctl/rigctld/rotctl usw. sind separate Tools — nicht Teil unseres Builds.
# Sie werden extern installiert (MSYS2-Paket oder Hamlib-Release-ZIP), falls
# der Nutzer einen echten rigctld-Daemon betreiben will.
# libhamlib.a enthält bereits den NETRIGCTL-Client-Code zum Verbinden.
rm -rf "${HAMLIB_PREFIX}/bin" "${HAMLIB_PREFIX}/share"
# Debug-Symbole aus der statischen Lib entfernen (~60-70% der Dateigröße)
strip --strip-debug "${HAMLIB_PREFIX}/lib/libhamlib.a" 2>/dev/null || true
# Quellcode und Build-Artefakte nicht mehr nötig
rm -rf "${HAMLIB_SRC}" "${HAMLIB_BUILD}"

# ── Fertig ────────────────────────────────────────────────────────────────────
du -sh "${HAMLIB_PREFIX}" 2>/dev/null || true
echo ""
echo "=== Fertig! ==="
echo ""
echo "Installiert nach: ${HAMLIB_PREFIX}"
echo "  ${HAMLIB_PREFIX}/include/hamlib/rig.h"
echo "  ${HAMLIB_PREFIX}/lib/libhamlib.a"
echo ""
echo "Nächster Schritt — cmake in einer normalen Shell/PowerShell:"
echo "  mkdir build && cd build && cmake .. && cmake --build ."
echo ""
echo "Prüfen ob Hamlib erkannt wurde:"
echo "  cmake .. 2>&1 | grep -i hamlib"
