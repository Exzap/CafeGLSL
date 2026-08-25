#!/usr/bin/env bash

set -euo pipefail

usage()
{
	echo "usage: ${0##*/} <artifacts-dir> <outdir> <version>" 1>&2
	exit 1
}

[ $# -eq 3 ] || usage

ARTIFACTS="$1"
OUTDIR="$2"
VERSION="$3"

if [ ! -d "${ARTIFACTS}" ]; then
	echo "no such artifacts directory: ${ARTIFACTS}" 1>&2
	exit 1
fi

mkdir -p "${OUTDIR}"
OUTDIR="$(cd "${OUTDIR}" && pwd)"

WORK="$(mktemp -d)"
trap 'rm -rf "${WORK}"' EXIT

FOUND=0

for STAGED in "${ARTIFACTS}"/cafeglsl-*/; do
	[ -d "${STAGED}" ] || continue

	PLATFORM="$(basename "${STAGED}")"
	PLATFORM="${PLATFORM#cafeglsl-}"
	BASE="cafeglsl-${VERSION}-${PLATFORM}"

	rm -rf "${WORK:?}/${BASE}"
	cp -r "${STAGED%/}" "${WORK}/${BASE}"

	# upload-artifact does not carry unix mode bits through.
	if [ -d "${WORK}/${BASE}/bin" ]; then
		chmod 755 "${WORK}/${BASE}"/bin/*
	fi

	case "${PLATFORM}" in
	windows-*)
		if command -v zip >/dev/null 2>&1; then
			(cd "${WORK}" && zip -qr "${OUTDIR}/${BASE}.zip" "${BASE}")
		else
			(cd "${WORK}" && python3 -c 'import shutil, sys; shutil.make_archive(sys.argv[1], "zip", ".", sys.argv[2])' \
				"${OUTDIR}/${BASE}" "${BASE}")
		fi
		echo "${BASE}.zip"
		;;
	*)
		tar -C "${WORK}" -cJf "${OUTDIR}/${BASE}.tar.xz" "${BASE}"
		echo "${BASE}.tar.xz"
		;;
	esac

	FOUND=$((FOUND + 1))
done

if [ "${FOUND}" -eq 0 ]; then
	echo "no cafeglsl-* artifact directories found in ${ARTIFACTS}" 1>&2
	exit 1
fi

(cd "${OUTDIR}" && sha256sum cafeglsl-* > SHA256SUMS)
echo "SHA256SUMS"
