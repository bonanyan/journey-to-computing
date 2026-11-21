# Lab 1 · Processing-in-Memory Macro Models

A hands-on Verilator and GTKWave lab for exploring three uses of a memory macro:

- **MEM:** read and write one 32-bit word through the scalar port.
- **DPIM:** apply multiply, AND, OR, or XOR across 32 lanes in parallel.
- **APIM:** evaluate eight four-input dot products and quantize them to six-bit codes.

Start with the [step-by-step tutorial](lab1-tutorial.md), also available as a [styled HTML page](lab1-tutorial.html).

## Run the lab

The root build requires Verilator 5 with timing support, a C++20 compiler with coroutine support, and GNU Make. GTKWave is needed only to view traces.

```sh
make                 # Build and run MEM, DPIM, and APIM
make wave-mem        # Run MEM and open its selected signals in GTKWave
make wave-dpim       # Run DPIM and open its selected signals in GTKWave
make wave-apim       # Run APIM and open its selected signals in GTKWave
make clean           # Remove generated files under build/
```

A successful run prints `PASS MEM`, `PASS DPIM`, and `PASS APIM`. Individual experiments can be rerun headlessly with `make mem`, `make dpim`, and `make apim`.

## File tree

The annotations below describe every file currently under `./`.

```text
.
├── Makefile                         # Main lab build: Verilator compilation, tests, traces, and cleanup
├── README.md                        # Project overview and this file-by-file guide
├── lab1-tutorial.md                 # Primary tutorial source with timing tables and trace exercises
├── lab1-tutorial.html               # Browser-ready tutorial with light styling, PT Sans, and PT Mono
├── .gitignore                       # Excludes the generated top-level build/ directory from Git
├── .DS_Store                        # macOS Finder metadata; not used by the lab
│
├── tb/                              # Self-checking SystemVerilog benches for the root Makefile
│   ├── tb_dpim.sv                   # Shared MEM and DPIM bench; selects MEM with the +mem plusarg
│   └── tb_apim.sv                   # APIM programming, scalar-read, dot-product, and ADC checks
│
├── views/                           # GTKWave view definitions used by the wave-* targets
│   ├── mem.gtkw                     # MEM clock, controls, address, write data, and read data
│   ├── dpim.gtkw                    # DPIM scalar signals plus operands/results for lanes 0 and 1
│   └── apim.gtkw                    # APIM controls, inputs, full column-0 sum, and eight ADC codes
│
└── pislib_verilog-main/             # Bundled PISLIB RTL, reference benches, docs, and old artifacts
    ├── LICENSE                      # Apache License 2.0 for the bundled PISLIB material
    ├── makefile                     # Original Icarus Verilog/vvp flow; separate from the root Makefile
    ├── plt.tcl                      # GTKWave signal-selection script for the original APIM testbench
    ├── wave                         # Prebuilt Icarus vvp DPIM simulation artifact from the original flow
    ├── wave.vcd                     # Prebuilt binary LXT2 waveform from the original flow, despite its suffix
    ├── .gitignore                   # Original PISLIB ignores for Finder metadata and site/public output
    │
    ├── doc/                         # One-page address-map references
    │   ├── DPIM_AddressMap.pdf      # DPIM word addressing, 32 compartments, and lane organization
    │   └── PIMAddressMap.pdf        # APIM byte addressing, input/output sharing, and ADC organization
    │
    └── src/                         # PISLIB RTL and its original reference material
        ├── DPIM.v                   # 4,096 × 32-bit memory with scalar access and 32 compute lanes
        ├── APIM.v                   # Basic_GeMM_CIM: 1,024 × 8-bit memory and eight quantized dot products
        ├── DPIM_tb.v                # Original randomized Icarus Verilog DPIM reference testbench
        ├── APIM_tb.v                # Original randomized APIM reference testbench and full-sum calculation
        └── README.md                # Original PISLIB source-directory notes and Icarus usage instructions
```

> [!IMPORTANT]
> Run the lab from this directory with the **capitalized** root `Makefile`. The lowercase `pislib_verilog-main/makefile` belongs to the original Icarus Verilog workflow and is not used by the root build.

## Which files to edit

| Goal | File or directory |
|---|---|
| Change the tutorial instructions | `lab1-tutorial.md` |
| Change MEM or DPIM stimulus and checks | `tb/tb_dpim.sv` |
| Change APIM stimulus and checks | `tb/tb_apim.sv` |
| Change digital macro behavior | `pislib_verilog-main/src/DPIM.v` |
| Change analog macro behavior | `pislib_verilog-main/src/APIM.v` |
| Change signals initially shown in GTKWave | `views/*.gtkw` |

The APIM RTL includes the lab correction that retains each full dot-product sum in a 14-bit register before selecting bits `[13:8]` as the six-bit ADC output.

## Generated output

Running the root Makefile creates `build/`, which is ignored by Git:

| Generated path | Purpose |
|---|---|
| `build/dpim/` | Verilator C++ build products and the `Vtb_dpim` MEM/DPIM simulator |
| `build/apim/` | Verilator C++ build products and the `Vtb_apim` simulator |
| `build/mem.vcd` | Value Change Dump trace for the scalar-memory experiment |
| `build/dpim.vcd` | Trace for the 32-lane digital compute experiment |
| `build/apim.vcd` | Trace for the analog compute and quantization experiment |
| `build/*.gtkw` | Scope-adjusted GTKWave views generated from `views/` by the `wave-*` targets |

The prebuilt `pislib_verilog-main/wave` and `wave.vcd` files are historical outputs from the bundled Icarus flow. The current lab does not read them; it generates fresh traces under `build/`.
