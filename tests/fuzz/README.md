# Fuzz harnesses

libFuzzer harnesses for firmware code that parses untrusted input. They run on the host, built
with the unit-test tree, so they share its stubs and mocks.

| Harness | Code under test | Untrusted source |
| --- | --- | --- |
| `url_decode` | `http::url_decode` | PrusaLink request paths (unauthenticated LAN) |
| `meatpack` | `MeatPack::handle_rx_char` | `.bgcode` G-code blocks, serial |
| `bgcode_reader` | `AnyGcodeFormatReader` (plain/binary G-code, heatshrink, MeatPack, metadata, thumbnails, encrypted files) | USB stick, PrusaLink upload, Connect download |
| `connect_command` | `connect_client::Command::parse_json_command` / `gcode_command` | Connect server (or a MITM when TLS is off) |
| `e2ee_identity` | `e2ee::read_and_verify_identity_block` | encrypted `.bgcode` |
| `gcode_parser` | `GCodeParser2` (options, quoted strings, number conversion) | every executed G-code line |
| `marlin_parser` | Marlin `GCodeParser` (`lib/Marlin/Marlin/src/gcode/parser.cpp`) and the value accessors of the handlers | every executed G-code line |
| `lwip_tcp_input` | lwIP `tcp_input` through `ip4_input` (a fixed 20-byte IPv4 header), with the test `lwipopts.h` of `ooseq_tcp_tests` (the Buddy TCP options with an 8 KB window, without the fork's runtime window) and `lib/WUI/ooseq_limit.cpp`. An lwIP assert or a leaked pcb, segment or pbuf is a crash | TCP packets from any LAN host (PrusaLink, port 80) |
| `lwip_eth_input` | lwIP receive path from the Ethernet frame: `ethernet_input`, ARP, IPv4 with reassembly, ICMP, IGMP, UDP, the DHCP client with the address conflict detection, and the mDNS responder of `lib/WUI/mdns`, with the options of `lwip_eth/buddy/lwipopts.h` (TCP off, no checksum checks). A static address or DHCP. The pools come from malloc; `lwip_eth_input_pools` has the heap pools and the timeout count of the firmware instead. An lwIP assert or an ASan finding is a crash | Ethernet and Wi-Fi frames from any LAN host |
| `motion_klipper` | `PreciseStepping` with the input shaper and pressure advance step generators, compared with Klipper's step generation | G-code moves and M593/M572 parameters (an oracle test, not a parser) |

`corpus/<name>/` holds seed inputs. ctest replays them in both build flavours, so they double as
regression tests. Add the reproducer of every fixed bug here.

## Build flavours

- **Default** (any compiler, `BUDDY_FUZZ=OFF`): each harness links `replay_main.cpp`, and
  `ctest -L fuzz` runs it over its corpus.
- **libFuzzer** (Clang, `BUDDY_FUZZ=ON`): instrument the whole tree and link the engine.
  `$LIB_FUZZING_ENGINE` (OSS-Fuzz / ClusterFuzzLite) replaces `-fsanitize=fuzzer` when set.

```sh
san="-fsanitize=fuzzer-no-link,address,undefined -fno-omit-frame-pointer -g"
cmake -S . -B build-fuzz -G Ninja -DBOARD=BUDDY -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_C_FLAGS="$san" -DCMAKE_CXX_FLAGS="$san" -DBUDDY_FUZZ=ON
cmake --build build-fuzz --target fuzz_bgcode_reader
cd build-fuzz/tests/fuzz   # bgcode_reader loads the test printer key from the working directory
mkdir -p /tmp/work && UBSAN_OPTIONS=halt_on_error=1 ./fuzz_bgcode_reader /tmp/work ../../../tests/fuzz/corpus/bgcode_reader
```

Run fuzzers with `UBSAN_OPTIONS=halt_on_error=1` (ctest sets it), otherwise UBSan findings are
printed but not saved as crashes.

`CMAKE_BUILD_TYPE=Release` defines `NDEBUG`, as on the printer, so bounds checks that exist only
as `assert` are off, exactly as on the device.

## Adding a harness

- If a unit-test target already builds the code, use
  `buddy_add_fuzzer_from_test(<name> TEST <test target> DROP <its Catch2 .cpp files> SOURCES <harness.cpp>)`.
  It copies that target's sources, include paths, definitions, libraries and generator
  dependencies.
- Otherwise use `buddy_add_fuzzer(<name> SOURCES … LIBS … INCLUDES …)`.
- Harnesses must be deterministic, keep no state between inputs, and use no network.
