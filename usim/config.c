/*
 * config.c
 * $Id: config.c 61 2006-04-01 01:25:14Z brad $
 */

#include "usim.h"

#include <string.h>

#include "config.h"

static char *mcrsym_filename;
static char *disk_filename;

const char *
config_get_promsym_filename(void)
{
	return "promh.sym.9";
}

const char *
config_get_mcrsym_filename(void)
{
	if (mcrsym_filename)
		return mcrsym_filename;

	return "ucadr.sym.841";
}

void
config_set_mcrsym_filename(const char *fn)
{
	mcrsym_filename = strdup(fn);
}


const char *
config_get_disk_filename(void)
{
	if (disk_filename)
		return disk_filename;

	return "disk.img";
}

void
config_set_disk_filename(const char *filename)
{
	disk_filename = strdup(filename);
}

