#!/bin/sh

if [ -d /config/scripts/commit/post-hooks.d/ ]; then
    sg vyattacfg -c "/bin/run-parts /config/scripts/commit/post-hooks.d"
fi
