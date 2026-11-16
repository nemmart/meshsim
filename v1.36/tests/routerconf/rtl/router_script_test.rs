//! Not a test of Snarky One: the recorder of meshsim v1.36's router conformance
//! (meshsim's v1.36/tests/routerconf/README.md says how it is used; it is never
//! committed here). It drives one router, the model of its tile's kind
//! (partition.toml), from a script of arrivals and writes what the RTL does with
//! them, a line a cycle, and it writes each router's routes as a hash.
//!
//! A script (`<name>.script`), cycles counted from 1 after the reset:
//!   # tile <id>              the router's tile id: 0, 7 and 15 with their attach ports,
//!                            64-66 the special tiles', any other a plain compute tile's
//!   # cycles <n>             the cycles to run
//!   <t> <P>:<lo>,<h>,<hi> …  the packets at the router's inputs in cycle t (hex): P is
//!                            I local, W E N S a link input (on a special tile, a DMA
//!                            input), A the attach port's; lo and hi the payload's
//!                            halves, h meshsim's header word: src [6:0], dst [13:7],
//!                            mode [16:14], counter [19:17], size [21:20], address [53:22]
//! A link's packet is valid in its cycle alone. A DMA input's is offered from its
//! cycle, behind the input's earlier packets, until the router takes it (16.4).
//!
//! The observations (`<name>.obs`), a line for each cycle t:
//!   <t> [W:… E:… N:… S:… A:…]  the packet in each output's register in cycle t
//!       [J:…[*]]               the eject's, * its loopback mark
//!       [V]                    `overflow` is 1 in cycle t
//!       [!W !E !N !S]          a DMA input offering in cycle t and not ready

use crate::router::Router;
use crate::router_attach_master::RouterAttachMaster;
use crate::router_attach_memory::RouterAttachMemory;
use crate::router_attach_tracer::RouterAttachTracer;
use crate::router_master::RouterMaster;
use crate::router_memory::RouterMemory;
use crate::router_tracer::RouterTracer;
use std::collections::VecDeque;
use std::fmt::Write as _;
use verif::Wide;
use verif::packet::{Format, Packet};

const FORMAT: Format = Format { tile_id_bits: 7 };
const WORDS: usize = FORMAT.words();
const PORT_NAMES: [char; 6] = ['I', 'W', 'E', 'N', 'S', 'A'];
const NONE: u32 = 127;

enum Rtl {
    Plain(Router),
    AttachMaster(RouterAttachMaster),
    AttachMemory(RouterAttachMemory),
    AttachTracer(RouterAttachTracer),
    Master(RouterMaster),
    Memory(RouterMemory),
    Tracer(RouterTracer),
}

macro_rules! with_router {
    ($rtl:expr, $router:ident => $body:expr) => {
        match $rtl {
            Rtl::Plain($router) => $body,
            Rtl::AttachMaster($router) => $body,
            Rtl::AttachMemory($router) => $body,
            Rtl::AttachTracer($router) => $body,
            Rtl::Master($router) => $body,
            Rtl::Memory($router) => $body,
            Rtl::Tracer($router) => $body,
        }
    };
}

/// What a cycle's outputs are: each output's register (I the eject), the eject's
/// loopback mark, the overflow pulse, each link input's ready.
struct Outputs {
    registers: [Option<Packet>; 6],
    loopback: bool,
    overflow: bool,
    ready: [bool; 6],
}

struct Bench {
    rtl: Rtl,
    tile: u32,
    /// The attach port in use: the tile's own, or the special tile's attached here.
    attach: usize,
    /// The link inputs that carry DMAs (bit 0 west).
    dma_inputs: u32,
}

impl Bench {
    fn new(tile: u32) -> Self {
        let (rtl, attach, dma_inputs) = match tile {
            0 => (Rtl::AttachMaster(RouterAttachMaster::new()), 0, 0),
            7 => (Rtl::AttachMemory(RouterAttachMemory::new()), 1, 0),
            15 => (Rtl::AttachTracer(RouterAttachTracer::new()), 2, 0),
            64 => (Rtl::Master(RouterMaster::new()), 0, crate::router_master::params::DMA_INPUTS),
            65 => (Rtl::Memory(RouterMemory::new()), 1, crate::router_memory::params::DMA_INPUTS),
            66 => (Rtl::Tracer(RouterTracer::new()), 2, crate::router_tracer::params::DMA_INPUTS),
            _ => (Rtl::Plain(Router::new()), 0, 0),
        };
        let mut bench = Bench { rtl, tile, attach, dma_inputs };
        for _ in 0..3 {
            bench.settle(true, &[None; 6]);
            bench.edge();
        }
        bench
    }

    fn is_dma(&self, port: usize) -> bool {
        (1..=4).contains(&port) && self.dma_inputs >> (port - 1) & 1 != 0
    }

    fn settle(&mut self, reset: bool, inputs: &[Option<Packet>; 6]) -> Outputs {
        let tile = self.tile;
        let attach = self.attach;
        let idle = Packet { src: NONE, ..Packet::default() };
        let encode = |port: usize| -> Wide<WORDS> { FORMAT.encode(&inputs[port].unwrap_or(idle)) };
        let valid = |port: usize| inputs[port].is_some();
        with_router!(&mut self.rtl, router => {
            router.set_rst_n(!reset);
            router.set_tile_id(verif::bits(tile));
            router.set_local_valid(valid(0));
            router.set_local_packet(encode(0));
            router.set_west_in_valid(valid(1));
            router.set_west_in_packet(encode(1));
            router.set_east_in_valid(valid(2));
            router.set_east_in_packet(encode(2));
            router.set_north_in_valid(valid(3));
            router.set_north_in_packet(encode(3));
            router.set_south_in_valid(valid(4));
            router.set_south_in_packet(encode(4));
            for port in 0..3 {
                router.set_attach_in_packet(port, if port == attach { encode(5) } else { FORMAT.encode(&idle) });
            }
            router.set_attach_in_valid(u8::from(valid(5)) << attach);
            router.eval();
            let take = |valid: bool, value: Wide<WORDS>| valid.then(|| FORMAT.decode(&value));
            Outputs {
                registers: [
                    take(router.eject_valid(), router.eject_packet()),
                    take(router.west_out_valid(), router.west_out_packet()),
                    take(router.east_out_valid(), router.east_out_packet()),
                    take(router.north_out_valid(), router.north_out_packet()),
                    take(router.south_out_valid(), router.south_out_packet()),
                    take(router.attach_out_valid() >> attach & 1 != 0, router.attach_out_packet(attach)),
                ],
                loopback: router.eject_loopback(),
                overflow: router.overflow(),
                ready: [
                    true,
                    router.west_in_ready(),
                    router.east_in_ready(),
                    router.north_in_ready(),
                    router.south_in_ready(),
                    true,
                ],
            }
        })
    }

    fn edge(&mut self) {
        with_router!(&mut self.rtl, router => verif::edge(router));
    }
}

fn packet_of(lo: u64, h: u64, hi: u64) -> Packet {
    Packet {
        src: (h & 0x7f) as u32,
        dst: (h >> 7 & 0x7f) as u32,
        mode: (h >> 14 & 7) as u32,
        counter: (h >> 17 & 7) as u32,
        size: (h >> 20 & 3) as u32,
        address: (h >> 22 & 0xffff_ffff) as u32,
        payload: u128::from(lo) | u128::from(hi) << 64,
    }
}

fn words_of(packet: &Packet) -> (u64, u64, u64) {
    let h = u64::from(packet.src)
        | u64::from(packet.dst) << 7
        | u64::from(packet.mode) << 14
        | u64::from(packet.counter) << 17
        | u64::from(packet.size) << 20
        | u64::from(packet.address) << 22;
    (packet.payload as u64, h, (packet.payload >> 64) as u64)
}

/// A script's events: (cycle, port, packet), in cycle order.
type Events = Vec<(u64, usize, Packet)>;

/// Runs `events` on tile's router for `cycles`; `line` gets each cycle's outputs
/// and the DMA inputs that offered and were not ready.
fn run(tile: u32, cycles: u64, events: &Events, mut line: impl FnMut(u64, &Outputs, &[bool; 6])) {
    let mut bench = Bench::new(tile);
    let mut queues: [VecDeque<Packet>; 6] = Default::default();
    let mut next = 0;
    for cycle in 1..=cycles {
        let mut inputs = [None; 6];
        while next < events.len() && events[next].0 == cycle {
            let (_, port, packet) = events[next];
            if bench.is_dma(port) {
                queues[port].push_back(packet);
            } else {
                assert!(inputs[port].is_none(), "cycle {cycle}: two packets at input {port}");
                inputs[port] = Some(packet);
            }
            next += 1;
        }
        for port in 1..=4 {
            if bench.is_dma(port) {
                inputs[port] = queues[port].front().copied();
            }
        }
        let outputs = bench.settle(false, &inputs);
        let mut held = [false; 6];
        for port in 1..=4 {
            if bench.is_dma(port) && inputs[port].is_some() {
                if outputs.ready[port] {
                    queues[port].pop_front();
                } else {
                    held[port] = true;
                }
            } else {
                assert!(outputs.ready[port], "cycle {cycle}: input {port} is no DMA's and not ready");
            }
        }
        line(cycle, &outputs, &held);
        bench.edge();
    }
    assert_eq!(next, events.len(), "events after the script's last cycle");
}

fn parse(text: &str) -> (u32, u64, Events) {
    let (mut tile, mut cycles, mut events) = (None, None, Vec::new());
    for line in text.lines() {
        if let Some(rest) = line.strip_prefix("# tile ") {
            tile = Some(rest.trim().parse().unwrap());
        } else if let Some(rest) = line.strip_prefix("# cycles ") {
            cycles = Some(rest.trim().parse().unwrap());
        } else if !line.starts_with('#') && !line.is_empty() {
            let mut fields = line.split(' ');
            let cycle: u64 = fields.next().unwrap().parse().unwrap();
            for field in fields {
                let (port, words) = field.split_once(':').unwrap();
                let port = PORT_NAMES.iter().position(|&name| name == port.chars().next().unwrap()).unwrap();
                let words: Vec<u64> = words.split(',').map(|word| u64::from_str_radix(word, 16).unwrap()).collect();
                events.push((cycle, port, packet_of(words[0], words[1], words[2])));
            }
        }
    }
    (tile.unwrap(), cycles.unwrap(), events)
}

/// meshsim's `ROUTER_SCRIPTS=DIR`: each DIR/<name>.script's observations, to
/// DIR/<name>.obs.
#[test]
#[ignore = "the recorder of meshsim's router conformance: ROUTER_SCRIPTS=DIR"]
fn router_scripts() {
    let directory = std::env::var("ROUTER_SCRIPTS").expect("ROUTER_SCRIPTS=DIR");
    let mut names: Vec<_> = std::fs::read_dir(&directory)
        .unwrap()
        .map(|entry| entry.unwrap().path())
        .filter(|path| path.extension().is_some_and(|extension| extension == "script"))
        .collect();
    names.sort();
    for path in names {
        let (tile, cycles, events) = parse(&std::fs::read_to_string(&path).unwrap());
        let mut text = format!("# routerconf observations: the RTL's router (Snarky One, rtl/blocks/router)\n# tile {tile}\n");
        run(tile, cycles, &events, |cycle, outputs, held| {
            write!(text, "{cycle}").unwrap();
            for port in [1, 2, 3, 4, 5, 0] {
                if let Some(packet) = &outputs.registers[port] {
                    let (lo, h, hi) = words_of(packet);
                    let name = if port == 0 { 'J' } else { PORT_NAMES[port] };
                    write!(text, " {name}:{lo:x},{h:x},{hi:x}").unwrap();
                    if port == 0 && outputs.loopback {
                        text.push('*');
                    }
                }
            }
            if outputs.overflow {
                text.push_str(" V");
            }
            for port in 1..=4 {
                if held[port] {
                    write!(text, " !{}", PORT_NAMES[port]).unwrap();
                }
            }
            text.push('\n');
        });
        std::fs::write(path.with_extension("obs"), text).unwrap();
        println!("{}: tile {tile}, {cycles} cycles, {} packets", path.display(), events.len());
    }
}

/// meshsim's `ROUTER_TABLES=FILE`: each router's routes, as a hash. For tile 0 to
/// 66, each input the router has (local, the four links or a special tile's DMA
/// inputs, its attach port), in that order, each src 0 to 66 and each dst 0 to 127:
/// one packet a cycle at that input, and the outputs that hold it two cycles
/// later, a bit each (the eject 1, west 2, east 4, north 8, south 16, the attach
/// port 32), folded into an FNV-1a hash of 64 bits.
#[test]
#[ignore = "the recorder of meshsim's router conformance: ROUTER_TABLES=FILE"]
fn router_tables() {
    let file = std::env::var("ROUTER_TABLES").expect("ROUTER_TABLES=FILE");
    let mut text = String::from("# the routers' routes, hashed: the RTL's router (Snarky One, rtl/blocks/router)\n");
    for tile in 0..67u32 {
        let dma_inputs = Bench::new(tile).dma_inputs;
        let mut inputs = vec![0];
        for port in 1..=4 {
            if tile < 64 || dma_inputs >> (port - 1) & 1 != 0 {
                inputs.push(port);
            }
        }
        if matches!(tile, 0 | 7 | 15 | 64..) {
            inputs.push(5);
        }
        let mut events = Events::new();
        let mut cycle = 1;
        for &port in &inputs {
            for src in 0..67 {
                for dst in 0..128 {
                    let serial = cycle;
                    events.push((cycle, port, Packet { src, dst, size: 2, payload: u128::from(serial), ..Packet::default() }));
                    cycle += 1;
                }
            }
        }
        let cycles = cycle + 2;
        let mut masks = vec![0u8; cycles as usize + 1];
        run(tile, cycles, &events, |cycle, outputs, held| {
            assert!(!outputs.overflow && held.iter().all(|&held| !held), "tile {tile}, cycle {cycle}: a loaded router");
            for port in 0..6 {
                if let Some(packet) = &outputs.registers[port] {
                    assert_eq!(packet.payload, u128::from(cycle - 2), "tile {tile}, cycle {cycle}: output {port}");
                    masks[cycle as usize - 2] |= 1 << port;
                }
            }
        });
        let mut hash = 0xcbf2_9ce4_8422_2325u64;
        for &(cycle, _, _) in &events {
            hash = (hash ^ u64::from(masks[cycle as usize])).wrapping_mul(0x0100_0000_01b3);
        }
        writeln!(text, "{tile} {hash:016x}").unwrap();
    }
    std::fs::write(file, text).unwrap();
}
