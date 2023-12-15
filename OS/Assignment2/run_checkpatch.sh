#!/bin/bash

INIT_COMMIT=$(git log --reverse --pretty=format:"%h" | head -n 2 | tail -1)
INIT_COMMIT_SUBJECT=$(git log -n 1 --pretty=format:%s $INIT_COMMIT)
IGNORES=FILE_PATH_CHANGES,SPDX_LICENSE_TAG

echo "Checking since commit $INIT_COMMIT: $INIT_COMMIT_SUBJECT"

HW2_IGNORES=$IGNORES
git diff $INIT_COMMIT | linux/scripts/checkpatch.pl --ignore $HW2_IGNORES

