#ifndef __TLB_H__
#define __TLB_H__

#define TLB_SIZE 64

typedef struct {
	uint32_t tag;
	bool valid;
	uint32_t page;
} TLB;

TLB tlb[TLB_SIZE];

uint32_t tlb_read(uint32_t tag); 
void init_tlb();

void tlb_write(uint32_t tag, uint32_t page); 

#endif