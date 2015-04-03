#!/bin/sh

if [ -d /config/scripts/commit/post-hooks.d/ ]; then
    /bin/run-parts /config/scripts/commit/post-hooks.d
fi
