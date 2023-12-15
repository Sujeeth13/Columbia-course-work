#!/bin/bash

INIT_COMMIT=$(git log --reverse --pretty=format:"%h" | head -n 1)
IGNORES=FILE_PATH_CHANGES,SPDX_LICENSE_TAG

HW4_IGNORES=$IGNORES
git diff $INIT_COMMIT | linux/scripts/checkpatch.pl --ignore $HW4_IGNORES
