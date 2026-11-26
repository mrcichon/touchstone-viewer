#!/bin/bash
# usage: open_many_curves.sh COUNT FOLDER [viewer options]
# Opens the viewer with the first COUNT measurement files below FOLDER, in name order.
basic="$(cd "$(dirname "$0")/.." && pwd)"
viewer="$basic/build/release/app/touchstone_viewer"
if [ $# -lt 2 ]; then
    sed -n 2,3p "$0"
    exit 1
fi
count="$1"
folder="$2"

if [ ! -x "$viewer" ]; then
    echo "no release build: run 'cmake --preset release' and 'cmake --build --preset release' in $basic"
    exit 1
fi

files=()
while IFS= read -r file; do files+=("$file"); done < <(find "$folder" -iname '*.s[0-9]p' | sort -V | head -n "$count")
echo "opening ${#files[@]} files from $folder"
exec "$viewer" "${files[@]}" "${@:3}"
