# Lab 3 Part 4 — integration handoff for the receiving developer/GPT

This folder contains Shreyas's Part 4 work: a llama.cpp hook, HAL, Linux kernel
driver, accelerator mocks, test tools, and recorded test results. The next task is
to integrate this existing implementation with the team's Part 3 SystemC
accelerator in the tutorial's ARM/QEMU + Linux environment.

**The full TinyStories → HAL → actual kernel driver → kernel accelerator mock
flow has passed. The real SystemC accelerator connection has not yet been
validated here.** Do not interpret the mock results as a completed hardware test.

All commands below start from the root of this copied folder unless explicitly
marked **guest**. The original Mac path is irrelevant on the receiving machine.
Keep the relative directory structure intact.

## 1. Read these files first

1. `part4/DRIVER_HANDOFF.md`: current driver, build, ioctl, smoke-test, and DT details.
2. `part4/PART4_HANDOFF.md`: teammate's supplied Part 3 handoff, revision **(4)**.
3. `part4/results/handoff4_validation.json`: concise record of completed/pending checks.
4. `part4/RESULTS.md` and `part4/DRIVER_MOCK.md`: detailed validation and mock limitations.
5. `part4/source_manifest.json`: pinned llama.cpp version and model provenance.

Some older logs/documentation describe earlier timing or bring-up choices. Use the
revision (4) settings below and `DRIVER_HANDOFF.md` for current integration.
`part4/include/dot_accel_regs.h` is the shared source of register/address constants.
Compare it with the teammate's current header before making interface changes.

## 2. What runs where

```text
Build host: compile the application, smoke tool, and kernel module

ARM CPU emulated by QEMU:
  guest Linux user space: llama-completion → HAL → ioctl
  guest Linux kernel:    dot_accel_drv → MMIO writes/reads, wait for completion
                                              │
SystemC process on host:               modeled accelerator + scratchpads
                                              │
                         result / optional interrupt back to guest
```

With `mock=1`, the kernel driver substitutes a kernel backend for the accelerator.
With `mock=1 mock_arithmetic=1`, the whole mock calculation runs in kernel space
using integer-only SoftFloat. There is **no user-space arithmetic worker**.

## 3. Important files

| Location | Purpose |
|---|---|
| `part4/driver/dot_accel_drv.c` | Actual driver, including real MMIO/IRQ path |
| `part4/driver/Makefile` | Kernel module build; output `dot_accel_drv.ko` |
| `part4/driver/dot_accel_main.c` | Composite Kbuild entry; includes main implementation |
| `part4/driver/dot_accel_mock.h`, `dot_softfloat.*`, `softfloat/` | Optional kernel mock and vendored arithmetic dependency/license |
| `part4/include/dot_accel_ioctl.h` | Shared application/kernel request ABI |
| `part4/include/dot_accel_regs.h` | Shared hardware register map |
| `part4/include/dot_accel_timing.h` | Current design_7 expected timing for tests |
| `part4/hal/dot_hal.c`, `dot_hal.h` | Pack Q8_0 inputs, submit ioctl, recover float result |
| `part4/integration/dot-ggml.c`, `dot-ggml.h` | llama.cpp wrapper, backend selection, checks, counters |
| `part4/integration/llama-cpu.patch`, `ggml.cmake` | Minimal upstream integration/build hook |
| `third_party/llama.cpp/ggml/src/ggml-cpu/ggml-cpu.c` | Actual Q8_0 dispatch hook already applied |
| `part4/tests/dot_smoke.c` | Real device-path command-line test |
| `part4/scripts/prepare_device_tree.py` | Merge accelerator node into a copy of actual boot DTB |
| `part4/scripts/build_llama.py` | Build the application with the HAL enabled |
| `part4/scripts/validate_llama.py` | CPU vs selected backend inference comparison |
| `part4/tutorial_driver/` | Original tutorial driver reference; **not** the active driver |
| `models/tinystories-gpt-0.1-3m.Q8_0.gguf` | Test network; provenance in `models/README.md` |
| `part4/results/` | Recorded logs and summaries, not fresh results on your platform |

Copy the entire driver and include trees, including SoftFloat sources. Copying
only `dot_accel_drv.c` and its Makefile is insufficient for this composite module.

## 4. Interface already implemented

The driver copies all four input arrays from user memory into kernel buffers,
loads the accelerator scratchpads, writes NB and START, waits, reads RESULT and
CYCLES, and returns them through one blocking `DOT_IOC_RUN` ioctl. The HAL does
not separately write START. The driver serializes calls from multiple threads.

| Setting | Current value |
|---|---|
| Device | `/dev/dot_accel` |
| Registers | `0xA0100000`, resource size `0x100` |
| Scratchpads | `0xA0110000`, resource size `0x6000` |
| Overall SystemC bus mapping | Base `0xA0100000`, size `0x20000` |
| XQ / YQ / XD / YD offsets within memory resource | `0x0000 / 0x2000 / 0x4000 / 0x5000` |
| NB | Number of 32-element blocks, supported range 0..152 |
| Input layout | Separate int8 XQ/YQ and raw FP16 XD/YD arrays |
| Result | Raw FP32 bits from RESULT, converted with `memcpy` |
| ID register | Offset `0x1C`, expected `0xD07A0001` |
| Current design_7 compute cycles | `nb ? 18 + 4*nb : 24` |
| Interrupt wiring | PL2PS[5], GIC ID 126, SPI 94, level high |
| DT compatible | `ece382m,dotaccel` |

For NB=N, each quant array has 32*N bytes and each scale array has N uint16
entries. llama.cpp's 34-byte interleaved Q8_0 blocks are de-interleaved by the HAL.
The driver handles aligned/padded scale writes. Oversized/unsupported calls are
handled by the integration's CPU fallback; the HAL must not rely on hardware
clamping to 152 blocks.

The ioctl request is `struct dot_run` (56 bytes): NB, zero flags, four 64-bit
user-space pointers, returned result_bits/cycles/status, and zero reserved field.
The pointers are user virtual addresses, **not physical addresses**. See the shared
header and `DRIVER_HANDOFF.md` for a complete C example.

Implemented tutorial-driver fixes include correct CLEAR acknowledgement at CTRL,
correct scratchpad offsets, proper I/O accessors, and new resources. This driver
uses a single transaction ioctl rather than retaining tutorial `/dev/fpga`
read/write and register-ioctl semantics. Do not use the old example application's
ABI against `/dev/dot_accel`.

The ISR clears CTRL, records completion, and wakes the waiter. CLEAR also clears
DONE, so the waiting IRQ path uses completion state rather than expecting DONE to
remain set. RESULT remains readable after CLEAR. There is no hardware abort bit;
do not blindly retry a timed-out operation while the accelerator is still busy.

## 5. Completed validation and its limits

| Test | Status |
|---|---|
| Standalone HAL packing, errors, golden vectors, supported lengths | Passed locally |
| Integer-only mock arithmetic, all FP16 encodings and randomized dots | Passed locally |
| Actual ARM Linux driver: golden cases, concurrent calls, fault/lifecycle tests | Passed in local ARM Linux VM |
| Full TinyStories inference through actual driver and kernel arithmetic mock | Passed, polling and software notification modes |
| Updated revision (4) settings and design_7 smoke-test expectations | Retested locally |
| DT generation/preservation/idempotence on tutorial reference DTB | Passed; generated DTB not boot-tested |
| Xilinx/PetaLinux target cross-build and module load | Pending |
| Actual QEMU ↔ SystemC MMIO and physical interrupt/GIC route | Pending |
| Full inference through real SystemC accelerator | Pending |

The full kernel-mock tests recorded 713,245 device calls, 713,245 CPU-reference
checks, zero application mock calls, zero fallbacks, and zero errors per tested
run. Generated text matched the CPU baseline. These are historical local results.
The mock's software notification exercises driver completion handling, not the
real GIC or SystemC interrupt wiring.

Local kernel tests used Alpine ARM Linux **6.18.59-0-virt**. Target PetaLinux is
expected to use Linux 5.15.x, which must be confirmed in the team's running guest.
The driver has compatibility code, but the target build still needs testing.

## 6. Build on the receiving machine

Do not deploy copied `.ko`, `.o`, CMake caches, or binaries from `part4/build/`.
Those include Mac executables and Alpine/musl ARM test binaries. Rebuild for the
actual guest kernel and userspace. Use a new application build directory so copied
CMake absolute paths do not contaminate the build.

On the tutorial/LRC build host, load the team's established environment, e.g.:

```sh
module load xilinx/2022
source /usr/local/packages/Xilinx_2022.2/Vivado/2022.2/settings64.sh
```

Confirm the cross compiler and sysroot are appropriate for the guest. Inside the
guest, record `uname -r`. The module's prepared kernel build tree must match that
kernel, its configuration, and module versioning.

```sh
make -C part4/driver KDIR=/home/projects/gerstl/ece382m/linux-xlnx
make -C part4 arm

python3 part4/scripts/build_llama.py --target arm \
  --build-dir part4/build/llama-petalinux \
  --toolchain /home/projects/gerstl/ece382m/ultra96_toolchain.cmake
```

The module Makefile defaults are ARCH=arm64, CROSS_COMPILE=aarch64-linux-gnu-,
KDIR=/home/projects/gerstl/ece382m/linux-xlnx. Override them if the actual tutorial
installation differs. The supplied toolchain path is from the assignment; check
it exists. Alternatively use the script's bundled ARM toolchain with the correct
`--cross-prefix`, and configure the target sysroot as needed.

Outputs to copy into guest Linux using the team's normal scp/wget workflow:

- `part4/driver/dot_accel_drv.ko`
- `part4/build/dot_smoke`
- `part4/build/llama-petalinux/bin/llama-completion`
- `models/tinystories-gpt-0.1-3m.Q8_0.gguf`

The build disables shared project libraries, which does **not** guarantee a fully
static executable. Inspect the target ELF interpreter/NEEDED libraries with the
cross-toolchain's readelf and ensure guest runtimes exist, especially libstdc++.
Rebuild with appropriate target static runtime options if practical, rather than
copying incompatible host/Alpine libraries into PetaLinux.

The included llama.cpp is pinned at
`ddf48f85c933c1f438212a02eb21e7b001d2d37d`, a fresh upstream checkout with our hook;
it is not the team's recovered Lab 1 tree. If using a different Lab 1 version,
port the small dispatch/CMake changes deliberately and prove its real Q8_0 calls
reach the wrapper. Do not replace the source version casually.

## 7. First real accelerator integration: polling

First confirm the teammate's SystemC platform actually maps the accelerator and
connects its interrupt output. Start the tutorial QEMU and SystemC processes on
the same machine with matching remote-port socket paths and sync quantum.
Revision (4) describes a platform executable named `zynqmp_dot`; use the teammate's
actual build. The old standalone TLM testbench alone cannot serve ARM accesses.

In guest Linux, if `devmem` is available:

```sh
devmem 0xa010001c 32
```

Expected ID is `0xD07A0001`. If this fails, investigate platform bus mapping before
debugging llama.cpp or enabling the mock to conceal the failure.

When no accelerator DT node is present, use the hardcoded-resource polling mode
for first bring-up (**guest**, root shell or sudo):

```sh
insmod ./dot_accel_drv.ko manual=1
./dot_smoke /dev/dot_accel --case 0 --check-design7-cycles
./dot_smoke /dev/dot_accel --ones 28 --check-design7-cycles
./dot_smoke /dev/dot_accel --check-design7-cycles
```

Expected first case: RESULT=0x42000000, value=32, CYCLES=22.
Expected NB=28 all-ones case: RESULT=0x44600000, value=896, CYCLES=130.
The final command checks all 11 golden cases plus zero length.

`manual=1` is polling only. Do not combine it with `use_irq=1` or mock mode. If the
accelerator DT node is already present, load without manual=1; default operation
uses its DT resources with polling. Unload the previous module before changing modes.

## 8. Device tree and interrupts

**Keep the original `fpga0@a0000000` / `ece382m,fpga` tutorial node and driver.**
Revision (4) adds a separate `dotaccel0@a0100000` sibling with compatible
`ece382m,dotaccel`. Older advice to replace the tutorial node is superseded.

On a build host with dtc, fdtget, fdtput, make, and a native C compiler:

```sh
python3 part4/scripts/prepare_device_tree.py \
  --input /path/to/actual/boot/system.dtb \
  --output /path/to/new/system-dotaccel.dtb
```

Use the team's **actual boot DTB**, not the locally generated reference output.
The script preserves the tutorial node, checks 2/2 address/size cells, copies its
actual interrupt-parent, and inserts the accelerator's two resources and
`interrupts = <0 94 4>`. It emits verification JSON and a decompiled DTS for review.
It does not overwrite the input, install the DTB, or reboot the guest.

Install the verified DTB through the team's boot workflow, reboot, then **guest**:

```sh
insmod ./dot_accel_drv.ko use_irq=1
./dot_smoke /dev/dot_accel --check-design7-cycles
cat /proc/interrupts
```

Check the interrupt count increases and the line deasserts after completion. The
driver obtains the Linux virtual IRQ via platform_get_irq; neither 94 nor 126 is
a number to hardcode as the Linux virtual IRQ. If IRQ mode fails but polling
works, investigate DT/GIC/SystemC wiring and acknowledgement.

## 9. Full model test through real accelerator

Use the freshly target-built application and real device, with the module loaded
without mock flags. In a guest directory containing the four copied outputs:

```sh
LAB3_DOT_BACKEND=device LAB3_DOT_DEVICE=/dev/dot_accel \
LAB3_DOT_VERIFY=1 LAB3_DOT_VERIFY_LIMIT=32 \
./llama-completion -m ./tinystories-gpt-0.1-3m.Q8_0.gguf \
  -p 'Once upon a time' -n 1 -t 1 -tb 1 -b 1 -ub 1 -c 256 -ngl 0 \
  --temp 0 --seed 1 --no-warmup --no-conversation --simple-io --no-display-prompt
```

After the one-token test passes, repeat with `-n 10`. Co-simulation may take much
longer than native execution. Verification compares selected returned values with
the software dot product; `LAB3_DOT_VERIFY_LIMIT=0` checks every supported call
and costs more time. Run the same invocation with LAB3_DOT_BACKEND=cpu for the
baseline, and compare generated text and counters.

When Python and the source tree are available inside the guest, the comparison
script automates the two runs and records logs:

```sh
python3 part4/scripts/validate_llama.py --backend device \
  --binary part4/build/llama-petalinux/bin/llama-completion \
  --device /dev/dot_accel --tokens 10 --verify-limit 32 \
  --timeout 3600 --output-dir part4/results/real-systemc-first-run
```

Inspect wrapper counters: device_calls must be nonzero, application mock_calls
must be zero, and errors must be zero. Investigate unexpected fallback calls for
this model. Preserve dmesg, smoke outputs, CPU/device text, counters, and the actual
build/kernel/DT/platform versions as integration evidence.

Backend meanings: `cpu` is the original software path; `mock` is the older
application-only mock that bypasses the driver; `device` performs real ioctls.
Using `device` with `/dev/dot_accel_mock` still means a kernel mock, not hardware.
The wrapper does not silently hide device errors by switching to a mock.

## 10. Optional local regressions and packaging

After copying onto a different host, existing host test executables may be the
wrong architecture. Compile into fresh output paths or remove only those generated
test executables before running `make -C part4 test test-softfloat`.

The Mac-specific `part4/scripts/run_driver_vm.py --full-flow` reproduces the saved
ARM VM tests when its QEMU and cached VM assets are available. It is optional;
it is not the team's QEMU/SystemC platform, and Docker is not required.

`output/part4_source_handoff.tar.gz` is a smaller source handoff containing this
README, Part 4, and the modified llama.cpp source. It excludes generated builds,
VM images, .git directories, and model binaries. Copy the GGUF separately or use
`models/README.md`. `part4_driver_handoff.tar.gz` excludes llama.cpp source too.
Copying this entire workspace, as planned, retains the model and all local assets,
but those assets still need the portability precautions above.

Recreate archives after source/document changes:

```sh
python3 part4/scripts/package_submission.py
python3 part4/scripts/package_submission.py --driver-only
```

The remaining deliverable is a successful target build, real device smoke tests,
real interrupt validation if used, and TinyStories inference through the actual
SystemC accelerator. Preserve the working HAL/ABI while resolving those platform
integration steps; report mock and real-accelerator evidence separately.
# SOC_LAB3
