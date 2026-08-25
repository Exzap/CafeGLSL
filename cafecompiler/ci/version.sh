#!/usr/bin/env bash

set -euo pipefail

CAFEDIR="$(cd "${BASH_SOURCE%/*}/.." && pwd)"

VERSION="$(sed -n "s/^cafeglsl_version = '\([^']*\)'.*/\1/p" "${CAFEDIR}/meson.build")"

if [ -z "${VERSION}" ]; then
	echo "could not read cafeglsl_version from ${CAFEDIR}/meson.build" 1>&2
	exit 1
fi

echo "${VERSION}"
