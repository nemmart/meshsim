#!/bin/bash
# tools/freeze/strip_files.sh: the files the comment strip reads, "LANG<TAB>path" (LANG c or sh, cstrip's): the frozen
# copy's source files (tools/freeze/frozen.sh), less the history (planning/, spec/history/), the files held to v1.3's
# bytes (COPIES.sha256, check_copies; pipeline/FROM-V1.3.sha256 and the snapshots under pipeline/inputs/src/,
# pipeline/check.sh), and these tools, whose comments show tags as examples.
set -eu
export LC_ALL=C   # byte order for sort, ls and globs, and nm's symbol order: the outputs are the same in any locale
cd "$(dirname "$0")/../.."
tools/freeze/frozen.sh | grep -Ev '^(planning|spec/history|tools/freeze|pipeline/inputs/src)/' |
  grep -vxF -f <(grep -v '^#' COPIES.sha256 | awk '{print $2}'; grep -v '^#' pipeline/FROM-V1.3.sha256 | awk '{print "pipeline/" $2}') |
awk '/\.(cpp|cc|h|hpp|c|java|ld|S)$/ { print "c\t" $0; next }
     /\.(sh|mk|py|inc)$/ || /(^|\/)Makefile$/ { print "sh\t" $0 }'
