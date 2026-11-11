# Sourced by the pipeline's scripts: the pinned sources and the runs. Nothing here is a secret.

# jolt-asic-planning (Tony's): the guests and their inputs, jolt-cpp's tracer (88dca85), WORKER9's delivery_writer.
# fa59d9a = tracer-standalone-main, the tracer moved to jolt-cpp 88dca85; its outputs equal 821e1bd's (WORKER16), which the
# 2^29 comparison (Quest 111, 112) checks against the runner's deliveries, written at 8d546e3 (jolt-cpp 821e1bd).
JAP_URL=https://github.com/Quarky93/jolt-asic-planning.git
JAP_COMMIT=fa59d9a0e336088ee4940bb96a2c622ab362b3ea
JAP_FALLBACK=8d546e3e218d0217b3bf799425895b344e1de03a   # the runner's deliveries' commit (tasks 103, 104), if fa59d9a's differ

# v1.35 (W19, D42): the universal replayer tables are v1.35's data/tables/replayer (byte-identical to v1.26.1's, to
# wip/tables/replayer and to what 103/104 used), and ConvertDelivery is v1.35's C++ convert_delivery (v1.3's, which writes
# the manifest in one of v1.26.1's two key orders): build.sh's TABLES and CONVERT_DELIVERY. v1.3 read v1.26.1/tables and
# v1.26.1/meshsim from the repository.

# gold and jumpmap, pinned by meshsim commit (wip/ moves on; these are built from git archive, not the working tree):
#   GOLD_SRC      gold 7ef9039 and jumpmap 4788e0e: the v1.2 segment file and jumpmap's outputs exactly as task 109 made
#                 them (the runner's copies)
#   GOLD_V13_SRC  gold eeb64b5 (WORKER18's v1.3 accepted, byte-identical to WORKER16's at 2^29): the v1.3 segment file
#                 (frames 7, 1, 6, then per segment the 68-word frame 2, 3, 4/5), built with GOLD_FORMAT=v1.3
GOLD_SRC=d97ecdebfe8a8fc5f7f53ac5cc9d517454f9dfb9
GOLD_V13_SRC=eeb64b5c10efc50965f255d208fa5d6715a42dfe

# run_info RUN: sets PD (program dir), IN (input dir), both relative to the tracer directory of jolt-asic-planning, and
# SEL (the selected windows, 2^29 only, as 103/104). The paths are kept relative because delivery_writer, gold and
# jumpmap write them into their manifests (program_dir, input_dir; program, input): relative, a build's files are the
# same on every machine.
run_info() {
  SEL=""
  case $1 in
    evm-zone-23) PD=evm-zone-4e3451c1d8c5; IN=$PD/inputs/trace-23-812ded164e49 ;;
    cyclone-22)  PD=cyclone; IN=cyclone/input/22/input-default ;;
    evm-zone-29) PD=evm-zone-4e3451c1d8c5; IN=$PD/inputs/trace-29-4599adc50712; SEL=0,1,2,7,12,21,22,34,35,5414,5415,8191 ;;
    cyclone-29)  PD=cyclone; IN=cyclone/input/29/input-15961266; SEL=0,1,2,4,964,965,988,1295,4803,5222,5669,5670,8191 ;;
    *) echo "unknown run '$1' (evm-zone-23, cyclone-22, evm-zone-29, cyclone-29)" >&2; return 1 ;;
  esac
}

# run_name WORKLOAD SIZE -> RUN
run_name() {
  case $1:$2 in
    evm-zone:small) echo evm-zone-23 ;;  cyclone:small) echo cyclone-22 ;;
    evm-zone:29)    echo evm-zone-29 ;;  cyclone:29)    echo cyclone-29 ;;
    *) echo "workload evm-zone|cyclone, size small|29 (got '$1' '$2')" >&2; return 1 ;;
  esac
}
