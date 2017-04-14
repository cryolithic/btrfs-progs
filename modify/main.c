/*
 * Copyright (C) 2017 Fujitsu.  All rights reserved.
 *
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
#include <fcntl.h>
#include <unistd.h>
#include <getopt.h>
#include <limits.h>

#include "kerncompat.h"
#include "ctree.h"
#include "volumes.h"
#include "disk-io.h"
#include "transaction.h"
#include "list.h"
#include "utils.h"
#include "help.h"
#include "commands.h"
#include "crc32c.h"
#include "modify/modify_commands.h"

const char * const modify_group_usage[] = {
	"btrfs-modify <command> <dest_options> <device>",
	NULL
};

static const char * const modify_short_desc[] = {
	"For an overview of a given command use 'btrfs-modify command --help'",
	"or 'btrfs-modify [command...] --help --full' to print all available options.",
	"Any command name can be shortened as far as it stays unambiguous,",
	"however it is recommended to use full command names in scripts.",
	"All command groups share the same man page named 'btrfs-modify'.",
	NULL
};

static const char modify_group_info[] =
	"Use --help as an argument for information on a specific group or command.";

static const struct cmd_group modify_cmd_group = {
	modify_group_usage, modify_group_info, {
		{ "mirror", modify_mirror, modify_mirror_usage, NULL, 0 },
		NULL_CMD_STRUCT
	},
};

static void check_options(int argc, char **argv)
{
	const char *arg;

	if (argc == 0)
		return;

	arg = argv[0];

	if (arg[0] != '-' ||
	    !strncmp(arg, "--help", strlen("--help")))
		return;
	fprintf(stderr, "Unknown option: %s\n", arg);
	fprintf(stderr, "usage: %s\n",
		modify_cmd_group.usagestr[0]);
	exit(129);
}

int main(int argc, char **argv)
{
	const struct cmd_struct *command;
	int ret;

	btrfs_config_init();

	set_argv0(argv);
	argc--;
	argv++;

	check_options(argc, argv);
	if (argc == 0) {
		usage_command_group_short(&modify_cmd_group, modify_short_desc);
		exit(1);
	}

	command = parse_command_token(argv[0], &modify_cmd_group);

	handle_help_options_next_level(command, argc, argv);

	crc32c_optimization_init();

	fixup_argv0(argv, command->token);

	ret = command->fn(argc, argv);

	btrfs_close_all_devices();

	exit(!!ret);
}
