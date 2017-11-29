#!/bin/bash
# Regression test for mkfs.btrfs --rootdir on non-exist file
# Designed behavior is, it should create a new file if destination doesn't exist
# Regression 460e93f25754 ("btrfs-progs: mkfs: check the status of file at mkfs")


source "$TOP/tests/common"

check_prereq mkfs.btrfs

tmp=$(mktemp -d --tmpdir btrfs-progs-mkfs.rootdirXXXXXXX)
run_check "$TOP/mkfs.btrfs" -f -r "$TOP/Documentation/" $tmp/new_file

rm -rf -- "$tmp"
