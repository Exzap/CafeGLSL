#!/usr/bin/env bash

set -euo pipefail

usage()
{
	echo "usage: ${0##*/} <host|wiiu> <builddir> <stagedir>" 1>&2
	exit 1
}

[ $# -eq 3 ] || usage

MODE="$1"
BUILDDIR="$2"
STAGEDIR="$3"

SRCDIR="$(cd "${BASH_SOURCE%/*}/../.." && pwd)"
CAFEDIR="${SRCDIR}/cafecompiler"

if [ ! -d "${BUILDDIR}" ]; then
	echo "no such build directory: ${BUILDDIR}" 1>&2
	exit 1
fi
BUILDDIR="$(cd "${BUILDDIR}" && pwd)"

VERSION="$("${CAFEDIR}/ci/version.sh")"

rm -rf "${STAGEDIR}"
mkdir -p "${STAGEDIR}/include/cafeglsl" "${STAGEDIR}/lib"

install -m 644 "${CAFEDIR}/CafeGLSLCompiler.h" "${STAGEDIR}/include/cafeglsl/"
install -m 644 "${CAFEDIR}/cafe_gx2.h" "${STAGEDIR}/include/cafeglsl/"
install -m 644 "${CAFEDIR}/gfd.h" "${STAGEDIR}/include/cafeglsl/"
install -m 644 "${BUILDDIR}/cafecompiler/libcafeglsl.a" "${STAGEDIR}/lib/"
install -m 644 "${SRCDIR}/licenses/MIT" "${STAGEDIR}/LICENSE"

case "${MODE}" in
host)
	mkdir -p "${STAGEDIR}/bin"
	# meson appends .exe to the target name, which already ends in .elf.
	if [ -f "${BUILDDIR}/cafecompiler/glslcompiler.elf.exe" ]; then
		install -m 755 "${BUILDDIR}/cafecompiler/glslcompiler.elf.exe" \
			"${STAGEDIR}/bin/glslcompiler.exe"
		BINARY="bin/glslcompiler.exe"
	else
		install -m 755 "${BUILDDIR}/cafecompiler/glslcompiler.elf" \
			"${STAGEDIR}/bin/glslcompiler.elf"
		BINARY="bin/glslcompiler.elf"
	fi
	;;
wiiu)
	mkdir -p "${STAGEDIR}/lib/pkgconfig" "${STAGEDIR}/lib/cmake/cafeglsl"
	install -m 644 "${BUILDDIR}/meson-private/cafeglsl.pc" \
		"${STAGEDIR}/lib/pkgconfig/cafeglsl.pc"
	install -m 644 "${BUILDDIR}/cafecompiler/cafeglslConfig.cmake" \
		"${STAGEDIR}/lib/cmake/cafeglsl/"
	install -m 644 "${BUILDDIR}/cafecompiler/cafeglslConfigVersion.cmake" \
		"${STAGEDIR}/lib/cmake/cafeglsl/"
	BINARY=""
	;;
*)
	usage
	;;
esac

sed -e "s|@VERSION@|${VERSION}|g" -e "s|@BINARY@|${BINARY}|g" \
	"${CAFEDIR}/ci/README-${MODE}.md" > "${STAGEDIR}/README.md"

echo "staged ${MODE} tree for ${VERSION} in ${STAGEDIR}"
