#!/bin/bash
# Under certain power loss case, btrfs quota tree can be initialized but
# rescan not kicked in. (can be reproduced in low possibility in btrfs/166).
#
# This test case is to ensure for such special case, btrfs check doesn't report
# such qgroup difference as error, thus no false alert for btrfs/166.

source "$TEST_TOP/common"

check_prereq btrfs

check_image() {
	run_check "$TOP/btrfs" check "$1"
}

check_all_images
