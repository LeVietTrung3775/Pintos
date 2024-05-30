#ifndef VM_PAGE_H
#define VM_PAGE_H
#include "threads/thread.h"
#include "lib/kernel/hash.h"
#include "threads/palloc.h"
#include "threads/vaddr.h"
#include "filesys/off_t.h"
#include "filesys/file.h"
// typedef int32_t off_t;
enum {
    VM_BIN, VM_FILE, VM_ANON
};

struct vm_entry
{
    struct hash_elem h_elem;
    
    uint8_t type;
    void* vaddr;
    bool writable;

    bool loaded_on_phys;
    struct file* file;

    struct list_elem mmap_elem;
    struct mmap_file* mmap_file;
    off_t offset;
    size_t read_bytes;
    size_t zero_bytes;

    block_sector_t swap_sector;
};

struct kpage_t{
    void* kaddr; // physcial
    struct vm_entry* vme;
    struct thread* thread;
    struct list_elem lru_elem;
    struct list_elem elem;
};

struct mmap_file{
    int mapid;
    struct file* file;
    struct list_elem elem;
    struct list vme_list;
};

void vm_init(struct hash* vm);
inline void insert_vme(struct hash* vm, struct vm_entry* vme){
    hash_insert(vm,&vme->h_elem);
}
inline void delete_vme(struct hash* vm, struct vm_entry* vme);
struct vm_entry* find_vme(void* vaddr);
void vm_destroy(struct hash* vm);

struct list_elem* mmap_destroy(struct mmap_file* mmap_file, bool free_vm);

#endif