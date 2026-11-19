#!/bin/bash
# deps.sh [--runtime]: checks what the input pipeline needs on Ubuntu 24.04 and prints the apt line for what is missing.
#   (default)   building everything from source: JDK 21 (javac), cmake >= 3.24, g++ 13, libelf-dev, python3, git, curl,
#               the RISC-V gcc (only for meshsim's Stage B controller firmware, sw/replay/build.sh), network to github.com
#               (the clone and cmake's FetchContent: fmt, nlohmann_json, xbyak)
#   --runtime   running with prebuilt classes (a Quest task's kit): java 21, cmake, g++, python3; libelf may be the kit's
# Also checked: the tracer reserves the guest's 4 GB RAM up front (mprotect); with vm.overcommit_memory = 0 and a commit
# limit under ~4.5 GB (a small VM or container) it fails with "failed allocate riscv ram". Fix: more memory or swap, or
# sysctl vm.overcommit_memory=1 (root).
set -u
RUNTIME=0; [ "${1:-}" = --runtime ] && RUNTIME=1
miss=(); bad=0
have() { command -v "$1" > /dev/null 2>&1; }
need() {  # need COMMAND PACKAGE [what]
  if have "$1"; then printf '  ok       %-24s %s\n' "$1" "$(command -v "$1")"; else printf '  MISSING  %-24s (%s) %s\n' "$1" "$2" "${3:-}"; miss+=("$2"); fi
}
echo "input pipeline dependencies ($( . /etc/os-release 2>/dev/null; echo "${PRETTY_NAME:-unknown OS}"), $(nproc) threads, $(free -g | awk '/^Mem/{print $2}') GB)"
need java openjdk-21-jdk-headless
if have java; then
  v=$(java -version 2>&1 | head -1 | sed -E 's/.*version "([0-9]+).*/\1/')
  [ "${v:-0}" -ge 21 ] && echo "           java $v" || { echo "  TOO OLD  java $v (21 needed)"; bad=1; }
fi
[ $RUNTIME = 1 ] || need javac openjdk-21-jdk-headless "(the runner has none: its tasks embed classes compiled elsewhere)"
need cmake cmake
if have cmake; then
  v=$(cmake --version | head -1 | awk '{print $3}')
  [ "$(printf '%s\n3.24\n' "$v" | sort -V | head -1)" = 3.24 ] && echo "           cmake $v" || { echo "  TOO OLD  cmake $v (3.24 needed)"; bad=1; }
fi
need g++ g++
need python3 python3
need sha256sum coreutils
if [ $RUNTIME = 0 ]; then
  need git git
  need curl curl
  need riscv64-unknown-elf-gcc gcc-riscv64-unknown-elf "(Stage B's controller firmware only)"
  if [ -e /usr/include/libelf.h ] || [ -e /usr/include/libelf/libelf.h ]; then echo "  ok       libelf.h"; else echo "  MISSING  libelf.h (libelf-dev)"; miss+=(libelf-dev); fi
else
  if [ -e /usr/include/libelf.h ] || [ -n "${LIBELF_PREFIX:-}" ]; then echo "  ok       libelf (${LIBELF_PREFIX:-system})"; else echo "  MISSING  libelf (libelf-dev, or LIBELF_PREFIX)"; bad=1; fi
fi
oc=$(cat /proc/sys/vm/overcommit_memory 2>/dev/null || echo 0)
cl=$(awk '/^CommitLimit/{print int($2/1048576)}' /proc/meminfo 2>/dev/null || echo 0)
if [ "$oc" = 1 ] || { [ "$oc" = 0 ] && [ "$cl" -ge 5 ]; }; then echo "  ok       memory reservation (overcommit $oc, commit limit ${cl} GB)"
else echo "  PROBLEM  overcommit $oc, commit limit ${cl} GB: the tracer's 4 GB reservation will fail (sysctl vm.overcommit_memory=1, or more memory/swap)"; bad=1; fi
if [ $RUNTIME = 0 ]; then
  if curl -sfI https://github.com > /dev/null 2>&1; then echo "  ok       network to github.com"; else echo "  PROBLEM  no network to github.com (the clone and FetchContent need it)"; bad=1; fi
fi
echo "disk: $(df -h --output=avail "${OUT:-$HOME}" 2>/dev/null | tail -1 | tr -d ' ') free at ${OUT:-$HOME} (small runs ~2 GB; one 2^29 workload ~47 GB of deliveries, ~0.5 GB more)"
if [ ${#miss[@]} -gt 0 ]; then
  echo "install: sudo apt-get install -y $(printf '%s\n' "${miss[@]}" | sort -u | tr '\n' ' ')"; exit 1
fi
[ $bad = 0 ] && echo "all dependencies present" || exit 1
