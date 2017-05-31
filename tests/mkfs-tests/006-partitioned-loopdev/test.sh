#!/bin/bash
# recognize partitioned loop devices

source $TOP/tests/common

if ! losetup --help | grep -q 'partscan'; then
	_not_run "losetup --partscan not available"
	exit 0
fi

check_prereq mkfs.btrfs

setup_root_helper

cp partition-1g-1g img
loopdev=$(prepare_loop_dev img)
base=$(basename $loopdev)

# expect partitions named like loop0p1 etc
for looppart in $(ls /dev/$base?*); do
	run_check $SUDO_HELPER $TOP/mkfs.btrfs -f $looppart
	run_check $SUDO_HELPER $TOP/btrfs inspect-internal dump-super $looppart
done

# cleanup
cleanup_loop_dev img
rm img
