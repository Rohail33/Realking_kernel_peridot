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

resolve_modules_dir() {
	local extract_dir=$1
	local partition_name=$2
	local modules_dir

	for modules_dir in \
		"${extract_dir}/lib/modules" \
		"${extract_dir}/${partition_name}/lib/modules"; do
		[ -d "$modules_dir" ] && {
			echo "$modules_dir"
			return 0
		}
	done

	modules_dir=$(find "$extract_dir" -type d -path "*/lib/modules" | grep -v "/config/" | head -n1)
	[ -n "$modules_dir" ] || return 1
	echo "$modules_dir"
	return 0
}

resolve_all_modules_dirs() {
	local extract_dir=$1
	local partition_name=$2
	local modules_dir all_dirs

	all_dirs=""
	for modules_dir in \
		"${extract_dir}/lib/modules" \
		"${extract_dir}/${partition_name}/lib/modules"; do
		[ -d "$modules_dir" ] || continue
		case " $all_dirs " in
			*" ${modules_dir} "*) ;;
			*) all_dirs="${all_dirs} ${modules_dir}" ;;
		esac
	done

	for modules_dir in $(find "$extract_dir" -type d -path "*/lib/modules" | grep -v "/config/"); do
		case " $all_dirs " in
			*" ${modules_dir} "*) ;;
			*) all_dirs="${all_dirs} ${modules_dir}" ;;
		esac
	done

	[ -n "$all_dirs" ] || return 1
	echo "$all_dirs"
	return 0
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

# Fix unable to mount image as read-write in recovery
$BOOTMODE || setenforce 0

#if $skip_update_flag; then
#else
	# Dump vendor_dlkm partition image
	dd if=${vendor_dlkm_block} of=${home}/vendor_dlkm.img

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
	vendor_dlkm_erofs_root_dir=""
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
	fi
	extract_vendor_dlkm_modules_dir=$(resolve_modules_dir "$extract_vendor_dlkm_dir" "$vendor_dlkm_partition") || \
		abort "! Failed to locate ${vendor_dlkm_partition} modules directory"
	if ! $vendor_dlkm_is_ext4; then
		vendor_dlkm_erofs_root_dir=$(resolve_erofs_root_dir "$extract_vendor_dlkm_dir" "$vendor_dlkm_partition") || \
			abort "! Failed to locate ${vendor_dlkm_partition} erofs root directory"
	fi
	search_vendor_dlkm_modules_dir=${extract_vendor_dlkm_modules_dir}

	ui_print "- Updating /vendor_dlkm image..."
	cp -f ${home}/_modules/*.ko ${extract_vendor_dlkm_modules_dir}/
	cp -f ${home}/vertmp ${extract_vendor_dlkm_modules_dir}/vertmp
	sync

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
		mkfs_erofs ${vendor_dlkm_erofs_root_dir} ${home}/vendor_dlkm.img || \
			abort "! Failed to repack the vendor_dlkm image!"
		rm -rf ${extract_vendor_dlkm_dir}
	fi

	unset vendor_dlkm_is_ext4 vendor_dlkm_free_space extract_vendor_dlkm_dir extract_vendor_dlkm_modules_dir \
		vendor_dlkm_erofs_root_dir search_vendor_dlkm_modules_dir \
		module_src module_name system_match vendor_match build_prop backup_package \
		backup_images blocklist_expr
#fi

unset skip_update_flag do_backup_flag vendor_dlkm_block


########## CUSTOM END ##########
flash_generic ${vendor_dlkm_partition}

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
