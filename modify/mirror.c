/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License v2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 */

#include <strings.h>
#include <stdlib.h>
#include <stdbool.h>
#include <limits.h>
#include <getopt.h>
#include <unistd.h>
#include "utils.h"
#include "ctree.h"
#include "kerncompat.h"
#include "help.h"
#include "disk-io.h"
#include "volumes.h"
#include "modify/modify_commands.h"

const char * const modify_mirror_usage[] = {
	"btrfs-modify mirror <options> <device>",
	"Modify specified mirror/parity of a filesystem(unmounted).",
	"<options> are used to specify the destination.",
	"See 'btrfs-modify'(8) for supported options",
	NULL
};

#define STRIPE_UNINITILIZED	(-1)
#define STRIPE_P		(-2)
#define STRIPE_Q		(-3)

static char write_buf[BTRFS_STRIPE_LEN] = { 0 };

static int strtostripe(const char *str)
{
	u32 tmp;

	if (!strncasecmp(str, "p", strlen("p") + 1))
		return STRIPE_P;
	if (!strncasecmp(str, "q", strlen("q") + 1))
		return STRIPE_Q;
	tmp = arg_strtou32(str);
	return (int)tmp;
}

static int write_range_fd(int fd, u64 offset, u64 len)
{
	u64 cur = offset;
	int ret;

	while (cur < offset + len) {
		u64 write_len = min(offset + len - cur, (u64)BTRFS_STRIPE_LEN);
		ret = pwrite(fd, write_buf, write_len, cur);
		if (ret < 0)
			return -errno;
		cur += ret;
	}
	return 0;
}

static int corrupt_mapped_range(struct btrfs_fs_info *fs_info,
				struct btrfs_map_block *map, u64 logical,
				u64 len, int stripe_num)
{
	u64 mirror_profiles = BTRFS_BLOCK_GROUP_RAID1 |
			      BTRFS_BLOCK_GROUP_RAID10 |
			      BTRFS_BLOCK_GROUP_DUP;
	u64 parity_profiles = BTRFS_BLOCK_GROUP_RAID5 |
			      BTRFS_BLOCK_GROUP_RAID6;
	int i;
	int ret;

	/* Check stripe_num with map->profiles */
	if (!(map->type & mirror_profiles) && stripe_num > 0) {
		error("logical range [%llu, %llu) doesn't have extra mirror",
			map->start, map->length);
		return -EINVAL;
	}
	if (stripe_num == STRIPE_P && !(map->type & parity_profiles)) {
		error("logical range [%llu, %llu) doesn't have P stripe",
			map->start, map->length);
		return -EINVAL;
	}
	if (stripe_num == STRIPE_Q && !(map->type & BTRFS_BLOCK_GROUP_RAID6)) {
		error("logical range [%llu, %llu) doesn't have Q stripe",
			map->start, map->length);
		return -EINVAL;
	}

	for (i = 0; i < map->num_stripes; i++) {
		struct btrfs_map_stripe *stripe = &map->stripes[i];

		u64 corrupt_logical = 0;
		u64 corrupt_phy;
		u64 corrupt_len;

		if (stripe_num == STRIPE_P || stripe_num == STRIPE_Q) {
			u64 dest_logical;

			if (stripe_num == STRIPE_P)
				dest_logical = BTRFS_RAID5_P_STRIPE;
			else
				dest_logical = BTRFS_RAID6_Q_STRIPE;
			if (stripe->logical != dest_logical)
				continue;

			/* For P/Q, corrupt the whole stripe */
			corrupt_phy = stripe->physical;
			corrupt_len = stripe->length;
		} else  {
			/* Skip unrelated mirror stripe */
			if (map->type & mirror_profiles && i % 2 != stripe_num)
				continue;

			corrupt_logical = max(stripe->logical, logical);
			corrupt_phy = corrupt_logical - stripe->logical +
					stripe->physical;
			corrupt_len = min(stripe->logical + stripe->length,
					logical + len) - corrupt_logical;
		}
		ret = write_range_fd(stripe->dev->fd, corrupt_phy,
				     corrupt_len);
		if (ret < 0) {
			if (stripe_num == STRIPE_P || stripe_num == STRIPE_Q)
				error(
			"failded to write %s stripe for full stripe [%llu, %llu): %s",
					(stripe_num == STRIPE_P ? "P" : "Q"),
					map->start, map->start + map->length,
					strerror(-ret));
			else
				error(
			"failed to write data for logical range [%llu, %llu): %s",
					corrupt_logical,
					corrupt_logical + corrupt_len,
					strerror(-ret));
			return ret;
		}
	}
	return 0;
}

static int modify_logical(struct btrfs_fs_info *fs_info, u64 logical, u64 len,
			  int stripe)
{
	u32 sectorsize = fs_info->tree_root->sectorsize;
	u64 cur;
	int ret;

	if (!IS_ALIGNED(logical, sectorsize)) {
		error("logical address %llu is not aligned to sectorsize %d",
			logical, sectorsize);
		return -EINVAL;
	}
	if (!IS_ALIGNED(len, sectorsize)) {
		error("length %llu is not aligned to sectorsize %d",
			len, sectorsize);
		return -EINVAL;
	}
	/* Current btrfs only support 1 mirror */
	if (stripe > 1) {
		error("btrfs only supports 1 mirror, stripe number %d is invalid",
			stripe);
		return -EINVAL;
	}

	cur = logical;

	while (cur < logical + len) {
		struct btrfs_map_block *map;

		ret = __btrfs_map_block_v2(fs_info, WRITE, cur,
					   logical + len - cur, &map);
		if (ret < 0) {
			error("failed to map logical range [%llu, %llu): %s",
				cur, logical + len, strerror(-ret));
			return ret;
		}
		ret = corrupt_mapped_range(fs_info, map, cur,
					   logical + len - cur, stripe);
		if (ret < 0) {
			error("failed to modify on-disk data for range [%llu, %llu): %s",
				cur, logical + len, strerror(-ret));
			free(map);
			return ret;
		}
		cur = map->start + map->length;
	}
	return 0;
}

struct root_ino_offset {
	u64 root;
	u64 ino;
	u64 offset;
	bool set;
};

static int modify_root_ino_offset(struct btrfs_fs_info *fs_info,
				  struct root_ino_offset *dest,
				  u64 length, int stripe)
{
	struct btrfs_root *root;
	struct btrfs_key key;
	struct btrfs_path path;
	u32 sectorsize = fs_info->tree_root->sectorsize;
	u64 cur = dest->offset;
	int ret;

	if (!is_fstree(dest->root)) {
		error("rootid %llu is not a valid subvolume id", dest->root);
		return -EINVAL;
	}
	if (!IS_ALIGNED(dest->offset, sectorsize)) {
		error("offset %llu is not aligned to sectorsize %u",
			dest->offset, sectorsize);
		return -EINVAL;
	}

	key.objectid = dest->root;
	key.type = BTRFS_ROOT_ITEM_KEY;
	key.offset = (u64)-1;

	root = btrfs_read_fs_root(fs_info, &key);
	if (IS_ERR(root)) {
		ret = PTR_ERR(root);
		error("failed to read out root %llu: %s",
			dest->root, strerror(-ret));
		return ret;
	}

	btrfs_init_path(&path);
	key.objectid = dest->ino;
	key.type = BTRFS_EXTENT_DATA_KEY;
	key.offset = cur;

	ret = btrfs_search_slot(NULL, root, &key, &path, 0, 0);
	if (ret < 0)
		goto out;
	if (ret > 0) {
		ret = btrfs_previous_item(root, &path, dest->ino,
					  BTRFS_EXTENT_DATA_KEY);
		if (ret < 0)
			goto out;
		if (ret > 0) {
			error("root %llu ino %llu offset %llu not found",
				dest->root, dest->ino, dest->offset);
			ret = -ENOENT;
			goto out;
		}
	}
	while (cur < dest->offset + length) {
		struct extent_buffer *leaf = path.nodes[0];
		struct btrfs_file_extent_item *fi;
		int slot = path.slots[0];
		u64 corrupt_start;
		u64 corrupt_len;

		btrfs_item_key_to_cpu(leaf, &key, slot);
		if (key.objectid != dest->ino ||
		    key.type != BTRFS_EXTENT_DATA_KEY)
			goto out;

		fi = btrfs_item_ptr(leaf, slot, struct btrfs_file_extent_item);
		/* Skip inline extent */
		if (btrfs_file_extent_type(leaf, fi) ==
				BTRFS_FILE_EXTENT_INLINE) {
			cur = key.offset + sectorsize;
			goto next;
		}

		/* Skip unrelated extent */
		if (key.offset + btrfs_file_extent_num_bytes(leaf, fi) <=
				dest->offset) {
			cur = key.offset + btrfs_file_extent_num_bytes(leaf,
					fi);
			goto next;
		}

		/* Skip hole or prealloc extent */
		if (btrfs_file_extent_disk_num_bytes(leaf, fi) == 0 ||
		    btrfs_file_extent_type(leaf, fi) ==
				BTRFS_FILE_EXTENT_PREALLOC) {
			cur = key.offset + btrfs_file_extent_num_bytes(leaf,
						fi);
			goto next;
		}

		/* For compressed extent, corrupt all on-disk data */
		if (btrfs_file_extent_compression(leaf, fi) !=
			BTRFS_COMPRESS_NONE) {
			ret = modify_logical(fs_info,
				btrfs_file_extent_disk_bytenr(leaf, fi),
				btrfs_file_extent_disk_num_bytes(leaf, fi),
				stripe);
			if (ret < 0)
				goto out;
			cur = key.offset +
				btrfs_file_extent_num_bytes(leaf, fi);
			goto next;
		}

		/* Regular plain extents, corrupt given range */
		corrupt_start = btrfs_file_extent_disk_bytenr(leaf, fi) +
			cur - key.offset + btrfs_file_extent_offset(leaf, fi);
		corrupt_len = min(dest->offset + length, key.offset +
				btrfs_file_extent_num_bytes(leaf, fi)) - cur;
		ret = modify_logical(fs_info, corrupt_start, corrupt_len, stripe);
		if (ret < 0)
			goto out;
		cur += corrupt_len;

next:
		ret = btrfs_next_item(root, &path);
		if (ret < 0)
			goto out;
		if (ret > 0) {
			ret = 0;
			goto out;
		}
	}
out:
	btrfs_release_path(&path);
	return ret;
}

static void parse_root_ino_offset(struct root_ino_offset *dest, char *optarg)
{
	char *this_char;
	char *save_ptr = NULL;
	int i = 0;

	for (this_char = strtok_r(optarg, ",", &save_ptr);
	     this_char != NULL;
	     this_char = strtok_r(NULL, ",", &save_ptr)) {
		switch (i) {
		case 0:
			dest->root = arg_strtou64(this_char);
			break;
		case 1:
			dest->ino = arg_strtou64(this_char);
			break;
		case 2:
			dest->offset = arg_strtou64(this_char);
			break;
		default:
			goto error;
		}
		i++;
	}
error:
	if (i != 3) {
		error("--root-ino-offset must be specified in number,number,number form");
		exit(1);
	}
	dest->set = true;
}

int modify_mirror(int argc, char **argv)
{
	struct btrfs_fs_info *fs_info;
	struct root_ino_offset dest = { 0 };
	char *device;
	u64 length = (u64)-1;
	u64 logical = (u64)-1;
	int stripe = STRIPE_UNINITILIZED;
	int ret;

	while (1) {
		int c;
		enum { GETOPT_VAL_LOGICAL = 257, GETOPT_VAL_LENGTH,
			GETOPT_VAL_STRIPE, GETOPT_VAL_ROOT_INO_OFFSET };
		static const struct option long_options[] = {
			{ "logical", required_argument, NULL,
				GETOPT_VAL_LOGICAL },
			{ "length", required_argument, NULL,
				GETOPT_VAL_LENGTH },
			{ "stripe", required_argument, NULL, GETOPT_VAL_STRIPE },
			{ "root-ino-offset", required_argument, NULL,
				GETOPT_VAL_ROOT_INO_OFFSET},
			{ NULL, 0, NULL, 0 }
		};

		c = getopt_long(argc, argv, "", long_options, NULL);
		if (c < 0)
			break;
		switch (c) {
		case GETOPT_VAL_LOGICAL:
			logical = arg_strtou64(optarg);
			break;
		case GETOPT_VAL_LENGTH:
			length = arg_strtou64(optarg);
			break;
		case GETOPT_VAL_STRIPE:
			stripe = strtostripe(optarg);
			break;
		case GETOPT_VAL_ROOT_INO_OFFSET:
			parse_root_ino_offset(&dest, optarg);
			break;
		case '?':
		case 'h':
			usage(modify_mirror_usage);
		}
	}
	if (check_argc_exact(argc - optind, 1))
		usage(modify_mirror_usage);
	device = argv[optind];
	
	ret = check_mounted(device);
	if (ret < 0) {
		error("could not check mount status for device %s: %s",
			device, strerror(-ret));
		return ret;
	}
	if (ret > 0) {
		error("%s is currently mounted, aborting", device);
		return -EINVAL;
	}
	if (logical == (u64)-1 && !dest.set) {
		error("--logical or --root-ino-offset must be specified");
		return 1;
	}
	if (logical != (u64)-1 && dest.set) {
		error("--logical conflicts with --root-ino-offset");
		return 1;
	}
	if (stripe == STRIPE_UNINITILIZED) {
		printf("--stripe not specified, fallback to 0 (1st stripe)\n");
		stripe = 0;
	}

	fs_info = open_ctree_fs_info(device, 0, 0, 0, OPEN_CTREE_WRITES);
	if (!fs_info) {
		error("failed to open btrfs on device %s\n", device);
		return -EIO;
	}
	if (length == (u64)-1) {
		printf("--length not specified, fallback to sectorsize (%d)\n",
			fs_info->tree_root->sectorsize);
		length = fs_info->tree_root->sectorsize;
	}
	if (logical != (u64)-1)
		ret = modify_logical(fs_info, logical, length, stripe);
	else
		ret = modify_root_ino_offset(fs_info, &dest, length, stripe);
	if (ret < 0)
		error("failed to modify btrfs: %s", strerror(-ret));
	else
		printf("Succeeded in modifying specified mirror\n");

	close_ctree(fs_info->tree_root);
	return ret;
}
