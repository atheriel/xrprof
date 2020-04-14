#!/bin/sh

usage() {
    echo "Usage: $0 TEST [OPTIONS...]"
}

if [ -z "$1" ]; then
    usage
    exit 1
fi

TEST=$1
shift
OUTFILE=`echo $TEST | sed 's/\.R/\.out/'`

if [ -z "$RSCRIPT" ]; then
    RSCRIPT=`which Rscript`
fi

if [ -z "$BIN" ]; then
    BIN="./xrprof"
fi

set -e

if [ -z "$SUDO_USER" ]; then
    $RSCRIPT $TEST &
    PID=$!
    # Try to detect Cygwin. Unixy PIDs will not work for OpenProcess().
    WINPID=`cat /proc/$PID/winpid || true`
    if [ ! -z "$WINPID" ]; then
        PID=$WINPID
    fi
else
    # Run the script as the original sudo user (i.e. not root), so that it
    # behaves as expected.
    sudo -u $SUDO_USER $RSCRIPT $TEST &
    # Wait a little bit for the fork(s).
    sleep 0.25
    PID=`ps --ppid $! -o pid=`
fi

$BIN -F 50 $@ -p $PID > $OUTFILE

if [ ! -z "$SUDO_USER" ]; then
    # Ensure we can actually look at the output.
    chown $SUDO_USER:$SUDO_USER $OUTFILE
fi

LINES=`wc -l $OUTFILE | cut -d ' ' -f 1`
echo "--- $OUTFILE ($LINES lines):"
head $OUTFILE
