# SPDX-License-Identifier: AGPL-3.0-only
# Linux: builds the pinned ngspice shared library from its source release into third_party/ngspice
# and checks the source's SHA-256 (ADR 0008). For CI, where the engine's threading code runs under
# the sanitizers; it is never shipped. Only the files Proton uses are kept:
#   third_party/ngspice/lib/libngspice.so
#   third_party/ngspice/include/ngspice/sharedspice.h
#   third_party/ngspice/VERSION
# No code models (*.cm, *.osdi) are built into or kept with it: table.cm is GPL.
#
# Needs a C compiler and make; the release ships its configure script and generated parsers.
# Usage: bash tools/fetch-ngspice.sh [--force]
set -euo pipefail

# The pin: the same release as tools/fetch-ngspice.ps1. Changing it means a new SHA-256 and an
# update of ADR 0002 and ADR 0008.
version=46
archive="ngspice-${version}.tar.gz"
url="https://downloads.sourceforge.net/project/ngspice/ng-spice-rework/old-releases/${version}/${archive}"
sha256=a0d1699af1940b06649276dcd6ff5a566c8c0cad01b2f7b5e99dedbb4d64c19b

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
dest="${root}/third_party/ngspice"

complete() {
    [[ -f "${dest}/VERSION" && "$(tr -d '[:space:]' < "${dest}/VERSION")" == "${version}" &&
       -f "${dest}/lib/libngspice.so" && -f "${dest}/include/ngspice/sharedspice.h" ]]
}

if [[ "${1:-}" != "--force" ]] && complete; then
    echo "ngspice ${version} is already in ${dest}"
    exit 0
fi

work="$(mktemp -d)"
trap 'rm -rf "${work}"' EXIT

echo "Downloading ${url}"
curl --fail --location --silent --show-error --user-agent Wget --output "${work}/${archive}" "${url}"
actual="$(sha256sum "${work}/${archive}" | cut -d' ' -f1)"
if [[ "${actual}" != "${sha256}" ]]; then
    echo "SHA-256 mismatch for ${archive}: expected ${sha256}, got ${actual}" >&2
    exit 1
fi

tar -xzf "${work}/${archive}" -C "${work}"
src="${work}/ngspice-${version}"
mkdir "${src}/build"
echo "Building libngspice (a minute or two)"
(
    cd "${src}/build"
    ../configure --with-ngshared --disable-debug --prefix="${work}/install" > "${work}/configure.log" 2>&1
    make -j"$(nproc)" > "${work}/make.log" 2>&1
    make install > "${work}/install.log" 2>&1
) || { tail -n 30 "${work}"/*.log >&2; exit 1; }

# Only the library and its header; the Windows files, if any, stay where they are.
mkdir -p "${dest}/lib" "${dest}/include/ngspice"
cp -L "${work}/install/lib/libngspice.so" "${dest}/lib/libngspice.so"
cp "${work}/install/include/ngspice/sharedspice.h" "${dest}/include/ngspice/sharedspice.h"
echo "${version}" > "${dest}/VERSION"
echo "ngspice ${version} installed in ${dest}"
