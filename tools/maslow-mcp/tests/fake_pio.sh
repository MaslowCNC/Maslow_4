#!/bin/sh
# Stand-in for PlatformIO in tests: logs its arguments; uploads fail when FAKE_PIO_FAIL_UPLOAD=1.
echo "fake pio $*"
case " $* " in
    *" upload "*) [ "$FAKE_PIO_FAIL_UPLOAD" = 1 ] && { echo "espota: Error Uploading"; exit 1; } ;;
esac
exit 0
