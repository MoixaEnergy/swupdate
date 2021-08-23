/*
 * (C) Copyright 2017
 * Stefano Babic, DENX Software Engineering, sbabic@denx.de.
 *
 * SPDX-License-Identifier:     GPL-2.0-only
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/file.h>
#include <dirent.h>
#include "generated/autoconf.h"
#include "util.h"
#include "bootloader.h"

#include <libuboot.h>
#ifndef CONFIG_UBOOT_DEFAULTENV
#define CONFIG_UBOOT_DEFAULTENV	"/etc/u-boot-initial-env"
#endif

#define ALMP_MODE (1)

#ifdef ALMP_MODE

#define MAX_NUM_LINES (256)
#define MAX_LINE_DATA (4096)
static char* filebuf[MAX_NUM_LINES];
static int linenum = 0;
static bool init = false;
static char applylist[MAX_LINE_DATA];
static char applyname[256];
static int numlines = 0;

static int bootloader_initialize(char* dummy)
{
	int init = 0;
	FILE* fin = fopen(CONFIG_UBOOT_DEFAULTENV, "r");
	if (!fin)
	{
		ERROR("can't open input file: %s", CONFIG_UBOOT_DEFAULTENV);
		init = 1;
	}
	else
	{
		for (int x = 0; x < MAX_NUM_LINES; x++)
		{
			filebuf[x] = NULL;
		}

		linenum = 0;
		bool done = false;

		while (!done)
		{
			filebuf[linenum] = malloc(MAX_LINE_DATA);

			if (!fgets(filebuf[linenum], MAX_LINE_DATA, fin))
			{
				// end of file
				done = true;
			}
			else				
			{
				//printf("x:%d\t%s", linenum, filebuf[linenum]);
				linenum++;
			}
		}
		fclose(fin);
	}
	return (init);	
}

void bootloader_write_env_file(void)
{
	FILE *fout = fopen(CONFIG_UBOOT_DEFAULTENV, "wt");
	if (!fout)
	{
		ERROR("can't open output file: %s", CONFIG_UBOOT_DEFAULTENV);
	}
	else
	{
		for (int x = 0; x < linenum; x++)
		{
			fprintf(fout, "%s", filebuf[x]);
			free(filebuf[x]);
		}
		fclose(fout);
	}
}

int bootloader_env_set(const char *name, const char *value)
{
	int ret = 1;
	if (bootloader_initialize(NULL) == 0)
	{
		int x = 0;
		int y = 0;
		bool done = false;
		for (x = 0; x < linenum && !done; x++)
		{
			if (strncmp(filebuf[x], name, strlen(name)) == 0)
			{
				INFO("found %s", name);
				// match, find the = 
				char *ptr = strchr(filebuf[x], '=');
				if (ptr != NULL)
				{
					if (value == NULL)
					{
						// doing an unset
						*(ptr+1) = '\0';
						INFO("setting to NULL");
					}
					else
					{
						// setting to something
						memset((void*)ptr+1, 0, strlen(filebuf[x])-(ptr - filebuf[x]));
						strncat(ptr+1, value, strlen(value));
						INFO("setting to %s", value);
					}
					done = true;
					ret = 0;
					bootloader_write_env_file();
				}
			}
		}
		if (!done)
		{
			INFO("Env set adding %s = %s", name, value);
			sprintf(filebuf[linenum], "%s=%s", name, value);
			++linenum;
			bootloader_write_env_file();
			ret = 0;
		}
	}
	return (ret);
}

int bootloader_env_unset(const char *name)
{
	INFO("Env unset %s", name);
	return bootloader_env_set(name, NULL);
}

char *bootloader_env_get(const char *name)
{
	char *ret = NULL;
	if (bootloader_initialize(NULL) == 0)
	{
		int x = 0;
		int y = 0;
		bool done = false;
		for (x = 0; x < linenum && !done; x++)
		{
			if (strncmp(filebuf[x], name, strlen(name)) == 0)
			{
				INFO("found %s", name);
				// match, find the = 
				char *ptr = strchr(filebuf[x], '=');
				if (ptr != NULL)
				{
					ret = ptr+1;
					done = true;
					INFO("returning %s", ptr);
				}
			}
		}
	}
	return (ret);
}

int bootloader_apply_list(const char *filename)
{
	int init = 0;
	size_t line_size;
	char *line_buf = NULL;
	size_t line_buf_size = 0;
	char *ptr = NULL;
	
	FILE* fapply = fopen(filename, "r");
	numlines = 0;
	fapply = fopen(filename, "r");
	if (!fapply)
	{
		ERROR("Apply list can't open input file: %s", filename);
		init = 1;
	}
	else
	{
		line_size = getline(&line_buf, &line_buf_size, fapply);
		if (line_size >= 3)
		{
			// got a line
			ptr = strchr(line_buf, '=');
			memset(applyname, 0, 256);
			int namelen = ptr - line_buf;
			for (int x = 0; x < namelen; x++)
			{
				applyname[x] = line_buf[x];
			}
			bootloader_env_set(applyname, ptr + 1);
  			//line_size = getline(&line_buf, &line_buf_size, fapply);
		}

		if (line_buf)
			free(line_buf);
		
		fclose(fapply);
	}
	return (init);	
}

#else
static int bootloader_initialize(struct uboot_ctx **ctx)
{
	if (libuboot_initialize(ctx, NULL) < 0) {
		ERROR("Error: environment not initialized");
		return -ENODEV;
	}
	if (libuboot_read_config(*ctx, CONFIG_UBOOT_FWENV) < 0) {
		ERROR("Configuration file %s wrong or corrupted", CONFIG_UBOOT_FWENV);
		return -EINVAL;
	}
	if (libuboot_open(*ctx) < 0) {
		WARN("Cannot read environment, using default");
		if (libuboot_load_file(*ctx, CONFIG_UBOOT_DEFAULTENV) < 0) {
			ERROR("Error: Cannot read default environment from file");
			return -ENODATA;
		}
	}

	return 0;
}

int bootloader_env_set(const char *name, const char *value)
{
	int ret;
	struct uboot_ctx *ctx = NULL;

	ret = bootloader_initialize(&ctx);
	if (!ret) {
		libuboot_set_env(ctx, name, value);
		ret = libuboot_env_store(ctx);
	}

	libuboot_close(ctx);
	libuboot_exit(ctx);

	return ret;
}

int bootloader_env_unset(const char *name)
{
	return bootloader_env_set(name, NULL);
}


int bootloader_apply_list(const char *filename)
{
	int ret;
	struct uboot_ctx *ctx = NULL;

	ret = bootloader_initialize(&ctx);
	if (!ret) {
		libuboot_load_file(ctx, filename);
		ret = libuboot_env_store(ctx);
	}

	libuboot_close(ctx);
	libuboot_exit(ctx);

	return ret;
}

char *bootloader_env_get(const char *name)
{
	int ret;
	struct uboot_ctx *ctx = NULL;
	char *value = NULL;

	ret = bootloader_initialize(&ctx);
	if (!ret) {
		value = libuboot_get_env(ctx, name);
	}
	libuboot_close(ctx);
	libuboot_exit(ctx);

	return value;
}
#endif
