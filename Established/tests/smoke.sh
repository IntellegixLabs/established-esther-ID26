#!/bin/sh
set -eu

esther=$1
root=$2

assert_output() {
    example=$1
    expected=$2
    actual=$("$esther" run "$root/examples/$example.esther")
    if [ "$actual" != "$expected" ]; then
        printf 'Unexpected output from %s:\n%s\n' "$example" "$actual" >&2
        exit 1
    fi
}

assert_output hello 'Hello, World!'
assert_output functions '8'
assert_output shopping-list 'Your cart:
bread
butter
milk
eggs'
assert_output classes 'Health:
120'
assert_output control-flow 'Count is three.
1
Handled missing file.'
assert_output imports 'Hello, Ada'

temp_dir=$(mktemp -d)
trap 'rm -rf "$temp_dir"' EXIT HUP INT TERM
cp "$root/examples/file-io.esther" "$temp_dir/file-io.esther"
actual=$("$esther" run "$temp_dir/file-io.esther")
if [ "$actual" != 'File round trip works.' ] || [ -e "$temp_dir/roundtrip.txt" ]; then
    printf 'File I/O example failed or left its output file behind.\n' >&2
    exit 1
fi

"$esther" check "$root/examples/number-guessing.esther" >/dev/null
game_output=$(printf '1\n2\n3\n' | "$esther" run "$root/examples/number-guessing.esther")
case "$game_output" in
    *"Game over. The number was:"*) ;;
    *) printf 'Number guessing example did not finish.\n' >&2; exit 1 ;;
esac
"$esther" ir "$root/examples/functions.esther" | grep -q '"category": "program"'
if "$esther" check "$root/examples/hello.est" >/dev/null 2>&1; then
    printf 'The CLI accepted the obsolete .est extension.\n' >&2
    exit 1
fi