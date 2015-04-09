#!/bin/sh

if [ -d /config/scripts/commit/pre-hooks.d/ ]; then
    sg vyattacfg -c "/bin/run-parts /config/scripts/commit/pre-hooks.d"
fi

