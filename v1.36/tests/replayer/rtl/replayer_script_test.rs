//! SCRATCH (meshsim v1.36's reference; never committed here): replays meshsim's replayconf scripts
//! (external/meshsim v1.36/tests/replayconf.h) on replayer_unit and writes its observations.
//! `REPLAY_SCRIPTS=<dir>`: for every `<name>.script` there, `<name>.obs`.

use crate::replayer_unit::ReplayerUnit;

const WORD_STATUS: u8 = 1;
const WORD_ERRORS: u8 = 2;
const WORD_EVENTS: u8 = 3;
const WORD_EVENTS_PEEK: u8 = 4;
const WORD_EVENTS_MASK: u8 = 5;
const WORD_START: u8 = 6;

fn mix(mut z: u64) -> u64 {
    z = (z ^ (z >> 30)).wrapping_mul(0xbf58476d1ce4e5b9);
    z = (z ^ (z >> 27)).wrapping_mul(0x94d049bb133111eb);
    z ^ (z >> 31)
}

fn quiet(dut: &mut ReplayerUnit) {
    dut.set_control_reg_valid(false);
    dut.set_pass_reg_valid(false);
    dut.set_reg_write(false);
    dut.set_noc_write_valid(false);
}

/// A word's value in this cycle, with no access presented: rdata is combinational
/// from the registers whatever valid is.
fn peek(dut: &mut ReplayerUnit, control: bool, word: u8) -> u32 {
    dut.set_reg_word(word);
    dut.eval();
    if control { dut.control_reg_rdata() } else { dut.pass_reg_rdata() }
}

fn run(script: &str) -> String {
    let mut dut = ReplayerUnit::new();
    // The reset, then EVENTS_MASK every event.
    dut.set_rst_n(false);
    quiet(&mut dut);
    dut.set_reg_word(0);
    dut.set_reg_wdata(0);
    dut.set_noc_write_address(0);
    dut.set_noc_write_size(2);
    dut.set_noc_write_data(0);
    dut.set_replay_row_ready(true);
    verif::cycles(&mut dut, 2);
    dut.set_rst_n(true);
    dut.set_control_reg_valid(true);
    dut.set_reg_write(true);
    dut.set_reg_word(WORD_EVENTS_MASK);
    dut.set_reg_wdata(0x7f);
    verif::tick(&mut dut);
    quiet(&mut dut);

    let names = ["busy", "err", "ev", "w32", "w33", "w34", "w35"];
    let mut last = [0u64; 7];
    let mut first = true;
    let mut rows = 0u64;
    let mut cycles = 0u64;
    let mut obs = String::from("# replayconf observations v2\n");
    for line in script.lines() {
        if line.is_empty() || line.starts_with('#') {
            continue;
        }
        let mut parts = line.split(' ');
        let t: u64 = parts.next().unwrap().parse().unwrap();
        assert_eq!(t, cycles, "cycle out of order");
        cycles += 1;
        // The observations: each word as a load presented in this cycle reads it.
        quiet(&mut dut);
        let values = [
            u64::from(peek(&mut dut, true, WORD_STATUS) & 1),
            u64::from(peek(&mut dut, true, WORD_ERRORS)),
            u64::from(peek(&mut dut, true, WORD_EVENTS_PEEK)),
            u64::from(peek(&mut dut, false, 0)),
            u64::from(peek(&mut dut, false, 1)),
            u64::from(peek(&mut dut, false, 2)),
            u64::from(peek(&mut dut, false, 3)),
        ];
        // The inputs.
        let mut access = false;
        for op in parts.filter(|op| !op.is_empty()) {
            let (kind, rest) = op.split_at(1);
            match kind {
                "W" => {
                    let fields: Vec<&str> = rest.split(',').collect();
                    let offset = u32::from_str_radix(fields[0], 16).unwrap();
                    let lo = u64::from_str_radix(fields[1], 16).unwrap();
                    let hi = u64::from_str_radix(fields[2], 16).unwrap();
                    dut.set_noc_write_valid(true);
                    dut.set_noc_write_address(0x0400_0000 | offset);
                    dut.set_noc_write_data(u128::from(hi) << 64 | u128::from(lo));
                }
                "S" | "C" | "K" => {
                    assert!(!access, "two accesses in cycle {t}");
                    access = true;
                    dut.set_control_reg_valid(kind != "C");
                    dut.set_pass_reg_valid(kind == "C");
                    dut.set_reg_write(kind != "K");
                    dut.set_reg_word(match kind {
                        "S" => WORD_START,
                        "C" => 0,
                        _ => WORD_EVENTS,
                    });
                    dut.set_reg_wdata(if kind == "S" { 1 } else if kind == "C" { u32::from_str_radix(rest, 16).unwrap() } else { 0 });
                }
                _ => panic!("op {op} in cycle {t}"),
            }
        }
        dut.eval();
        let mut text = String::new();
        for (at, name) in names.iter().enumerate() {
            if first || values[at] != last[at] {
                text += &format!(" {name}={:x}", values[at]);
                last[at] = values[at];
            }
        }
        if dut.replay_row_valid() {
            let row: [u32; 20] = dut.replay_row().0;
            let mut h = 0u64;
            for word in 0..10 {
                h = mix(h ^ (u64::from(row[2 * word]) | u64::from(row[2 * word + 1]) << 32));
            }
            text += &format!(" row={h:x}");
            rows += 1;
        }
        if !text.is_empty() {
            obs += &format!("{t}{text}\n");
        }
        first = false;
        verif::edge(&mut dut);
    }
    obs + &format!("# end {cycles} cycles, {rows} rows\n")
}

#[test]
#[ignore = "meshsim's reference: REPLAY_SCRIPTS names the scripts' directory"]
fn replay_scripts() {
    let directory = std::env::var("REPLAY_SCRIPTS").expect("REPLAY_SCRIPTS");
    let mut done = 0;
    for entry in std::fs::read_dir(&directory).unwrap() {
        let path = entry.unwrap().path();
        if path.extension().and_then(|e| e.to_str()) != Some("script") {
            continue;
        }
        let script = std::fs::read_to_string(&path).unwrap();
        std::fs::write(path.with_extension("obs"), run(&script)).unwrap();
        done += 1;
    }
    println!("{done} scripts replayed on replayer_unit");
}
