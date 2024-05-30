#include "swap.h"
#include "devices/block.h"
#include <bitmap.h>
#include <debug.h>
static struct block* swap_device;
static struct bitmap* swap_free_map;
static struct lock swap_lock;
void swap_out(struct kpage_t* page){
    int i;

    lock_acquire(&swap_lock);    
    block_sector_t sector=bitmap_scan_and_flip(swap_free_map,0,SECTOR_PER_PAGE,false);
    for(i=0;i<SECTOR_PER_PAGE;i++){
        block_write(swap_device,(sector+i),page->kaddr + (i*BLOCK_SECTOR_SIZE)); 
    }
    lock_release(&swap_lock);
    page->vme->swap_sector=sector;

}
void swap_in(struct kpage_t* page){
    int i;
    // ASSERT(page->vme->swap_sector!=-1);
    EXPECT_NE(page->vme->swap_sector,-1);
    lock_acquire(&swap_lock);
    memset(page->kaddr,0,PGSIZE);
    bitmap_set_multiple(swap_free_map,page->vme->swap_sector,SECTOR_PER_PAGE,false);
    for(i=0;i<SECTOR_PER_PAGE;i++){
        block_read(swap_device, page->vme->swap_sector+i ,page->kaddr + (i*BLOCK_SECTOR_SIZE));
    }
    lock_release(&swap_lock);
    page->vme->swap_sector=NOT_IN_SWAP;
}

void swap_free(struct kpage_t* page){
    if(page->vme->swap_sector==NOT_IN_SWAP||page->vme->loaded_on_phys==true){
        return;
    }
    lock_acquire(&swap_lock);
    bitmap_set_multiple(swap_free_map,page->vme->swap_sector,SECTOR_PER_PAGE,false);
    lock_release(&swap_lock);
}

void swap_init(void){
    swap_device=block_get_role(BLOCK_SWAP);
    if (swap_device == NULL)
        PANIC ("No file swap device found, can't initialize file swap.");
    swap_free_map=bitmap_create(block_size(swap_device));
    bitmap_set_all(swap_free_map,false);
    if (swap_free_map == NULL)
        PANIC ("bitmap creation failed--swap device is too large");
    lock_init(&swap_lock);
}