#!/bin/sh

POSIXLY_CORRECT=1
export POSIXLY_CORRECT

TIDY='tidy --show-body-only 1 --quiet 1 --show-warnings 0'
SCRIPT="$1"
TESTDIR="$2"
PASSED=0
FAILED=0

abort() {
    echo "Error: $*"
    exit 1
}

EXECUTE="${SCRIPT/ */}"

test -f "$EXECUTE"   || abort "argument #1 invalid; not a file"
test -x "$EXECUTE"   || abort "argument #1 invalid; not executable"
echo "" | "$EXECUTE" || abort "argument #1 invalid; script failed to run"
test -d "$TESTDIR"  || abort "argument #2 invalid; not a directory"

for TEXT in "$TESTDIR"/*.text; do
    test -f "$TEXT" || abort "empty or invalid test directory"
    printf "$(basename "$TEXT" .text) ... "
    HTML=$(echo "$TEXT" | sed 's/\.text$/.html/')

    # We use mktemp to create an unpredictable, temporary filename.
    # The created file is immediately deleted, since we only want a
    # name to pass to mkfifo and "mktemp -u" is not portable.
    PIPE=$(mktemp .testpipe-XXXXXXXX)
    test -f "$PIPE" -a -n "$PIPE" || abort "mktemp failed"
    trap 'rm -f "$PIPE"' EXIT INT TERM HUP
    rm -f "$PIPE"
    mkfifo -m 0600 "$PIPE" || abort "unable to create named pipe"

    $SCRIPT "$TEXT" | $TIDY > "$PIPE" &
    DIFF=$($TIDY "$HTML" | diff "$PIPE" -)
    if test "$?" = 0; then
        PASSED=$(expr $PASSED + 1)
        echo OK
    else
        FAILED=$(expr $FAILED + 1)
        echo FAILED
        printf "\n$DIFF\n\n"
    fi
    rm -f "$PIPE"
done

printf "\n\n$PASSED passed; $FAILED failed.\n"
test "$FAILED" = 0 || exit 1
