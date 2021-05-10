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

#include <math.h>

#include <stdio.h>
#ifndef errno
#include <errno.h>
#endif

#ifdef FREEBSD
#include <sys/sysctl.h>
#endif

#ifdef __linux__
#include <sys/types.h>
#define MEMINFO_FILE "/proc/meminfo"
#endif

#include "memstats.h"
#include "../console.h"

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
		CONS_Debug(DBG_MEMORY, "get_entry: strtol() failed: %s\n", strerror(errno));
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

#ifdef FREEBSD
static size_t freebsd_get_totalmem(void)
{
	unsigned long totalMem, realtotalmem;
	int MIB_hw_physmem[2];
	size_t len;
	
	len = 2;
	sysctlnametomib("hw.physmem", MIB_hw_physmem, &len);
	
	len = sizeof(totalMem);	
	sysctl(MIB_hw_physmem, 2, &(totalMem), &len, NULL, 0);
	
	/* round the memory size to the next power of two, as it's usually in powers of 2 */
	realtotalmem = pow(2, ceil(log(totalMem)/log(2)));

	return realtotalmem;
}

static size_t freebsd_get_freemem(void)
{
	int MIB_vm_stats_vm_v_inactive_count[4];
	int MIB_vm_stats_vm_v_cache_count[4];
	int MIB_vm_stats_vm_v_free_count[4];
	int pageSize;
	size_t len;
	unsigned int inactiveMem, freeMem, cachedMem;
	unsigned long realfreemem;

	len = sizeof(pageSize);
	
 	if (sysctlbyname("vm.stats.vm.v_page_size", &pageSize, &len, NULL, 0) == -1)
		CONS_Debug(DBG_MEMORY, "Cannot get pagesize by sysctl");
	len = 4;	
	sysctlnametomib("vm.stats.vm.v_inactive_count", MIB_vm_stats_vm_v_inactive_count, &len);
	sysctlnametomib("vm.stats.vm.v_cache_count", MIB_vm_stats_vm_v_cache_count, &len);
	sysctlnametomib("vm.stats.vm.v_free_count", MIB_vm_stats_vm_v_free_count, &len);

	len = sizeof(inactiveMem);
	sysctl(MIB_vm_stats_vm_v_inactive_count, 4, &(inactiveMem), &len, NULL, 0);
	inactiveMem *= pageSize;

	len = sizeof(cachedMem);
	sysctl(MIB_vm_stats_vm_v_cache_count, 4, &(cachedMem), &len, NULL, 0);
	cachedMem *= pageSize;

	len = sizeof(freeMem);
	sysctl(MIB_vm_stats_vm_v_free_count, 4, &(freeMem), &len, NULL, 0);
	freeMem *= pageSize;

	realfreemem = (inactiveMem + cachedMem + freeMem);
	return realfreemem;
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
#ifdef FREEBSD
	return freebsd_get_totalmem();
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
#ifdef FREEBSD
	return freebsd_get_freemem();
#endif
	/*	Guess 48 MB. */
	return 48<<20;
}
