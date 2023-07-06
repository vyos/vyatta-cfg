#!/bin/sh

if [ -d /config/scripts/commit/post-hooks.d/ ]; then
    sg vyattacfg -c "/bin/run-parts /config/scripts/commit/post-hooks.d"
fi

# T775 Add commit post-hook for /run
if [ -d /run/scripts/commit/post-hooks.d/ ]; then
    sg vyattacfg -c "/bin/run-parts /run/scripts/commit/post-hooks.d"
fi
