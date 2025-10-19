#!/bin/bash
set -e

# Configuration
DIR=$(readlink -f .)
MAIN=$(readlink -f ${DIR}/..)
KERNEL_DEFCONFIG=peridot_defconfig
CLANG_DIR="$MAIN/toolchains/clang-17"
KERNEL_DIR=$(pwd)
OUT_DIR="$KERNEL_DIR/out"
ZIMAGE_DIR="$OUT_DIR/arch/arm64/boot"
DTB_DTBO_DIR="$ZIMAGE_DIR/dts/vendor/qcom"
BUILD_START=$(date +"%s")

# Techpack paths
export AUDIO_ROOT="$KERNEL_DIR/techpack/audio-kernel"
export CAMERA_ROOT="$KERNEL_DIR/techpack/camera-kernel"
export DISPLAY_ROOT="$KERNEL_DIR/techpack/display-drivers"
export GRAPHICS_ROOT="$KERNEL_DIR/techpack/graphics-kernel"
export MM_ROOT="$KERNEL_DIR/techpack/mm-drivers"
export MMRM_ROOT="$KERNEL_DIR/techpack/mmrm-driver"
export SECUREMSM_ROOT="$KERNEL_DIR/techpack/securemsm-kernel"
export SYNX_ROOT="$KERNEL_DIR/techpack/synx-kernel"
export TOUCH_ROOT="$KERNEL_DIR/techpack/touch-drivers"
export WLAN_ROOT="$KERNEL_DIR/techpack/wlan"

# Function to check for existing Clang
check_clang() {
    if [ -d "$CLANG_DIR" ] && [ -f "$CLANG_DIR/bin/clang" ]; then
        export PATH="$CLANG_DIR/bin:$PATH"
        export KBUILD_COMPILER_STRING="$($CLANG_DIR/bin/clang --version | head -n 1 | perl -pe 's/\(http.*?\)//gs' | sed -e 's/  */ /g' -e 's/[[:space:]]*$//')"
        echo "Found existing Clang: $KBUILD_COMPILER_STRING"
        return 0
    fi
    return 1
}

# Install Clang if needed
if ! check_clang; then
    echo "No valid Clang found. Installing..."
    echo "1. AOSP Clang (clang-r487747c)"
    echo "2. Prelude Clang"
    read -p "Choose [1-2]: " clang_choice

    case "$clang_choice" in
        1)
            CLANG_URL="https://android.googlesource.com/platform/prebuilts/clang/host/linux-x86/+archive/refs/heads/android14-release/clang-r487747c.tar.gz"
            ARCHIVE_NAME="clang.tar.gz"
            mkdir -p "$CLANG_DIR"
            wget -P "$MAIN" "$CLANG_URL" -O "$MAIN/$ARCHIVE_NAME" || exit 1
            tar -xf "$MAIN/$ARCHIVE_NAME" -C "$CLANG_DIR" --strip-components=1 || exit 1
            rm -f "$MAIN/$ARCHIVE_NAME"
            ;;
        2)
            git clone --depth=1 https://gitlab.com/jjpprrrr/prelude-clang.git "$CLANG_DIR" || exit 1
            ;;
        *)
            echo "Invalid choice. Exiting..."
            exit 1
            ;;
    esac

    if ! check_clang; then
        echo "Clang installation failed. Exiting..."
        exit 1
    fi
fi

# Set up toolchain
export LD=ld.lld
export ARCH=arm64
export SUBARCH=arm64
export CROSS_COMPILE=aarch64-linux-gnu-

# Include paths
export KERNEL_SRC="$KERNEL_DIR"
INCLUDE_PATHS="
    -I$KERNEL_DIR/include
    -I$KERNEL_DIR/arch/arm64/include
    -I$KERNEL_DIR/drivers/base/regmap
    -I$AUDIO_ROOT/include
    -I$CAMERA_ROOT/include
    -I$DISPLAY_ROOT/include
"

# Build flags
MAKE_OPTS="
    CC=clang
    STRIP=llvm-strip
    LD=ld.lld
    AR=llvm-ar
    NM=llvm-nm
    OBJCOPY=llvm-objcopy
    OBJDUMP=llvm-objdump
    HOSTCC=clang
    HOSTCXX=clang++
    HOSTAR=llvm-ar
    HOSTLD=ld.lld
    LLVM=1
    LLVM_IAS=1
    $INCLUDE_PATHS
"

# Apply YYLLOC workaround
echo "Applying YYLLOC workaround..."
YYLL1="$KERNEL_DIR/scripts/dtc/dtc-lexer.lex.c_shipped"
YYLL2="$KERNEL_DIR/scripts/dtc/dtc-lexer.l"
[ -f "$YYLL1" ] && sed -i "s/extern YYLTYPE yylloc/YYLTYPE yylloc/g;s/YYLTYPE yylloc/extern YYLTYPE yylloc/g" "$YYLL1"
[ -f "$YYLL2" ] && sed -i "s/extern YYLTYPE yylloc/YYLTYPE yylloc/g;s/YYLTYPE yylloc/extern YYLTYPE yylloc/g" "$YYLL2"

# Start build process
echo "**** Building with $KBUILD_COMPILER_STRING ****"
echo "**** Defconfig: $KERNEL_DEFCONFIG ****"

# Build kernel
make O="$OUT_DIR" $KERNEL_DEFCONFIG $MAKE_OPTS || exit 1
make -j$(nproc --all) O="$OUT_DIR" $MAKE_OPTS || exit 1

# Build modules
BUILD_HAS_MODULES=$(grep "=m" "$OUT_DIR/.config" | wc -l)
if [ $BUILD_HAS_MODULES -gt 0 ]; then
    echo "Building modules..."
    make -j$(nproc --all) O="$OUT_DIR" $MAKE_OPTS modules || exit 1

    # Install modules to temporary directory
    MODULES_DIR="$OUT_DIR/modules_temp"
    rm -rf "$MODULES_DIR"
    mkdir -p "$MODULES_DIR"
    make O="$OUT_DIR" INSTALL_MOD_PATH="$MODULES_DIR" INSTALL_MOD_STRIP=1 modules_install || exit 1

    # Clean up symlinks
    find "$MODULES_DIR" -type l -delete
fi

# Restore YYLL files if in git repo
[ -d "$KERNEL_DIR"/.git ] && git restore "$YYLL1" "$YYLL2" 2>/dev/null || true

# Clean up old kernel zip files
echo "Cleaning up old kernel zip files..."
find "$KERNEL_DIR" -maxdepth 1 -type f -name "RealKing-Peridot-*.zip" -exec rm -v {} \;

# Create temporary anykernel directory
TIME=$(date "+%Y%m%d-%H%M%S")
TEMP_ANY_KERNEL_DIR="$KERNEL_DIR/anykernel_temp"
rm -rf "$TEMP_ANY_KERNEL_DIR"

# Clone entire anykernel directory
echo "Cloning anykernel directory..."
if [ -d "$KERNEL_DIR/anykernel" ]; then
    cp -r "$KERNEL_DIR/anykernel" "$TEMP_ANY_KERNEL_DIR"
else
    echo "Error: anykernel directory not found!"
    exit 1
fi

# Copy kernel image
if [ -f "$ZIMAGE_DIR/Image.gz-dtb" ]; then
    cp -v "$ZIMAGE_DIR/Image.gz-dtb" "$TEMP_ANY_KERNEL_DIR/"
elif [ -f "$ZIMAGE_DIR/Image.gz" ]; then
    cp -v "$ZIMAGE_DIR/Image.gz" "$TEMP_ANY_KERNEL_DIR/"
elif [ -f "$ZIMAGE_DIR/Image" ]; then
    cp -v "$ZIMAGE_DIR/Image" "$TEMP_ANY_KERNEL_DIR/"
fi

# Handle _modules directory
if [ $BUILD_HAS_MODULES -gt 0 ]; then
    echo "Preparing _modules directory..."

    # Check if _modules exists in template
    if [ -d "$TEMP_ANY_KERNEL_DIR/_modules" ]; then
        echo "Found existing _modules directory, clearing contents..."
        rm -f "$TEMP_ANY_KERNEL_DIR/_modules"/*.ko
    else
        echo "Creating _modules directory..."
        mkdir -p "$TEMP_ANY_KERNEL_DIR/_modules"
    fi

    # List of specific modules to include
    IMPORTANT_MODULES=(
        "cnss_nl.ko" "cnss_plat_ipc_qmi_svc.ko" "cnss_prealloc.ko" "cnss_utils.ko" "cnss2.ko"
        "goodix_ts.ko" "icnss2.ko" "ipam.ko" "ipanetm.ko"
        "lpass_cdc_dlkm.ko" "lpass_cdc_rx_macro_dlkm.ko" "lpass_cdc_tx_macro_dlkm.ko"
        "lpass_cdc_va_macro_dlkm.ko" "lpass_cdc_wsa_macro_dlkm.ko" "lpass_cdc_wsa2_macro_dlkm.ko"
        "msm_drm.ko" "msm_kgsl.ko" "mi_thermal_interface.ko"
        "qti_cpufreq_cdev.ko" "qti_devfreq_cdev.ko" "rmnet_offload.ko" "rmnet_shs.ko"
        "smcinvoke_dlkm.ko" "wcd_core_dlkm.ko" "wcd9xxx_dlkm.ko"
        "wcd937x_dlkm.ko" "wcd937x_slave_dlkm.ko" "wcd938x_dlkm.ko"
        "wcd938x_slave_dlkm.ko" "wcd939x_dlkm.ko" "wcd939x_slave_dlkm.ko"
        "wlan_firmware_service.ko" "xiaomi_touch.ko" "fs19xx_dlkm.ko" "aw882xx_dlkm.ko" "qca_cld3_kiwi_v2.ko"
        "spf_core_dlkm.ko" "snd_event_dlkm.ko" "gpr_dlkm.ko" "panel_event_notifier.ko" "machine_dlkm.ko" "focaltech_3683g.ko" "dcvs_boost.ko"
    )

    # Find and copy each specified module
    for module in "${IMPORTANT_MODULES[@]}"; do
        find "$MODULES_DIR/lib/modules" -name "$module" -exec cp -v {} "$TEMP_ANY_KERNEL_DIR/_modules/" \;
    done

    echo "Modules in _modules:"
    ls -lh "$TEMP_ANY_KERNEL_DIR/_modules"
fi

# Generate DTBO if not already
echo "=========================================="
echo "Generating DTBO from peridot-*.dtbo"
echo "=========================================="
scripts/mkdtboimg.py create $TEMP_ANY_KERNEL_DIR/dtbo.img \
  $(find out/arch/arm64/boot/dts/ -name "peridot-*.dtbo" -type f)

# Move Appropriate .DTB to ZIP
echo "=========================================="
echo "Generating DTB blob from cliffs.dtb"
echo "=========================================="
cat out/arch/arm64/boot/dts/vendor/qcom/cliffs.dtb > $TEMP_ANY_KERNEL_DIR/dtb

# Create zip file in kernel root directory
echo "Creating zip package..."
ZIP_NAME="RealKing-Peridot-$TIME.zip"
cd "$TEMP_ANY_KERNEL_DIR"
zip -r9 "$KERNEL_DIR/$ZIP_NAME" ./*
cd ..

# Clean up temporary directory
rm -rf "$TEMP_ANY_KERNEL_DIR"

BUILD_END=$(date +"%s")
DIFF=$((BUILD_END - BUILD_START))
echo -e "\nBuild completed in $((DIFF / 60))m $((DIFF % 60))s"
echo "Final zip: $KERNEL_DIR/$ZIP_NAME"
echo "Zip size: $(du -h "$KERNEL_DIR/$ZIP_NAME" | cut -f1)"
