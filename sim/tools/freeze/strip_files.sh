#!/bin/bash
# tools/freeze/strip_files.sh: the files the comment strip reads, "LANG<TAB>path" (LANG c or sh, cstrip's): the frozen
# copy's source files (tools/freeze/frozen.sh), less the history (planning/, spec/history/) and the files copied from
# v1.3 (COPIES.sha256: check_copies holds them to v1.3's bytes).
set -eu
export LC_ALL=C   # byte order for sort, ls and globs, and nm's symbol order: the outputs are the same in any locale
cd "$(dirname "$0")/../.."
tools/freeze/frozen.sh | grep -Ev '^(planning|spec/history)/' | grep -vxF -f <(grep -v '^#' COPIES.sha256 | awk '{print $2}') |
awk '/\.(cpp|cc|h|hpp|c|java|ld|S)$/ { print "c\t" $0; next }
     /\.(sh|mk|py|inc)$/ || /(^|\/)Makefile$/ { print "sh\t" $0 }'
