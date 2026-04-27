#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="$ROOT/bgp_simulator"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

require_line() {
    local file="$1"
    local line="$2"
    if ! grep -Fxq "$line" "$file"; then
        echo "Missing expected line in $file:" >&2
        echo "$line" >&2
        echo "Actual output:" >&2
        cat "$file" >&2
        exit 1
    fi
}

mkdir "$TMP/basic"
cat > "$TMP/basic/relationships.txt" <<'DATA'
1|2|-1|test
1|3|0|test
3|4|-1|test
DATA
cat > "$TMP/basic/anns.csv" <<'DATA'
seed_asn,prefix,rov_invalid
2,203.0.113.0/24,False
DATA
: > "$TMP/basic/rov_asns.csv"

(cd "$TMP/basic" && "$BIN" --relationships relationships.txt --announcements anns.csv --rov-asns rov_asns.csv)
require_line "$TMP/basic/ribs.csv" 'asn,prefix,as_path'
require_line "$TMP/basic/ribs.csv" '1,203.0.113.0/24,"(1, 2)"'
require_line "$TMP/basic/ribs.csv" '2,203.0.113.0/24,"(2,)"'
require_line "$TMP/basic/ribs.csv" '3,203.0.113.0/24,"(3, 1, 2)"'
require_line "$TMP/basic/ribs.csv" '4,203.0.113.0/24,"(4, 3, 1, 2)"'

mkdir "$TMP/rov"
cat > "$TMP/rov/relationships.txt" <<'DATA'
1|2|-1|test
DATA
cat > "$TMP/rov/anns.csv" <<'DATA'
seed_asn,prefix,rov_invalid
2,198.51.100.0/24,True
DATA
cat > "$TMP/rov/rov_asns.csv" <<'DATA'
1
DATA

(cd "$TMP/rov" && "$BIN" --relationships relationships.txt --announcements anns.csv --rov-asns rov_asns.csv)
require_line "$TMP/rov/ribs.csv" '2,198.51.100.0/24,"(2,)"'
if grep -Fq '1,198.51.100.0/24' "$TMP/rov/ribs.csv"; then
    echo "ROV AS installed an invalid route" >&2
    cat "$TMP/rov/ribs.csv" >&2
    exit 1
fi

mkdir "$TMP/cycle"
cat > "$TMP/cycle/relationships.txt" <<'DATA'
1|2|-1|test
2|1|-1|test
DATA
cat > "$TMP/cycle/anns.csv" <<'DATA'
seed_asn,prefix,rov_invalid
1,192.0.2.0/24,False
DATA
: > "$TMP/cycle/rov_asns.csv"

if (cd "$TMP/cycle" && "$BIN" --relationships relationships.txt --announcements anns.csv --rov-asns rov_asns.csv >stdout.txt 2>stderr.txt); then
    echo "Cycle input unexpectedly succeeded" >&2
    exit 1
fi
if ! grep -qi 'cycle' "$TMP/cycle/stderr.txt"; then
    echo "Cycle error did not mention cycle" >&2
    cat "$TMP/cycle/stderr.txt" >&2
    exit 1
fi

echo "All local regression tests passed."
