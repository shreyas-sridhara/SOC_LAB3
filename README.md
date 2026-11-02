# Lab 3: Run llama.cpp with the SystemC Accelerator

Before starting, complete the tutorial's QEMU/SystemC environment setup on LRC.
The tutorial Linux disk image, firmware, and device-tree files should be present
under `images/linux/`, and the baseline QEMU platform should already boot.
The `systemctlm-cosim-demo` checkout should include its initialized
`libsystemctlm-soc` submodule, with the tutorial's build dependencies available.

Use the following folder layout so the relative paths in the commands work:

```text
/misc/scratch/$USER/Lab3/
├── images/
│   └── linux/                 # Tutorial boot files and Linux disk image
├── systemctlm-cosim-demo/      # Tutorial SystemC platform and submodule
└── SOC_LAB3/                  # This repository
    ├── part3/
    └── part4/
```

The steps below add our accelerator and application to that existing setup.

## 1. Add Part 4 to llama.cpp - LRC

```bash
cd /misc/scratch/$USER/Lab3/SOC_LAB3

git clone https://github.com/ggml-org/llama.cpp
bash part4/apply_to_llama.sh llama.cpp
```

Skip the clone if `llama.cpp` already exists. The script copies our HAL/driver
sources into it and updates GGML to call the HAL. 

## 2. Build the accelerated application - LRC

```bash
module load xilinx/2022
source /usr/local/packages/Xilinx_2022.2/Vivado/2022.2/settings64.sh
module load gcc/14

cd llama.cpp
cp /home/projects/gerstl/ece382m/ultra96_toolchain.cmake .
mkdir -p build
cd build

/usr/bin/cmake \
  -DCMAKE_TOOLCHAIN_FILE=../ultra96_toolchain.cmake \
  -DBUILD_SHARED_LIBS=OFF \
  -DSOC_DOT_HAL_DIR="$PWD/../part4" \
  -DGGML_NATIVE=OFF \
  -DGGML_BLAS=OFF \
  -DGGML_LLAMAFILE=OFF \
  -DGGML_METAL=OFF \
  -DGGML_ACCELERATE=OFF \
  -DGGML_CPU_REPACK=OFF \
  -DGGML_CPU_KLEIDIAI=OFF \
  -DGGML_OPENMP=OFF \
  ..

make -j 8
cd ..
cp build/bin/llama-cli ../llama-accel
```

## 3. Build the CPU reference - LRC

Still inside `llama.cpp`:

```bash
/usr/bin/cmake -S . -B build-reference \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/ultra96_toolchain.cmake" \
  -DBUILD_SHARED_LIBS=OFF \
  -DSOC_DOT_HAL_DIR= \
  -DGGML_NATIVE=OFF \
  -DGGML_BLAS=OFF \
  -DGGML_LLAMAFILE=OFF \
  -DGGML_METAL=OFF \
  -DGGML_ACCELERATE=OFF \
  -DGGML_CPU_REPACK=OFF \
  -DGGML_CPU_KLEIDIAI=OFF \
  -DGGML_OPENMP=OFF

/usr/bin/cmake --build build-reference --target llama-cli --parallel 8
cp build-reference/bin/llama-cli ../llama-reference
cd ..
```

## 4. Build the driver and get the model - LRC

From `SOC_LAB3`:

```bash
make -C part4/driver ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- \
  KDIR=/home/projects/gerstl/ece382m/linux-xlnx

mkdir -p models
wget -O models/tinystories-gpt-0.1-3m.Q8_0.gguf \
  https://huggingface.co/afrideva/Tinystories-gpt-0.1-3m-GGUF/resolve/main/tinystories-gpt-0.1-3m.Q8_0.gguf
```

Skip the download if the model is already present.

## 5. Build the Part 3 platform - LRC

From `SOC_LAB3`:

```bash
module load systemc/2.3.4
sh part3/cosim/setup.sh ../systemctlm-cosim-demo
make -C ../systemctlm-cosim-demo -f Makefile.dot \
  SYSTEMC=/usr/local/packages/systemc-2.3.4 zynqmp_dot
```

## 6. Start QEMU, then SystemC - two LRC terminals on the same machine

**Terminal 1 - QEMU:**

```bash
cd /misc/scratch/$USER/Lab3
/home/projects/gerstl/ece382m/qemu-boot -c
```

**Terminal 2 - SystemC:**

```bash
module load systemc/2.3.4
cd /misc/scratch/$USER/Lab3/systemctlm-cosim-demo
./zynqmp_dot unix:../tmp/qemu-rport-_amba@0_cosim@0 1000000 \
  -v 2 > dot_trace.log 2>&1
```

QEMU will continue booting after SystemC connects. Both use a sync quantum of
`1000000` ns. Use `-v 0` for faster runs without tracing, or `-v 3` to also log
scratchpad writes.

## 7. Transfer files into QEMU - another LRC terminal

Once guest Linux has booted and SSH is available:

```bash
cd /misc/scratch/$USER/Lab3/SOC_LAB3
scp -O -P 2222 llama-accel llama-reference part4/driver/dot_accel_drv.ko \
  models/tinystories-gpt-0.1-3m.Q8_0.gguf petalinux@127.0.0.1:~/
```

Log in through the QEMU console, or open a second guest shell:

```bash
ssh -p 2222 petalinux@127.0.0.1
```

## 8. Load the driver - inside QEMU

Check the accelerator is accessible:

```bash
cd ~
chmod +x llama-reference llama-accel
sudo devmem 0xa010001c 32
```

Expected accelerator ID: `0xD07A0001`.

**With the accelerator device-tree node installed**, use interrupt mode:

```bash
sudo insmod ./dot_accel_drv.ko use_irq=1
```

Skip it if the driver is already loaded.

```bash
ls -l /dev/dot_accel
dmesg | grep dot_accel
```

Expect `/dev/dot_accel` and a driver-ready message.

## 9. Run and compare - inside QEMU

```bash
./llama-reference --temp 0 --seed 1 -n 10 \
  -m tinystories-gpt-0.1-3m.Q8_0.gguf \
  -p "Once upon a time" -st -t 1 -tb 1 --simple-io \
  > reference.txt 2> reference.log
echo $?

sudo devmem 0xa0100018 32

sudo ./llama-accel --temp 0 --seed 1 -n 10 \
  -m tinystories-gpt-0.1-3m.Q8_0.gguf \
  -p "Once upon a time" -st -t 1 -tb 1 --simple-io \
  > accelerated.txt 2> accelerated.log
echo $?

sudo devmem 0xa0100018 32
cat reference.txt
cat accelerated.txt
cat accelerated.log
diff -u reference.txt accelerated.txt
```

The application needs `sudo` because the device is root-only. Co-simulation can
be much slower than the reference; use `-n 1` in both commands for a shorter test.

