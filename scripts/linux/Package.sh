#!/bin/sh
set -e
root="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$root"

[ $# -gt 0 ] || set -- Debug Release Distribution
base=$(head -n 1 VERSION | tr -d '[:space:]')
DDP_VERSION=${DDP_VERSION:-$base.0}
build=${DDP_BUILD_NUMBER:-$(git rev-list --count HEAD)}
ver="$DDP_VERSION.$build"
IFS=. read -r v_major v_minor v_patch <<EOF
$DDP_VERSION
EOF
[ -n "$v_patch" ] || { echo "DDP_VERSION must be MAJOR.MINOR.PATCH." >&2; exit 1; }

mkdir -p build/package
cp include/DingoDiscordPresence/BuildInfo.h build/package/BuildInfo.h.bak
trap 'cp "$root/build/package/BuildInfo.h.bak" "$root/include/DingoDiscordPresence/BuildInfo.h"; rm -f "$root/build/package/BuildInfo.h.bak"' EXIT
printf '%s\n' "#pragma once" "" \
	"#define DDP_VERSION_MAJOR $v_major" \
	"#define DDP_VERSION_MINOR $v_minor" \
	"#define DDP_VERSION_PATCH $v_patch" \
	"#define DDP_VERSION_BUILD $build" > include/DingoDiscordPresence/BuildInfo.h

for arg in "$@"; do
	case "$(echo "$arg" | tr '[:upper:]' '[:lower:]')" in
		debug) config=Debug ;;
		release) config=Release ;;
		distribution) config=Distribution ;;
		*) echo "Unknown configuration: $arg" >&2; exit 1 ;;
	esac
	"$root/scripts/linux/Build.sh" "$config"

	stage="build/package/stage-$config-linux"
	zip_path="$root/build/package/DingoDiscordPresence-$ver-$config-linux-x86_64.zip"
	rm -rf "$stage" "$zip_path"
	mkdir -p "$stage/lib/$config"
	cp -r include "$stage/include"
	cp -r tools "$stage/tools"
	cp "build/bin/$config-linux-x86_64/DingoDiscordPresence/libDingoDiscordPresence.a" "$stage/lib/$config/"
	cp LICENSE.txt "$stage/"
	echo "$ver" > "$stage/VERSION.txt"
	if command -v zip >/dev/null 2>&1; then
		(cd "$stage" && zip -q -r "$zip_path" .)
	else
		(cd "$stage" && python3 -m zipfile -c "$zip_path" *)
	fi
	rm -rf "$stage"
	echo "Packaged $zip_path"
done
