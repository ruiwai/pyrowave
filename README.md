# PyroWave

PyroWave is an intra-only video codec (practically speaking a still-image codec)
that is optimized for extremely fast GPU encode (< ~0.1 ms encode and decode at 1080p, < ~0.2 ms at 4K).
It is fully implemented in Vulkan compute shaders.

The targeted bit-rates are quite high (~200+ mbit/s) and the intended use case is
local network game streaming over ethernet with absolute minimum latency where bandwidth is less of a concern.

It is integrated as part of my [pyrofling](https://github.com/Themaister/pyrofling) project.

It is similar in scope to my [master thesis from 2014](https://ntnuopen.ntnu.no/ntnu-xmlui/handle/11250/2400689),
except the use case now is game streaming instead of adaptation of raw video to ethernet links.

## Overview

### Colorspace

Currently implemented YCbCr 4:2:0 and 4:4:4.

### Wavelets

The images are transformed with the Discrete Wavelet Transform using CDF 9/7 filter.
This is basically the exact same as JPEG2000.

### Exact rate control

The encoder can target exact maximum bitrate for an encoded image.

### Trivial "entropy" coding

The encoding of coefficients is trivial compared to normal image codecs,
and is responsible for a large increase in bit-rate, especially at higher compression ratios.
This simplicity massively improves encode/decode performance, since it's extremely parallelizable.
It also makes it possible to do a single-pass exact rate control for an entire image.

It should be possible in theory to add a more proper entropy coder to the encoded bit-planes to achieve somewhat
competent compression, but that has not been explored and is out of scope.
High Throughput JPEG2000 is likely a good place to look for that anyway.

### Robustness against packet loss

Being intra-only and encoding 64x64 blocks of coefficients in isolation ensures great recovery against packet loss.
PyroWave has been battled tested over long distance streaming over fiber links.

### Bitstream definition

See [bitstream/bitstream.md](bitstream/bitstream.md)

## Building

PyroWave is intended to be built alongside PyroFling with Granite in the normal case.

### Standalone C API

The upstream C API is version 1.0.0. This fork's Vulkan C API exposes version 101.0.0, with
additional color-metadata entry points and a negotiated HLG bitstream extension.
Consumers that require these extensions must check the runtime version and
entry points before creating a decoder; 0.6.x libraries are not ABI compatible.
The fork's version line is separate from upstream releases. An upstream 1.x
runtime must not be substituted for the fork runtime required by a consumer.
The upstream Metal port remains at 1.0.0 and does not provide these fork-specific
color-metadata/HLG extensions.

A pinned Granite revision is tracked as a PyroWave third-party submodule.
Initialize the repository and its nested dependencies before configuring CMake:

```
git submodule update --init --recursive
```

Build normally with CMake and a C API is installed.

```
$ mkdir build
$ cd build
$ cmake .. -DCMAKE_INSTALL_PREFIX=output -DCMAKE_BUILD_TYPE=Release -G Ninja
$ ninja install

[0/1] Install the project...
-- Install configuration: "Release"
-- Installing: ...../build/output/include/pyrowave/pyrowave.h
-- Installing: ...../build/output/lib/libpyrowave-shared.so.101.0.0
-- Installing: ...../build/output/lib/libpyrowave-shared.so.101
-- Installing: ...../build/output/lib/libpyrowave-shared.so
-- Installing: ...../build/output/share/pyrowave-shared/cmake/pyrowave-sharedConfig.cmake
-- Installing: ...../build/output/share/pyrowave-shared/cmake/pyrowave-sharedConfig-release.cmake
-- Installing: ...../build/output/lib/pkgconfig/pyrowave-shared.pc
```

The build is tested on Linux, MinGW, msys2 and MSVC.
See `pyrowave-c-test` which unit tests the shared C API.
That test also serves as a basic user guide for the API.

`build-steamrt.sh` builds against the Sniper SDK and is also supported.

#### Buffer and color contracts

`pyrowave_encoder_create_native_fp16` (C) / `Encoder::init_native_fp16` (C++)
selects per-encoder R16 wavelets and native half DWT arithmetic, without FP32
fallback or changing decoder precision. Query the enabled device prerequisites
with `pyrowave_device_supports_native_fp16` / `Encoder::supports_native_fp16`;
the C query returns false for NULL. Strict creation returns
`PYROWAVE_ERROR_NOT_IMPLEMENTED` for missing prerequisites before allocation.
Other initialization failures remain errors; creation never retries the legacy API.
Create-info structures, API version and bitstream are unchanged.

Legacy creation still uses the compile-time/default `PYROWAVE_PRECISION` and
its environment override: 0 is half math/storage, 1 is FP32 math with mixed
R16/R32 storage (the build default), and 2 is full FP32. All shader variants
remain available. Strict FP16 retains the half-precision quantization cap of
512 with an initial base of 8 bits; legacy precision 0 keeps its base of 6 bits.
This is not a promise of lossless output. Decoders, fragment
decoding, HDR and 4:4:4 remain on their existing paths. For validation,
`pyrowave-device-validation --roundtrip --native-fp16-encoder` selects only
the encoder; the decoder still follows the default/environment precision.

CPU output precision is selected by `pyrowave_cpu_buffer.format`: planar
8-bit, 10-bit LSB-in-16-bit, or 16-bit UNORM. GPU output precision is selected
by the image/view formats supplied in `pyrowave_gpu_buffers`. The decoder
creation structure does not contain an output-bit-depth field.

Frame context 0 retains the synchronous workflow. Explicitly selecting the
two frame contexts enables pipelined CPU readback/encode; it does not change
the native bitstream or impose pipelining on applications. An asynchronous
readback must be completed before that context is reused. Completion must
provide the same dimensions, format, strides and plane sizes as its start;
data pointers may change.

RGB scaled encode supports full and narrow YCbCr ranges through
`ycbcr_range` and `ycbcr_range_bit_depth`. These parameters describe pixel
quantization; applications must separately signal matching color metadata.
HDR mastering/content-light metadata is presentation data, not the PyroWave
color-metadata structure.

CPU encode/decode selects the scaling factor for each buffer format without
changing the scaling configured for later GPU calls. The fragment decoder
does not support non-default scaling, so 10-bit LSB CPU readback returns
`PYROWAVE_ERROR_NOT_IMPLEMENTED`; 8-bit and 16-bit UNORM readback are supported.

#### Installed static C API

Configure with `-DPYROWAVE_BUILD_SHARED=OFF -DPYROWAVE_BUILD_STATIC_C_API=ON`.
The installed static archive includes the PyroWave and Granite dependency
objects. Its CMake package supplies the C API/Vulkan headers and system link
requirements, without referring to the original source or build directories:

```cmake
find_package(pyrowave-static CONFIG REQUIRED)
target_link_libraries(your_application PRIVATE Pyrowave::pyrowave-c-api-static)
```

#### Android standalone build

This assumes that NDK is installed somewhere.

```
# Replace as needed. Any NDK should work in theory, but this is the one I checked with.
$ NDK_VERSION=29.0.14206865

$ cmake .. -DCMAKE_TOOLCHAIN_FILE=$ANDROID_HOME/ndk/$NDK_VERSION/build/cmake/android.toolchain.cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DANDROID_ABI=arm64-v8a \
    -G Ninja -DCMAKE_INSTALL_PREFIX=output
$ ninja install
```

### CMake subproject

The C API can also be built directly within a parent project's build graph:

```cmake
set(PYROWAVE_BUILD_SHARED OFF)
set(PYROWAVE_BUILD_STATIC_C_API ON)
set(PYROWAVE_BUILD_TESTS OFF)
set(PYROWAVE_INSTALL OFF)
set(GRANITE_INSTALL_TARGETS OFF)
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
add_subdirectory(third-party/pyrowave pyrowave EXCLUDE_FROM_ALL)
target_link_libraries(your_application PRIVATE pyrowave-c-api-static)
```

Initialize the pinned nested submodules before configuration. The static target
provides the C API and Vulkan include paths and carries its PyroWave/Granite
link dependencies; no intermediate source copy or separate build script is
required. C API tests and PyroWave install rules default to enabled in a
standalone build and disabled in a subproject.

### Local development and CLI

For the sample and test applications in this repo, the same pinned Granite
submodule is used. Build with `-DPYROWAVE_DEVEL=ON` to get the "full" build.

#### Basic encoder/decoder CLI

The basic CLI takes a y4m and dumps out a raw bitstream. This is just intended for local testing.

```shell
pyrowave-encode test.y4m out.wave 400000
```

This encodes a raw bitstream where each frame consumes maximum 400 KB.
To decode back to y4m:

```shell
pyrowave-decode out.wave out.y4m
```

## License

This fork contains upstream PyroWave code under the MIT License and
AlkaidLab-maintained additions and changes under GPL-3.0-only. Combined files
are marked `MIT AND GPL-3.0-only` to require both licenses for redistribution;
the expression does not relicense unchanged upstream portions. See
`NOTICE.md` and the complete license texts in `LICENSES/` for the scope and
redistribution terms.

### Experimental Intel quant-only SIMD8 candidate

On Intel (vendor ID 0x8086), quantization requests SIMD8 when supported:
one 8-lane cluster per block, aiming to limit per-subgroup divergence and
register demand, not asserting actual driver occupancy. Otherwise the existing
subgroup-size selection and unsupported-device failure path are unchanged.
Other vendors and stages, shaders, operations and precision are unchanged.
Narrower subgroups may cost SIMD utilization. Performance is unverified;
there is no GPU validation or speedup claim for this candidate.
On a healthy device, compare the existing `pyrowave-bench` before/after with
the same `PYROWAVE_PRECISION=1`, device, input and benchmark configuration.
Its repeated fixed static input is not representative of live changing frames;
check completed timing output, not just the process exit status.


### Experimental quant band batching

Quant dispatches batch the active bands of each component/level in Z (420:
42 to 13 dispatches; 444: 48 to 15). Quantization, distortion calculations,
partial-block masks and the per-cluster atomic allocation scheme are unchanged;
analyze/RDO, DWT and packing are outside this change. No GPU runtime proof,
quality equivalence or performance improvement is claimed.
The embedded header retains all non-quant modules and reflection byte-for-byte:
compile a candidate with `slangmosh --output <candidate> shaders/slangmosh.json
--namespace PyroWave -O --strip`, splice only its quant module, append its quant
reflection to the old reflection bank, and update offsets/only the quant reflection
reference. The artifact-only merge script validates ranges and per-entry hashes.

### Experimental coarse RDO

Each Encoder defaults to coarse RDO; set `PYROWAVE_RDO_FULL_SEARCH=1` before
construction for the original full search. Only the exact value `1` disables it;
the environment is read once per constructor, not per frame.
Coarse mode skips odd quant candidates 1 through 13, retaining even candidates
and the original terminal candidate 15. Selected costs, precision, quantization
and decoding are unchanged; rate/quality choices may differ. No GPU validation,
quality equivalence or performance improvement is claimed.
Known preexisting limitation: extreme `encode_pre_transformed` inputs can overflow
`errors[15]`; this optimization does not fix or validate that unsafe path. The
checked domain is baseline-in-bounds stats (num_planes 0 through 14), not all inputs.
Regeneration replaces only quant/analyze modules and appends their reflections,
preserving the other 23 entries and existing reflection aliases.

### Bitrate-aware one-bit fine-band prequantization

Inactive by default: the caller must explicitly allow each next encode via
`pyrowave_encoder_set_fine_prequantization_allowed` / `Encoder::set_fine_prequantization_allowed`.
Allowance is consumed by the next attempted encode, including failures before profiling.
Sunshine on Windows grants it only for actual borrowed VDD frames after successful
fused direct conversion. CPU, DDX, copied, general-converter and blank frames keep
prior quantization. Allowed frames still require payload budgets <= 2 bits per
actual luma pixel per frame (1080p60: approximately 248.832 Mb/s payload),
for both 4:2:0 and 4:4:4.
Level 0/1 non-LL bands, including chroma where present, use half the existing
precision-capped resolution: twice the initial quantization step, not band zeroing.
LL, levels >= 2 and budgets above the threshold are unaffected.
Set `PYROWAVE_FINE_PREQUANT=0` before Encoder construction to opt out unconditionally; only exact
`0` disables it, and the environment is read once per Encoder. This is independent
of `PYROWAVE_RDO_FULL_SEARCH`: full search does not disable prequantization.
The budget profile resets each frame; explicit nonnegative quant-scale overrides
remain unchanged. Noise normalization, distortion weighting, shaders, decoding
and the existing budget resolver are unchanged; no stronger bitrate guarantee is
introduced. Texture and text can soften; quality loss is not guaranteed small.
No measured performance improvement or GPU/quality validation is claimed.
