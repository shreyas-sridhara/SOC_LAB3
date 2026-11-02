#/bin/sh
# setup.sh -- install the accelerator platform into a systemctlm-cosim-demo checkout
#
#   cosim/setup.sh <path-to-systemctlm-cosim-demo>
#
set -e

DEMO="$1"
if [ -z "$DEMO" ] || [ ! -f "$DEMO/zynqmp_demo.cc" ]; then
    echo "usage: $0 <path-to-systemctlm-cosim-demo>   (the directory containing zynqmp_demo.cc)" >&2
    exit 1
fi
if [ ! -f "$DEMO/libsystemctlm-soc/soc/xilinx/zynqmp/xilinx-zynqmp.h" ]; then
    echo "libsystemctlm-soc submodule is missing: cd $DEMO && git submodule update --init libsystemctlm-soc" >&2
    exit 1
fi

HERE="$(cd "$(dirname "$0")" && pwd)"
PART3="$(dirname "$HERE")"

for f in dot_accel.h dot_accel_regs.h dot_q8_0_core.h dot_q8_0_core.cpp; do
    cp "$PART3/$f" "$DEMO/$f"
done
cp "$HERE/zynqmp_dot.cc" "$HERE/Makefile.dot" "$DEMO/"

echo "installed into $DEMO. Now:"
echo "  cd $DEMO"
echo "  make -f Makefile.dot SYSTEMC=<systemc install dir> zynqmp_dot"
