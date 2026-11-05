# Sourced by the pipeline's scripts: the pinned sources and the runs. Nothing here is a secret.

# jolt-asic-planning (Tony's): the guests and their inputs, jolt-cpp's tracer (88dca85), WORKER9's delivery_writer.
# fa59d9a = tracer-standalone-main, the tracer moved to jolt-cpp 88dca85; its outputs equal 821e1bd's (WORKER16), which the
# 2^29 comparison (Quest 111, 112) checks against the runner's deliveries, written at 8d546e3 (jolt-cpp 821e1bd).
JAP_URL=https://github.com/Quarky93/jolt-asic-planning.git
JAP_COMMIT=fa59d9a0e336088ee4940bb96a2c622ab362b3ea
JAP_FALLBACK=8d546e3e218d0217b3bf799425895b344e1de03a   # the runner's deliveries' commit (tasks 103, 104), if fa59d9a's differ

# The universal replayer tables: v1.26.1's (frozen), byte-identical to wip/tables/replayer and to what 103/104 used.
TABLES_REL=v1.26.1/tables/replayer
# ConvertDelivery and the acceptance kit: v1.26.1's meshsim (frozen), the RTL's target.
MESHSIM_REL=v1.26.1/meshsim

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
