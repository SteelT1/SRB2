// SONIC ROBO BLAST 2
//-----------------------------------------------------------------------------
// Copyright (C) 2014-2020 by Sonic Team Junior
// Copyright (C) 2020 by Victor "SteelT" Fuentes
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  memstats.c
/// \brief Functions to get system memory information.

#include <stdio.h>
#ifndef errno
#include <errno.h>
#endif

#ifdef __linux__
#include <sys/types.h>
#define MEMINFO_FILE "/proc/meminfo"
#endif

#include "memstats.h"

#ifdef __linux__ 
ssize_t numread;
int meminfo_fd = -1;
long Cached;
long MemFree;
long Buffers;
long Shmem;
long MemAvailable = -1;
char procbuf[1024];

/* Parse the contents of /proc/meminfo (in buf), return value of "name"
 * (example: MemTotal) */
static long linux_get_entry(const char* name, const char* buf)
{
	char *memTag;
	size_t totalKBytes;

	long val;
	char* hit = strstr(buf, name);
	if (hit == NULL) {
		return -1;
	}

	errno = 0;
	val = strtol(hit + strlen(name), NULL, 10);
	if (errno != 0) {
		CONS_Debug(DBG_MEMORY, M_GetText("get_entry: strtol() failed: %s\n"), strerror(errno));
		return -1;
	}
	return val;
}

static size_t linux_get_totalmem(void)
{
	size_t totalKB;
	char *memTag;
	#define MEMTOTAL "MemTotal:"

	meminfo_fd = open(MEMINFO_FILE, O_RDONLY);
	numread = read(meminfo_fd, buf, 1023);
	close(meminfo_fd);

	if (numread < 0)
	{
		// Error
		return 0;
	}

	procbuf[numread] = '\0';

	if ((memTag = strstr(buf, MEMTOTAL)) == NULL)
	{
		// Error
		return 0;
	}

	memTag += sizeof (MEMTOTAL);
	totalKB = atoi(memTag);
	return totalKB << 10;
}

static size_t linux_get_freemem(void)
{
	char *memTag;
	long Cached;
	long MemFree;
	long Buffers;
	long Shmem;
	long MemAvailable = -1;
	size_t freeKB;

#define MEMAVAILABLE "MemAvailable:"
#define MEMFREE "MemFree:"
#define CACHED "Cached:"
#define BUFFERS "Buffers:"
#define SHMEM "Shmem:"

	meminfo_fd = open(MEMINFO_FILE, O_RDONLY);
	numread = read(meminfo_fd, buf, 1023);
	close(meminfo_fd);

	if (numread < 0)
	{
		// Error
		return 0;
	}

	procbuf[numread] = '\0';

	/* Kernel is too old to support MEMAVAILABLE 
		so get values using old calculation 
	*/
	if ((memTag = strstr(buf, MEMAVAILABLE)) == NULL)
	{
		Cached = get_entry(CACHED, buf);
		MemFree = get_entry(MEMFREE, buf);
		Buffers = get_entry(BUFFERS, buf);
		Shmem = get_entry(SHMEM, buf);
		MemAvailable = Cached + MemFree + Buffers - Shmem;

		if (MemAvailable == -1)
		{
			// Error
			return 0;
		}
		freeKB = MemAvailable;
	}
	else
	{
		memTag += sizeof(MEMAVAILABLE);
		freeKB = atoi(memTag);
	}

	return freeKB << 10;
}
#endif

#ifdef _WIN32
static size_t win_get_totalmem(void)
{
	MEMORYSTATUS info;
	info.dwLength = sizeof(MEMORYSTATUS);
	GlobalMemoryStatus(&info);
	return info.dwTotalPhys;
}

static size_t win_get_freemem(void)
{
	MEMORYSTATUS info;
	info.dwLength = sizeof(MEMORYSTATUS);
	GlobalMemoryStatus(&info);
	return info.dwAvailPhys;;
}
#endif

size_t GetTotalSysMem(void)
{
#ifdef __linux__
	return linux_get_totalmem();
#endif
#ifdef _WIN32_
	return win_get_totalmem();
#endif
	/*	Guess 48 MB. */
	return 48<<20;
}

size_t GetFreeSysMem(void)
{
#ifdef __linux__
	return linux_get_freemem();
#endif
#ifdef _WIN32_
	return win_get_freemem();
#endif
	/*	Guess 48 MB. */
	return 48<<20;
}
