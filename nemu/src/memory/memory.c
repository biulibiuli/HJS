#include "common.h"
#include <stdlib.h>
#include "burst.h"
#include "cpu/reg.h"
#include "memory/memory.h"
#define BLOCK_SIZE 64
#define STORAGE_SIZE_L1 64*1024
#define STORAGE_SIZE_L2 4*1024*1024
#define EIGHT_WAY 8
#define SIXTEEN_WAY 16

uint32_t dram_read(hwaddr_t, size_t);
void dram_write(hwaddr_t, size_t, uint32_t);

/* Memory accessing interfaces */
void ddr3_read(hwaddr_t, void*);
void ddr3_write(hwaddr_t, void*,uint8_t*);
lnaddr_t seg_translate(swaddr_t, size_t, uint8_t);
hwaddr_t page_translate(lnaddr_t);
int is_mmio(hwaddr_t);
uint32_t mmio_read(hwaddr_t, size_t, int);
void mmio_write(hwaddr_t, size_t, uint32_t, int);
CPU_state cpu;

struct Cache
{
	bool valid;
	int tag;
	uint8_t data[BLOCK_SIZE];
}cache[STORAGE_SIZE_L1/BLOCK_SIZE];
struct SecondaryCache
{
	bool valid,dirty;
	int tag;
	uint8_t data[BLOCK_SIZE];
}cache2[STORAGE_SIZE_L2/BLOCK_SIZE];

void init_cache()
{
	int i;
	time_count = 0;
	for (i = 0;i < STORAGE_SIZE_L1/BLOCK_SIZE;i ++)
	{
		cache[i].valid = false;
		cache[i].tag = 0;
		memset (cache[i].data,0,BLOCK_SIZE);
	}
	for (i = 0;i < STORAGE_SIZE_L2/BLOCK_SIZE;i ++)
	{
		cache2[i].valid = false;
		cache2[i].dirty = false;
		cache2[i].tag = 0;
		memset (cache2[i].data,0,BLOCK_SIZE);
	}
}
uint32_t secondarycache_read(hwaddr_t addr) 
{
	uint32_t g = (addr >> 6) & ((1<<12) - 1); //group number
	uint32_t block = (addr >> 6)<<6;
	int i;
	bool v = false;
	for (i = g * SIXTEEN_WAY ; i < (g + 1) * SIXTEEN_WAY ;i ++)
	{
		if (cache2[i].tag == (addr >> 18)&& cache2[i].valid)
			{
				v = true;
				time_count += 20;
				break;
			}
	}
	if (!v)
	{
		int j;
		//time_count += 200;
		for (i = g * SIXTEEN_WAY ; i < (g + 1) * SIXTEEN_WAY ;i ++)
		{
			if (!cache2[i].valid)break;
		}
		if (i == (g + 1) * SIXTEEN_WAY)//ramdom
		{
			srand (0);
			i = g * SIXTEEN_WAY + rand() % SIXTEEN_WAY;
			if (cache2[i].dirty)
			{
				uint8_t mask[BURST_LEN * 2];
				memset(mask, 1, BURST_LEN * 2);
				for (j = 0;j < BLOCK_SIZE/BURST_LEN;j ++)
				ddr3_write(block + j * BURST_LEN, cache2[i].data + j * BURST_LEN, mask);
			}
		}
		cache2[i].valid = true;
		cache2[i].tag = addr >> 18;
		cache2[i].dirty = false;
		for (j = 0;j < BURST_LEN;j ++)
		ddr3_read(block + j * BURST_LEN , cache2[i].data + j * BURST_LEN);
	}
	return i;
}
uint32_t cache_read(hwaddr_t addr) 
{
	uint32_t g = (addr>>6) & 0x7f; //group number
	//uint32_t block = (addr >> 6)<<6;
	int i;
	bool v = false;
	for (i = g * EIGHT_WAY ; i < (g + 1) * EIGHT_WAY ;i ++)
	{
		if (cache[i].tag == (addr >> 13)&& cache[i].valid)
			{
				v = true;
				time_count += 2;
				break;
			}
	}
	if (!v)
	{
		int j = secondarycache_read (addr);
		for (i = g * EIGHT_WAY ; i < (g+1) * EIGHT_WAY ;i ++)
		{
			if (!cache[i].valid)break;
		}
		if (i == (g + 1) * EIGHT_WAY)//ramdom
		{
			srand (0);
			i = g * EIGHT_WAY + rand() % EIGHT_WAY;
		}
		cache[i].valid = true;
		cache[i].tag = addr >> 13;
		memcpy (cache[i].data,cache2[j].data,BLOCK_SIZE);
	}
	return i;
}

void secondarycache_write(hwaddr_t addr, size_t len,uint32_t data) {
	uint32_t g = (addr >> 6) & ((1<<12) - 1);  //group number
	uint32_t offset = addr & (BLOCK_SIZE - 1); // inside addr
	int i;
	bool v = false;
	for (i = g * SIXTEEN_WAY ; i < (g + 1) * SIXTEEN_WAY ;i ++)
	{
		if (cache2[i].tag == (addr >> 13)&& cache2[i].valid)
			{
				v = true;
				break;
			}
	}
	if (!v)i = secondarycache_read (addr);
	cache2[i].dirty = true;
	memcpy (cache2[i].data + offset , &data , len);
}
void cache_write(hwaddr_t addr, size_t len,uint32_t data) {
	uint32_t g = (addr>>6) & 0x7f; //group number
	uint32_t offset = addr & (BLOCK_SIZE - 1); // inside addr
	int i;
	bool v = false;
	for (i = g * EIGHT_WAY ; i < (g + 1) * EIGHT_WAY ;i ++)
	{
		if (cache[i].tag == (addr >> 13)&& cache[i].valid)
			{
				v = true;
				break;
			}
	}
	if (v)
	{
		memcpy (cache[i].data + offset , &data , len);
	}
	secondarycache_write(addr,len,data);
}

uint32_t hwaddr_read(hwaddr_t addr, size_t len) {
	return dram_read(addr, len) & (~0u >> ((4 - len) << 3));
}

void hwaddr_write(hwaddr_t addr, size_t len, uint32_t data) {
	dram_write(addr, len, data);
}

uint32_t lnaddr_read(lnaddr_t addr, size_t len) {
	return hwaddr_read(addr, len);
}

void lnaddr_write(lnaddr_t addr, size_t len, uint32_t data) {
	hwaddr_write(addr, len, data);
}

uint32_t swaddr_read(swaddr_t addr, size_t len) {
#ifdef DEBUG
	assert(len == 1 || len == 2 || len == 4);
#endif
	return lnaddr_read(addr, len);
}

void swaddr_write(swaddr_t addr, size_t len, uint32_t data) {
#ifdef DEBUG
	assert(len == 1 || len == 2 || len == 4);
#endif
	lnaddr_write(addr, len, data);
}

