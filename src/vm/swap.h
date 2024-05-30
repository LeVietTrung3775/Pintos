#ifndef VM_SWAP_H
#define VM_SWAP_H
#include <stddef.h>
#include "vm/page.h"
#define SECTOR_PER_PAGE (PGSIZE)/(BLOCK_SECTOR_SIZE)

#define NOT_IN_SWAP -1
void swap_free(struct kpage_t* page);

void swap_init(void);
void swap_in(struct kpage_t* page);
void swap_out(struct kpage_t* page);


#endif