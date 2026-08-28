#!/bin/sh
set -eu

missing=
for command_name in bash bzip2 cksum cmp curl docker git make patch python3 tar xz; do
	if command -v "$command_name" >/dev/null 2>&1; then
		printf 'OK       %s\n' "$command_name"
	else
		printf 'MISSING  %s\n' "$command_name"
		missing="$missing $command_name"
	fi
done

if command -v sha256sum >/dev/null 2>&1 || \
	command -v shasum >/dev/null 2>&1; then
	echo "OK       SHA-256 utility"
else
	echo "MISSING  sha256sum or shasum"
	missing="$missing sha256sum-or-shasum"
fi

if command -v docker >/dev/null 2>&1; then
	if docker info >/dev/null 2>&1; then
		echo "OK       Docker engine is running"
	else
		echo "NOT READY Docker is installed, but its engine is unavailable"
		missing="$missing docker-engine"
	fi
fi

if [ -n "$missing" ]; then
	echo >&2
	echo "The full build cannot start until these are available:$missing" >&2
	echo "See docs/BUILDING.md for macOS and Debian/Ubuntu setup commands." >&2
	exit 1
fi

echo
echo "Host prerequisites for the Docker-based target builds are ready."
echo "The optional native libexword build has additional packages; see docs/BUILDING.md."
