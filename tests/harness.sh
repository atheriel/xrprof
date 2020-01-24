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

# Run the script as the original sudo user (i.e. not root), wait a little bit
# for the fork(s), and then start profiling.
sudo -u $SUDO_USER $RSCRIPT $TEST &
sleep 0.25
PID=`ps --ppid $! -o pid=`
$BIN -F 50 $@ -p $PID > $OUTFILE

# Ensure we can actually look at the output.
chown $SUDO_USER:$SUDO_USER $OUTFILE
