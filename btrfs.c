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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "volumes.h"
#include "crc32c.h"
#include "commands.h"
#include "utils.h"
#include "help.h"

static const char * const btrfs_short_desc[] = {
	"For an overview of a given command use 'btrfs command --help'",
	"or 'btrfs [command...] --help --full' to print all available options.",
	"Any command name can be shortened as far as it stays unambiguous,",
	"however it is recommended to use full command names in scripts.",
	"All command groups have their manual page named 'btrfs-<group>'.",
	NULL
};

static const char * const btrfs_cmd_group_usage[] = {
	"btrfs [--help] [--version] <group> [<group>...] <command> [<args>]",
	NULL
};

static const char btrfs_cmd_group_info[] =
	"Use --help as an argument for information on a specific group or command.";

static const struct cmd_group btrfs_cmd_group;

static const char * const cmd_help_usage[] = {
	"btrfs help [--full]",
	"Display help information",
	"",
	"--full     display detailed help on every command",
	NULL
};

static int cmd_help(int argc, char **argv)
{
	help_command_group(&btrfs_cmd_group, argc, argv);
	return 0;
}

static const char * const cmd_version_usage[] = {
	"btrfs version",
	"Display btrfs-progs version",
	NULL
};

static int cmd_version(int argc, char **argv)
{
	printf("%s\n", PACKAGE_STRING);
	return 0;
}

static void check_options(int argc, char **argv)
{
	const char *arg;

	if (argc == 0)
		return;

	arg = argv[0];

	if (arg[0] != '-' ||
	    !strcmp(arg, "--help") ||
	    !strcmp(arg, "--version"))
		return;

	fprintf(stderr, "Unknown option: %s\n", arg);
	fprintf(stderr, "usage: %s\n",
		btrfs_cmd_group.usagestr[0]);
	exit(129);
}

static const struct cmd_group btrfs_cmd_group = {
	btrfs_cmd_group_usage, btrfs_cmd_group_info, {
		{ "subvolume", cmd_subvolume, NULL, &subvolume_cmd_group, 0 },
		{ "filesystem", cmd_filesystem, NULL, &filesystem_cmd_group, 0 },
		{ "balance", cmd_balance, NULL, &balance_cmd_group, 0 },
		{ "device", cmd_device, NULL, &device_cmd_group, 0 },
		{ "scrub", cmd_scrub, NULL, &scrub_cmd_group, 0 },
		{ "check", cmd_check, cmd_check_usage, NULL, 0 },
		{ "rescue", cmd_rescue, NULL, &rescue_cmd_group, 0 },
		{ "restore", cmd_restore, cmd_restore_usage, NULL, 0 },
		{ "inspect-internal", cmd_inspect, NULL, &inspect_cmd_group, 0 },
		{ "property", cmd_property, NULL, &property_cmd_group, 0 },
		{ "send", cmd_send, cmd_send_usage, NULL, 0 },
		{ "receive", cmd_receive, cmd_receive_usage, NULL, 0 },
		{ "quota", cmd_quota, NULL, &quota_cmd_group, 0 },
		{ "qgroup", cmd_qgroup, NULL, &qgroup_cmd_group, 0 },
		{ "replace", cmd_replace, NULL, &replace_cmd_group, 0 },
		{ "help", cmd_help, cmd_help_usage, NULL, 0 },
		{ "version", cmd_version, cmd_version_usage, NULL, 0 },
		NULL_CMD_STRUCT
	},
};

int main(int argc, char **argv)
{
	const struct cmd_struct *cmd;
	const char *bname;
	int ret;

	btrfs_config_init();

	if ((bname = strrchr(argv[0], '/')) != NULL)
		bname++;
	else
		bname = argv[0];

	if (!strcmp(bname, "btrfsck")) {
		argv[0] = "check";
	} else {
		argc--;
		argv++;
		check_options(argc, argv);
		if (argc > 0) {
			if (!prefixcmp(argv[0], "--"))
				argv[0] += 2;
		} else {
			usage_command_group_short(&btrfs_cmd_group,
						  btrfs_short_desc);
			exit(1);
		}
	}

	cmd = parse_command_token(argv[0], &btrfs_cmd_group);

	handle_help_options_next_level(cmd, argc, argv);

	crc32c_optimization_init();

	fixup_argv0(argv, cmd->token);

	ret = cmd->fn(argc, argv);

	btrfs_close_all_devices();

	exit(ret);
}
