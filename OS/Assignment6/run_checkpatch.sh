#!/bin/bash

IGNORES=FILE_PATH_CHANGES,SPDX_LICENSE_TAG,MISSING_EOF_NEWLINE,EXPORT_SYMBOL
HW6_IGNORES=$IGNORES,ENOSYS,AVOID_EXTERNS,LINE_CONTINUATIONS,TRAILING_SEMICOLON
HW6_FILES=`find . -name "*.c"`
util/checkpatch.pl -f --no-tree --ignore $IGNORES $HW6_FILES

# ENOSYS: Looks like ENOSYS is often misused in the kernel so checkpatch always
#         warns on seeing ENOSYS. Our use is correct, though!
#         (Nonexistent syscall was called)
# EXPORT_SYMBOL: Buggy for our use-case, let's just ignore this.
# AVOID_EXTERNS: Checkpatch will complain if you extern in a .c file instead of
#                a .h file.
# MISSING_EOF_NEWLINE: Expects a file to end with a newline, which isn't the
#                      case for symlinks.
# LINE_CONTINUATIONS: Sometimes these are useful, as is the case with long
#                     macros.
