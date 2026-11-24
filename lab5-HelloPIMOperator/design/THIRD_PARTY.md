# Third-party sources

The Stage 1 Verilator build uses the local PISLIB macro. DarkRISCV and Tiny TPU are preserved for reference in this directory so the original source folders can be removed. The MPU controller, C operator and simulation harnesses are Lab 4 sources.

| Component | Origin and files | License information |
|---|---|---|
| PISLIB ACIM | The corrected Lab 1 snapshot of `pislib_verilog-main/src/APIM.v`, including the existing 14-bit sum correction. Copied byte-for-byte. | Apache License 2.0, copyright 2022 Bonan Yan. [Source](third_party/pislib/APIM.v) · [License](third_party/pislib/LICENSE) |
| DarkRISCV | The supplied `darkriscv-master/rtl` versions of `darkriscv.v`, `config.vh` and the optional `darkmac.v`. These are the CPU core, configuration and MAC for the proposed VPU. | BSD 3-Clause, copyright 2018 Marcelo Samsoniuk. Original notices and source headers are retained. [Upstream project](https://github.com/darklife/darkriscv) · [License](third_party/darkriscv/LICENSE) |
| Tiny TPU | A local reference copy of the supplied `tiny-tpu-main/src` RTL. Its unified buffer, matrix engine and VPU organization informed the architecture. The Stage 1 build compiles independently of this reference. | The supplied snapshot contains no project license file or license grant in the copied RTL. Redistribution permission for these reference files is unverified. The PISLIB and DarkRISCV licenses apply only to their respective components. [Reference RTL](third_party/tiny-tpu/reference/) |

The supplied snapshots have no per-project Git revision metadata. The Stage 1 APIM source has SHA-256 `5f300e20fb46ad08937450b87aeeb332494d7ebc5e02a51c8ef8c9f18db83cf9`.
