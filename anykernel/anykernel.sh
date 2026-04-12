### AnyKernel3 Ramdisk Mod Script
## osm0sis @ xda-developers

### AnyKernel setup
# begin properties
properties() { '
kernel.string=RealKing Kernel by Rohail(@Rohail33)--Telegram
do.devicecheck=0
do.modules=0
do.systemless=1
do.cleanup=1
do.cleanuponabort=0
device.name3=
device.name4=
device.name5=
supported.versions=
supported.patchlevels=
'; } # end properties

### AnyKernel install

## boot shell variables
block=boot
is_slot_device=1
ramdisk_compression=auto
patch_vbmeta_flag=auto
vendor_dlkm_partition=vendor_dlkm
system_dlkm_partition=system_dlkm
vendor_boot_partition=vendor_boot
vendor_boot_single_module_updated=false

# import functions/variables and setup patching - see for reference (DO NOT REMOVE)
. tools/ak3-core.sh

split_boot

########## CUSTOM START ##########

BOOTMODE=false;
ps | grep zygote | grep -v grep >/dev/null && BOOTMODE=true;
$BOOTMODE || ps -A 2>/dev/null | grep zygote | grep -v grep >/dev/null && BOOTMODE=true;


extract_erofs() {
	local img_file=$1
	local out_dir=$2

	${bin}/extract.erofs -i $img_file -x -T8 -o $out_dir &> /dev/null
}

mkfs_erofs() {
	local work_dir=$1
	local out_file=$2

	local partition_name=$(basename $work_dir)

	${bin}/mkfs.erofs \
		--mount-point /${partition_name} \
		--fs-config-file ${work_dir}/../config/${partition_name}_fs_config \
		--file-contexts  ${work_dir}/../config/${partition_name}_file_contexts \
		-z lz4hc \
		$out_file $work_dir
}

is_mounted() { mount | grep -q " $1 "; }

append_erofs_metadata() {
	local extract_dir=$1
	local partition_name=$2
	local relative_path=$3
	local file_context=$4

	cat ${extract_dir}/config/${partition_name}_fs_config | grep -q "lib/modules/${relative_path}" || \
		echo "${partition_name}/lib/modules/${relative_path} 0 0 0644" >> ${extract_dir}/config/${partition_name}_fs_config
	cat ${extract_dir}/config/${partition_name}_file_contexts | grep -q "lib/modules/${relative_path}" || \
		echo "/${partition_name}/lib/modules/${relative_path} ${file_context}" >> ${extract_dir}/config/${partition_name}_file_contexts
}

SYSTEM_DLKM_MODULES="
	zram.ko
	zsmalloc.ko
"

copy_special_modules() {
	local dst_dir=$1
	local additional_modules="$2"
	local search_dir src_path module_name existing_path target_path relative_path tmp_path module_size
	local processed_modules=""

	system_dlkm_copied_modules=""

	[ -d ${dst_dir} ] || mkdir -p ${dst_dir}

	for module_name in $SYSTEM_DLKM_MODULES $additional_modules; do
		case " $processed_modules " in
			*" ${module_name} "*) continue ;;
		esac
		processed_modules="${processed_modules} ${module_name}"
		src_path=""
		existing_path=$(find "$dst_dir" -type f \( -name "${module_name}" -o -name "*${module_name}" \) | head -n1)

		for search_dir in ${home}/_system_dlkm ${home}/_modules $search_system_dlkm_modules_dir $search_vendor_dlkm_modules_dir; do
			[ -d "$search_dir" ] || continue
			src_path=$(find "$search_dir" -type f \( -name "${module_name}" -o -name "*${module_name}" \) | head -n1)
			[ -n "$src_path" ] || continue
			break
		done

		[ -n "$src_path" ] || abort "! Failed to locate ${module_name} for ${system_dlkm_partition} update"
		[ -s "$src_path" ] || abort "! Source module is empty: $src_path"

		if [ -n "$existing_path" ]; then
			target_path=$existing_path
		else
			target_path=${dst_dir}/${module_name}
		fi

		mkdir -p "$(dirname "$target_path")"
		tmp_path=${target_path}.aknew
		rm -f "$tmp_path"
		cp -f "$src_path" "$tmp_path" || abort "! Failed to copy ${module_name} into ${system_dlkm_partition}"
		[ -s "$tmp_path" ] || abort "! Copied module is empty: $tmp_path"
		mv -f "$tmp_path" "$target_path" || abort "! Failed to move ${module_name} into place"
		module_size=$(wc -c < "$target_path")
		[ "$module_size" -gt 0 ] || abort "! Installed module is empty: $target_path"
		relative_path=${target_path#${dst_dir}/}
		system_dlkm_copied_modules="${system_dlkm_copied_modules} ${relative_path}"
	done
}

collect_shared_system_dlkm_modules() {
	local system_modules_dir=$1
	local vendor_modules_dir=$2
	local module_src module_name system_match vendor_match

	system_dlkm_shared_modules=""
	[ -d ${home}/_modules ] || return 0

	for module_src in ${home}/_modules/*.ko; do
		[ -f "$module_src" ] || continue
		module_name=$(basename "$module_src")
		system_match=$(find "$system_modules_dir" -type f \( -name "${module_name}" -o -name "*${module_name}" \) | head -n1)
		[ -n "$system_match" ] || continue
		vendor_match=$(find "$vendor_modules_dir" -type f \( -name "${module_name}" -o -name "*${module_name}" \) | head -n1)
		[ -n "$vendor_match" ] || continue
		system_dlkm_shared_modules="${system_dlkm_shared_modules} ${module_name}"
	done
}

find_named_block() {
	local partition_name=$1
	local partition_path

	for partition_path in /dev/block/mapper /dev/block/by-name /dev/block/bootdevice/by-name; do
		[ -e ${partition_path}/${partition_name}${slot} ] && {
			echo ${partition_path}/${partition_name}${slot}
			return 0
		}
		[ -e ${partition_path}/${partition_name} ] && {
			echo ${partition_path}/${partition_name}
			return 0
		}
	done

	return 1
}

detect_ramdisk_compression() {
	local file_path=$1
	local magic

	magic=$(dd if="$file_path" bs=6 count=1 2>/dev/null | od -An -tx1 | tr -d ' \n')
	case "$magic" in
		1f8b08*) echo gzip ;;
		fd377a585a00*) echo xz ;;
		425a68*) echo bzip2 ;;
		5d0000*) echo lzma ;;
		04224d18*) echo lz4 ;;
		02214c18*) echo lz4_legacy ;;
		*) echo none ;;
	esac
}

patch_vendor_boot_single_modules() {
	local vendor_boot_dir=${home}/_vendor_boot
	local shared_modules_dir=${home}/_modules
	local vendor_boot_block ramdisk_src out_dir replaced_count ramdisk_compression repacked_img patched_ramdisk
	local module_src module_name target_path
	local has_vendor_boot_modules=false has_shared_modules=false

	if [ -d "$vendor_boot_dir" ]; then
		set -- ${vendor_boot_dir}/*.ko
		[ -e "$1" ] && has_vendor_boot_modules=true
	fi
	if [ -d "$shared_modules_dir" ]; then
		set -- ${shared_modules_dir}/*.ko
		[ -e "$1" ] && has_shared_modules=true
	fi
	$has_vendor_boot_modules || $has_shared_modules || return 0

	vendor_boot_block=$(find_named_block ${vendor_boot_partition})
	[ -n "$vendor_boot_block" ] || abort "! Failed to find ${vendor_boot_partition} partition"

	ui_print "- Dumping /${vendor_boot_partition} partition..."
	dd if=${vendor_boot_block} of=${home}/vendor_boot.img 2>/dev/null || abort "! Failed to dump vendor_boot"

	out_dir=${home}/out
	rm -rf "$out_dir"
	mkdir -p "$out_dir"

	ui_print "- Unpacking vendor_boot image with magiskboot..."
	(
		cd ${home}
		${bin}/magiskboot unpack -h vendor_boot.img >/dev/null 2>&1
	) || abort "! Failed to unpack vendor_boot.img"

	ramdisk_src=$(find ${home} -maxdepth 1 -type f \( -name 'vendor_ramdisk.cpio' -o -name 'ramdisk.cpio' -o -name '*ramdisk*.cpio' \) | head -n1)
	[ -n "$ramdisk_src" ] || abort "! Failed to locate vendor ramdisk cpio"
	ramdisk_compression=$(detect_ramdisk_compression "$ramdisk_src")
	${bin}/magiskboot decompress "$ramdisk_src" ${out_dir}/ramdisk.cpio >/dev/null 2>&1 || \
		cp -f "$ramdisk_src" ${out_dir}/ramdisk.cpio

	ui_print "- Extracting ramdisk to ${home}/out..."
	(
		cd "$out_dir"
		cpio -idmv < ramdisk.cpio
	) || abort "! Failed to extract vendor ramdisk"

	replaced_count=0
	if $has_vendor_boot_modules; then
		for module_src in ${vendor_boot_dir}/*.ko; do
			[ -f "$module_src" ] || continue
			module_name=$(basename "$module_src")
			target_path=$(find ${out_dir} -type f -path '*/lib/modules/*' -name "$module_name" | head -n1)
			[ -n "$target_path" ] || continue
			ui_print "- Replacing ${module_name} in vendor_boot"
			cp -f "$module_src" "$target_path"
			replaced_count=$((replaced_count + 1))
		done
	fi

	# Also patch shared modules from _modules when the same module exists in vendor_boot ramdisk.
	if $has_shared_modules; then
		for module_src in ${shared_modules_dir}/*.ko; do
			[ -f "$module_src" ] || continue
			module_name=$(basename "$module_src")
			[ -f "${vendor_boot_dir}/${module_name}" ] && continue
			target_path=$(find ${out_dir} -type f -path '*/lib/modules/*' -name "$module_name" | head -n1)
			[ -n "$target_path" ] || continue
			ui_print "- Replacing ${module_name} in vendor_boot (shared module)"
			cp -f "$module_src" "$target_path"
			replaced_count=$((replaced_count + 1))
		done
	fi

	[ "$replaced_count" -gt 0 ] || {
		ui_print "- No matching vendor_boot modules found to replace"
		rm -rf "$out_dir"
		return 0
	}

	(
		cd "$out_dir"
		rm -f ramdisk-new.cpio
		find . -mindepth 1 ! -name 'ramdisk.cpio' -print | LC_ALL=C sort | cpio -o -H newc > ramdisk-new.cpio 2>/dev/null
	) || abort "! Failed to rebuild ramdisk cpio"
	patched_ramdisk=${out_dir}/ramdisk-new.cpio
	if [ "$ramdisk_compression" != "none" ]; then
		ui_print "- Recompressing vendor ramdisk (${ramdisk_compression})..."
		${bin}/magiskboot compress=${ramdisk_compression} ${out_dir}/ramdisk-new.cpio ${out_dir}/ramdisk-patched.cpio >/dev/null 2>&1 || \
			abort "! Failed to recompress vendor ramdisk"
		patched_ramdisk=${out_dir}/ramdisk-patched.cpio
	fi
	cp -f "$patched_ramdisk" "$ramdisk_src"

	ui_print "- Repacking vendor_boot image with magiskboot..."
	(
		cd ${home}
		${bin}/magiskboot repack vendor_boot.img >/dev/null 2>&1
	) || abort "! Failed to repack vendor_boot.img"

	repacked_img=$(find ${home} -maxdepth 1 -type f \( -name 'new-boot.img' -o -name 'new-vendor_boot.img' -o -name 'new*.img' \) | head -n1)
	[ -n "$repacked_img" ] || abort "! Failed to locate repacked vendor_boot image"
	mv -f "$repacked_img" ${home}/vendor_boot.img
	vendor_boot_single_module_updated=true
	rm -rf "$out_dir"
}

# Check snapshot status
# Technical details: https://blog.xzr.moe/archives/30/
${bin}/snapshotupdater_static dump &>/dev/null
rc=$?
if [ "$rc" != 0 ]; then
	ui_print "Cannot get snapshot status via snapshotupdater_static! rc=$rc."
	if $BOOTMODE; then
		ui_print "If you are installing the kernel in an app, try using another app."
		ui_print "Recommend KernelFlasher:"
		ui_print "  https://github.com/capntrips/KernelFlasher/releases"
	else
		ui_print "Please try to reboot to system once before installing!"
	fi
	abort "Aborting..."
fi
snapshot_status=$(${bin}/snapshotupdater_static dump 2>/dev/null | grep '^Update state:' | awk '{print $3}')
ui_print "Current snapshot state: $snapshot_status"
if [ "$snapshot_status" != "none" ]; then
	ui_print " "
	ui_print "Seems like you just installed a rom update."
	if [ "$snapshot_status" == "merging" ]; then
		ui_print "Please use the rom for a while to wait for"
		ui_print "the system to complete the snapshot merge."
		ui_print "It's also possible to use the \"Merge Snapshots\" feature"
		ui_print "in TWRP's Advanced menu to instantly merge snapshots."
	else
		ui_print "Please try to reboot to system once before installing!"
	fi
	abort "Aborting..."
fi
unset rc snapshot_status

vendor_dlkm_block=$(find_named_block ${vendor_dlkm_partition})
[ -n "$vendor_dlkm_block" ] || abort "! Failed to find ${vendor_dlkm_partition} partition"

# Check vendor_dlkm partition status
[ -d /vendor_dlkm ] || mkdir /vendor_dlkm
is_mounted /vendor_dlkm || \
	mount /vendor_dlkm -o ro || mount ${vendor_dlkm_block} /vendor_dlkm -o ro || \
		abort "! Failed to mount /vendor_dlkm"

strings ${home}/Image 2>/dev/null | grep -E -m1 'Linux version.*#' > ${home}/vertmp

skip_update_flag=false
do_backup_flag=false
if [ -f /vendor_dlkm/lib/modules/vertmp ]; then
	[ "$(cat /vendor_dlkm/lib/modules/vertmp)" == "$(cat ${home}/vertmp)" ] && skip_update_flag=true
else
	do_backup_flag=true
fi
umount /vendor_dlkm

system_dlkm_block=$(find_named_block ${system_dlkm_partition})
do_system_dlkm_update=false
[ -n "$system_dlkm_block" ] && do_system_dlkm_update=true

# Fix unable to mount image as read-write in recovery
$BOOTMODE || setenforce 0

#if $skip_update_flag; then
#else
	# Dump vendor_dlkm partition image
	dd if=${vendor_dlkm_block} of=${home}/vendor_dlkm.img
	if $do_system_dlkm_update; then
		dd if=${system_dlkm_block} of=${home}/system_dlkm.img
	fi

	# Backup kernel and vendor_dlkm image
	#if $do_backup_flag; then
		ui_print "- It looks like you are installing Realking Kernel for the first time."
		ui_print "- Next will backup the kernel and vendor_dlkm partitions..."

		build_prop=/system/build.prop
		[ -d /system_root/system ] && build_prop=/system_root/$build_prop
		backup_package=/sdcard/Realking-restore-kernel-$(file_getprop $build_prop ro.build.version.incremental)-$(date +"%Y%m%d-%H%M%S").zip
		${bin}/7za a -tzip -bd $backup_package \
			${home}/META-INF ${bin} ${home}/LICENSE ${home}/_restore_anykernel.sh ${split_img}/kernel ${home}/vendor_dlkm.img
		${bin}/7za rn -bd $backup_package Image.gz
		${bin}/7za rn -bd $backup_package _restore_anykernel.sh anykernel.sh
		sync

		ui_print " "
		ui_print "- The current kernel and gevendor_dlkm have been backedup to:"
		ui_print "  $backup_package"
		ui_print "- If you encounter an unexpected situation,"
		ui_print "  or want to restore the stock kernel,"
		ui_print "  please flash it in TWRP or some supported apps."
		ui_print " "
		touch ${home}/do_backup_flag

		unset build_prop backup_package
	#fi

	ui_print "- Unpacking /vendor_dlkm partition..."
	extract_vendor_dlkm_dir=${home}/_extract_vendor_dlkm
	mkdir -p $extract_vendor_dlkm_dir
	vendor_dlkm_is_ext4=false
	extract_erofs ${home}/vendor_dlkm.img $extract_vendor_dlkm_dir || vendor_dlkm_is_ext4=true
	sync

	if $vendor_dlkm_is_ext4; then
		ui_print "- /vendor_dlkm partition seems to be in ext4 file system."
		mount ${home}/vendor_dlkm.img $extract_vendor_dlkm_dir -o ro -t ext4 || \
			abort "! Unsupported file system!"
		vendor_dlkm_free_space=$(df -k | grep -E "[[:space:]]$extract_vendor_dlkm_dir\$" | awk '{print $4}')
		umount $extract_vendor_dlkm_dir

		[ "$vendor_dlkm_free_space" -gt 10240 ] || {
			# Resize vendor_dlkm image
			ui_print "- /vendor_dlkm partition does not have enough free space!"
			ui_print "- Trying to resize..."

			${bin}/e2fsck -f -y ${home}/vendor_dlkm.img
			vendor_dlkm_target_size_mb="128M"
			${bin}/resize2fs ${home}/vendor_dlkm.img $vendor_dlkm_target_size_mb || \
				abort "! Failed to resize vendor_dlkm image!"
			ui_print "- Resized vendor_dlkm.img size: ${vendor_dlkm_target_size_mb}M."
			# e2fsck again
			${bin}/e2fsck -f -y ${home}/vendor_dlkm.img

			unset super_free_space vendor_dlkm_current_size_mb vendor_dlkm_target_size_mb
		}

                {bin}/e2fsck -y -E unshare_blocks ${home}/vendor_dlkm.img
                 ui_print "Removing unshared blocks from vendor_dlkm.img partition.."

		ui_print "- Trying to mount vendor_dlkm image as read-write..."
		mount ${home}/vendor_dlkm.img $extract_vendor_dlkm_dir -o rw -t ext4 || \
			abort "! Failed to mount vendor_dlkm.img as read-write!"

		extract_vendor_dlkm_modules_dir=${extract_vendor_dlkm_dir}/lib/modules
	else
		extract_vendor_dlkm_modules_dir=${extract_vendor_dlkm_dir}/vendor_dlkm/lib/modules
	fi
	search_vendor_dlkm_modules_dir=${extract_vendor_dlkm_modules_dir}
	if $do_system_dlkm_update; then
		ui_print "- Unpacking /${system_dlkm_partition} partition..."
		extract_system_dlkm_dir=${home}/_extract_system_dlkm
		mkdir -p $extract_system_dlkm_dir
		system_dlkm_is_ext4=false
		extract_erofs ${home}/system_dlkm.img $extract_system_dlkm_dir || system_dlkm_is_ext4=true
		sync

		if $system_dlkm_is_ext4; then
			ui_print "- /${system_dlkm_partition} partition seems to be in ext4 file system."
			${bin}/e2fsck -y -E unshare_blocks ${home}/system_dlkm.img || \
				abort "! Failed to unshare ext4 blocks in ${system_dlkm_partition}.img"
			mount ${home}/system_dlkm.img $extract_system_dlkm_dir -o rw -t ext4 || \
				abort "! Failed to mount system_dlkm.img as read-write!"
			extract_system_dlkm_modules_dir=${extract_system_dlkm_dir}/lib/modules
		else
			extract_system_dlkm_modules_dir=${extract_system_dlkm_dir}/${system_dlkm_partition}/lib/modules
		fi
		search_system_dlkm_modules_dir=${extract_system_dlkm_modules_dir}
	fi

	ui_print "- Updating /vendor_dlkm image..."
	cp -f ${home}/_modules/*.ko ${extract_vendor_dlkm_modules_dir}/
	cp -f ${home}/vertmp ${extract_vendor_dlkm_modules_dir}/vertmp
	sync
	if $do_system_dlkm_update; then
		ui_print "- Updating /${system_dlkm_partition} image..."
		collect_shared_system_dlkm_modules ${extract_system_dlkm_modules_dir} ${extract_vendor_dlkm_modules_dir}
		[ -n "$system_dlkm_shared_modules" ] && \
			ui_print "- Also updating shared modules in /${system_dlkm_partition}: $system_dlkm_shared_modules"
		copy_special_modules ${extract_system_dlkm_modules_dir} "$system_dlkm_shared_modules"
		cp -f ${home}/vertmp ${extract_system_dlkm_modules_dir}/vertmp
		for system_dlkm_module in $system_dlkm_copied_modules; do
			[ -s "${extract_system_dlkm_modules_dir}/${system_dlkm_module}" ] || \
				abort "! ${system_dlkm_partition} module copy failed or empty: ${system_dlkm_module}"
		done
		sync
	fi

	if $vendor_dlkm_is_ext4; then
		set_perm 0 0 0644 ${extract_vendor_dlkm_modules_dir}/vertmp
		chcon u:object_r:vendor_file:s0 ${extract_vendor_dlkm_modules_dir}/vertmp
		umount $extract_vendor_dlkm_dir
	else
		cat ${extract_vendor_dlkm_dir}/config/vendor_dlkm_fs_config | grep -q 'lib/modules/vertmp' || \
			echo 'vendor_dlkm/lib/modules/vertmp 0 0 0644' >> ${extract_vendor_dlkm_dir}/config/vendor_dlkm_fs_config
		cat ${extract_vendor_dlkm_dir}/config/vendor_dlkm_file_contexts | grep -q 'lib/modules/vertmp' || \
			echo '/vendor_dlkm/lib/modules/vertmp u:object_r:vendor_file:s0' >> ${extract_vendor_dlkm_dir}/config/vendor_dlkm_file_contexts
		ui_print "- Repacking /vendor_dlkm image..."
		rm -f ${home}/vendor_dlkm.img
		mkfs_erofs ${extract_vendor_dlkm_dir}/vendor_dlkm ${home}/vendor_dlkm.img || \
			abort "! Failed to repack the vendor_dlkm image!"
		rm -rf ${extract_vendor_dlkm_dir}
	fi

	if $do_system_dlkm_update; then
		if $system_dlkm_is_ext4; then
			for system_dlkm_module in ${extract_system_dlkm_modules_dir}/vertmp; do
				[ -f "$system_dlkm_module" ] || continue
				set_perm 0 0 0644 "$system_dlkm_module"
				chcon u:object_r:system_file:s0 "$system_dlkm_module"
			done
			for system_dlkm_module in $system_dlkm_copied_modules; do
				system_dlkm_module=${extract_system_dlkm_modules_dir}/${system_dlkm_module}
				[ -f "$system_dlkm_module" ] || continue
				set_perm 0 0 0644 "$system_dlkm_module"
				chcon u:object_r:system_file:s0 "$system_dlkm_module"
			done
			umount $extract_system_dlkm_dir
		else
			append_erofs_metadata ${extract_system_dlkm_dir} ${system_dlkm_partition} vertmp u:object_r:system_file:s0
			for system_dlkm_module in $system_dlkm_copied_modules; do
				[ -f ${extract_system_dlkm_modules_dir}/${system_dlkm_module} ] || continue
				append_erofs_metadata ${extract_system_dlkm_dir} ${system_dlkm_partition} ${system_dlkm_module} u:object_r:system_file:s0
			done
			ui_print "- Repacking /${system_dlkm_partition} image..."
			rm -f ${home}/system_dlkm.img
			mkfs_erofs ${extract_system_dlkm_dir}/${system_dlkm_partition} ${home}/system_dlkm.img || \
				abort "! Failed to repack the system_dlkm image!"
			rm -rf ${extract_system_dlkm_dir}
		fi
	fi

	unset vendor_dlkm_is_ext4 vendor_dlkm_free_space extract_vendor_dlkm_dir extract_vendor_dlkm_modules_dir \
		system_dlkm_is_ext4 extract_system_dlkm_dir extract_system_dlkm_modules_dir search_vendor_dlkm_modules_dir \
		search_system_dlkm_modules_dir system_dlkm_module system_dlkm_copied_modules system_dlkm_shared_modules \
		system_dlkm_shared_module module_src module_name system_match vendor_match build_prop backup_package \
		backup_images blocklist_expr
#fi

unset skip_update_flag do_backup_flag vendor_dlkm_block system_dlkm_block do_system_dlkm_update

patch_vendor_boot_single_modules


########## CUSTOM END ##########
$vendor_boot_single_module_updated && flash_generic ${vendor_boot_partition}
flash_generic ${vendor_dlkm_partition}
[ -f ${home}/system_dlkm.img ] && flash_generic ${system_dlkm_partition}

# Flash kernel to boot
flash_boot

# Flash DTB to vendor_boot (only if dtb is present)
unzip -o "$ZIPFILE" dtb -d "$home" >/dev/null 2>&1
if [ -f "$home/dtb" ]; then
  ui_print "- Found dtb blob, flashing to vendor_boot..."

  block=${vendor_boot_partition};
  is_slot_device=1;
  ramdisk_compression=auto;
  patch_vbmeta_flag=auto;

  reset_ak;
  dump_boot;

  # Replace existing DTB
  cp -f "$home/dtb" "$split_img/dtb"

  write_boot;
else
  ui_print "! dtb blob not found, skipping vendor_boot flash"
fi

flash_dtbo
