#!/bin/sh

if [ -d /config/scripts/commit/pre-hooks.d/ ]; then
    /bin/run-parts /config/scripts/commit/pre-hooks.d
fi

