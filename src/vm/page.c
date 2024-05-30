#include "page.h"
#include "threads/pte.h"
#include "threads/interrupt.h"
struct hash lru_list;

static unsigned vm_hash_func(const struct hash_elem* helem,void* aux UNUSED){
    struct vm_entry* vm_entry=hash_entry(helem,struct vm_entry,h_elem);

    return hash_int(pg_round_down(vm_entry->vaddr));
}

static bool vm_less_func(const struct hash_elem* a, const struct hash_elem* b, void* aux UNUSED){
    struct vm_entry* va=hash_entry(a,struct vm_entry,h_elem);
    struct vm_entry* vb=hash_entry(b,struct vm_entry,h_elem);
    if(vb->vaddr>va->vaddr){
        return true;
    }
    return false;
}


void vm_init(struct hash* vm){
    hash_init(vm,vm_hash_func,vm_less_func,NULL);
}


// inline void insert_vme(struct hash* vm, struct vm_entry* vme){
//     hash_insert(vm,&vme->h_elem);
// }
inline void delete_vme(struct hash* vm, struct vm_entry* vme){
    hash_delete(vm,&vme->h_elem);
}

struct vm_entry* find_vme(void* vaddr){

    struct thread* cur=thread_current();
    struct vm_entry src;
    src.vaddr=pg_round_down(vaddr);
    return hash_entry(hash_find(&cur->vm,&src.h_elem),struct vm_entry,h_elem);
}

static void destroy_vme(struct hash_elem *e, void *aux UNUSED){

    struct vm_entry* vme=hash_entry(e,struct vm_entry,h_elem);
    free(vme);
}

void vm_destroy(struct hash* vm){
    // struct hash_iterator h_iter;
    // hash_first(&h_iter,vm);

    // struct file* file=list_entry(&h_iter.elem->list_elem,struct file,elem);
    // file_close(file);
    hash_destroy(vm,destroy_vme);
}

void all_mmap_destroy(struct list* mmap_list){
    struct list_elem* iter;
    // struct list_elem* inner_iter;
    struct mmap_file* mm_iter;
    // struct vm_entry* vm_iter;
    for(iter=list_begin(mmap_list);iter!=list_end(mmap_list);){
        mm_iter=list_entry(iter,struct mmap_file,elem);
        // for(inner_iter=list_begin(&mm_iter->vme_list);inner_iter!=list_end(&mm_iter->vme_list);){
        //     vm_iter=list_entry(inner_iter,struct vm_entry,mmap_elem);
        //     // free(vm_iter);
        // }
        // iter=list_remove(iter);
        iter=mmap_destroy(mm_iter,false);
        free(mm_iter);
    }
}

struct list_elem* mmap_destroy(struct mmap_file* mmap_file,bool free_vm){
    struct list_elem* iter;
    struct vm_entry* vm_iter;
    struct thread* cur=thread_current();
    uint32_t* pte;
    
    for(iter=list_begin(&mmap_file->vme_list);iter!=list_end(&mmap_file->vme_list);){
        vm_iter=list_entry(iter,struct vm_entry,mmap_elem);
        
        // hash_delete(&cur->vm,vm_iter);
        iter=list_remove(iter);

        pte = lookup_page (cur->pagedir, vm_iter->vaddr, false);
        // void* kaddr= pg_round_down(*pte);
        // file_write_at(vm_iter->file,vm_iter->vaddr,vm_iter->read_bytes,vm_iter->offset);
        if (pte != NULL && (*pte & PTE_P) != 0){
            if(*pte&PTE_D){
                // if(intr_get_level()==INTR_ON)
                 file_write_at(vm_iter->file,vm_iter->vaddr,vm_iter->read_bytes,vm_iter->offset);
                // else{
                    // intr_set_level(INTR_ON);
                    // file_write_at(vm_iter->file,pg_round_down(*pte),vm_iter->read_bytes,vm_iter->offset);
                    // intr_set_level(INTR_OFF);
                // }
            }
            *pte &= ~PTE_P;
        }
        // if(free_vm){

            // free(vm_iter);
        delete_vme(&cur->vm,vm_iter);
        free(vm_iter);
        // }
    }

    
    invalidate_pagedir (cur->pagedir);
    return list_remove(&mmap_file->elem);
}

