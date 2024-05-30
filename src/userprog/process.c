#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
// #include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
#include "lib/kernel/list.h"
#include "userprog/syscall.h"

#ifdef VM
#include "vm/page.h"
#include "threads/pte.h"
#include "vm/swap.h"
#endif
#define WORD_SIZE 4

#define ALIGNED_PUSH(esp,src,type) {esp-=(char*)WORD_SIZE;\
                              **esp=(type)src;\
                                }
                      

struct list all_list;
struct list lru_list;
struct lock lru_lock;
static struct kpage_t* lru_selected;
void init_lru(){
  list_init(&lru_list);
  lock_init(&lru_lock);
  lru_selected=NULL;
}

void insert_lru_list(struct kpage_t* page){
  lock_acquire(&lru_lock);
  list_push_back(&lru_list,&page->lru_elem);
  lock_release(&lru_lock);
}

void remove_lru_elem(struct kpage_t* page){
  lock_acquire(&lru_lock);
  list_remove(&page->lru_elem);
  lock_release(&lru_lock);
}


struct semaphore* file_handle_lock;

static thread_func start_process NO_RETURN;
static bool load (const char *cmdline, void (**eip) (void), void **esp);
static bool expand_stack();
static inline void parse_elf_name(const char* src,char* dst) {
  size_t i;
  size_t len=strlen(src);
  for(i=0;i<len;++i){
    if(src[i]==' '){
      break;
    }
  }
  strlcpy (dst, src, i+1);

}

static void construct_argument_stack(const char* cmdline,uint32_t** esp) 
{
  char* sp=*esp;

  int i=0;
  int j=0;
  int start_pos;
  int argc=0;
  uint32_t* argv_address;
  char** argv_temp;
  size_t len=strlen(cmdline);

  while(i<len) {
    while(cmdline[i]==' ') {
      i++;
    }
    if(i>=len){
      break;
    }

    argc++;

    while(cmdline[i]!=' ') {
      i++;
    }
  }

  if(argc==0) {
    return;
  }

  argv_address=(uint32_t*)malloc(argc*sizeof(uint32_t));
  argv_temp=(char**)malloc(argc*sizeof(char*));
  i=0;

  while(i<len){
    while(cmdline[i]==' '&&i<len) {
      i++;
    }
    if(i>=len){
      break;
    }
    
    start_pos=i;
    while(cmdline[i]!=' '&&cmdline[i]!=NULL){
      i++;
    }
    size_t size=i-start_pos+1;
    argv_temp[j]=malloc(size*sizeof(char));
    strlcpy(argv_temp[j],&cmdline[start_pos],size);

    j++;
  }

  for(j=argc-1;j>=0;j--) {
 
    size_t size=strlen(argv_temp[j])+1;

    sp-=size;
    strlcpy(sp,argv_temp[j],size);

    argv_address[j]=sp;
  }

  size_t alignment=(uint32_t)sp%4;
  sp-=alignment;
  ASSERT((int)sp%4==0);

  sp-=WORD_SIZE;
  *sp=(uint32_t)0;

  for(j=argc-1;j>=0;j--) {
    sp-=WORD_SIZE;

    *(uint32_t*)sp=(void*)argv_address[j];
  }


  sp-=WORD_SIZE;
  *(uint32_t*)sp=sp+WORD_SIZE;

  sp-=WORD_SIZE;
  *(uint32_t*)sp=argc;

  sp-=WORD_SIZE;
  *(uint32_t*)sp=0;
  *esp=sp;

  for(i=0;i<argc;i++){
    free(argv_temp[i]);
  }
  free(argv_address);
  free(argv_temp);
}


/* Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   thread id, or TID_ERROR if the thread cannot be created. */
static inline bool is_elf_file_exist(const char* name){
  if(is_open_file_executing(name)){
    return true;
  }
  sema_down(file_handle_lock);
  bool ret=lookup(dir_open_root(),name,NULL,NULL);
  sema_up(file_handle_lock);

  return ret;
}
tid_t
process_execute (const char *file_name) 
{

  char *fn_copy;

  struct thread* created;
  char ELF_NAME[1024];

  /* Make a copy of FILE_NAME.
     Otherwise there's a race between the caller and load(). */
  fn_copy = palloc_get_page (0);
  
  if (fn_copy == NULL)
    return TID_ERROR;
  strlcpy (fn_copy, file_name, PGSIZE);
  parse_elf_name(file_name,ELF_NAME);
  if(!is_elf_file_exist(ELF_NAME)){
    return TID_ERROR;
  }

  created = thread_create (ELF_NAME, PRI_DEFAULT, start_process, fn_copy);

  sema_down(&thread_current()->child_sema);
  if (created->tid == TID_ERROR){
    palloc_free_page (fn_copy); 
  }
  if(created->exit_status==-1){
    created->tid=TID_ERROR;
  }
  return created->tid;
}

/* A thread function that loads a user process and starts it
   running. */
static void
start_process (void *file_name_)
{
  char *file_name = file_name_;
  struct intr_frame if_;
  bool success;
  struct thread* cur=thread_current();
  /* Initialize interrupt frame and load executable. */
  memset (&if_, 0, sizeof if_);
#ifdef VM
  vm_init(&cur->vm);
  list_init(&cur->kpage_list);
#endif
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;
  success = load (file_name, &if_.eip, &if_.esp);

  sema_up(&cur->parent->child_sema);

  /* If load failed, quit. */
  palloc_free_page (file_name);
  if (!success) {
    thread_exit ();
  }
  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame
     and jump to it. */

  
  asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (&if_) : "memory");
  NOT_REACHED ();
}

/* Waits for thread TID to die and returns its exit status.  If
   it was terminated by the kernel (i.e. killed due to an
   exception), returns -1.  If TID is invalid or if it was not a
   child of the calling process, or if process_wait() has already
   been successfully called for the given TID, returns -1
   immediately, without waiting.

   This function will be implemented in problem 2-2.  For now, it
   does nothing. */

int
process_wait (tid_t tid) 
{   

  enum intr_level level=intr_disable();

  struct thread* waiting=find_thread_by_tid(tid,&all_list);
  int ret;
  
  if(!waiting||waiting->tid==TID_ERROR){

    return -1;
  }

  struct thread* cur= thread_current();
  
  list_push_back(&waiting->ps_wait_list,&cur->ps_wait_elem);
  sema_up(&waiting->exit_sema);

  
  sema_down(&cur->exit_sema2);
  ret=waiting->exit_status;
  intr_set_level(level);

  return ret;
}

/* Free the current process's resources. */
void
process_exit (void)
{
  struct thread *cur = thread_current ();
  uint32_t *pd;
  struct list_elem* iter;
  struct thread* t_iter;
  struct kpage_t* kp_iter;
  printf("%s: exit(%d)\n",cur->name,cur->exit_status);

  if(cur->executing){

    struct list* open_file_list=&cur->open_file_list;
    struct file* f_iter;
    if(!list_empty(open_file_list)){
      for(iter=list_begin(open_file_list);iter!=list_end(open_file_list);){
        f_iter=list_entry(iter,struct file,elem);
        iter=list_remove(iter);
        file_allow_write(f_iter);
        file_close(f_iter);
      }
    } 
    struct list* free_fd_list=&cur->free_fd_list;
    struct free_fd_elem* ff_iter;
    for(iter=list_begin(free_fd_list);iter!=list_end(free_fd_list);){
      ff_iter=list_entry(iter,struct free_fd_elem,elem);
      iter=list_remove(iter);
      free(ff_iter);
    }
  }
  
  lock_acquire(&lru_lock);
  for(iter=list_begin(&cur->kpage_list);iter!=list_end(&cur->kpage_list);) {
    kp_iter=list_entry(iter,struct kpage_t,elem);
    swap_free(kp_iter);
    if(kp_iter==lru_selected){
      iter=list_next(iter);
      continue;
    }
    list_remove(&kp_iter->lru_elem);
    // if(kp_iter->vme->type==VM_ANON&&!kp_iter->vme->loaded_on_phys){
    //   swap_free(kp_iter);
    // }
    iter=list_remove(iter);
    free(kp_iter);
  }

  all_mmap_destroy(&cur->mmap_list);
  vm_destroy(&cur->vm);
  lock_release(&lru_lock);
  /* Destroy the current process's page directory and switch back
     to the kernel-only page directory. */
  pd = cur->pagedir;
  if (pd != NULL) 
    {
      /* Correct ordering here is crucial.  We must set
         cur->pagedir to NULL before switching page directories,
         so that a timer interrupt can't switch back to the
         process page directory.  We must activate the base page
         directory before destroying the process's page
         directory, or our active page directory will be one
         that's been freed (and cleared). */
      cur->pagedir = NULL;
      pagedir_activate (NULL);
      pagedir_destroy (pd);
    }




  sema_down(&cur->exit_sema);
  for(iter=list_begin(&cur->ps_wait_list);iter!=list_end(&cur->ps_wait_list);iter=list_next(iter)){
    t_iter=list_entry(iter,struct thread,ps_wait_elem);
      sema_up(&t_iter->exit_sema2);
  }
  
}

/* Sets up the CPU for running user code in the current
   thread.
   This function is called on every context switch. */
void
process_activate (void)
{
  struct thread *t = thread_current ();

  /* Activate thread's page tables. */
  pagedir_activate (t->pagedir);

  /* Set thread's kernel stack for use in processing
     interrupts. */
  tss_update ();
}

/* We load ELF binaries.  The following definitions are taken
   from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* For use with ELF types in printf(). */
#define PE32Wx PRIx32   /* Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32   /* Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32   /* Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16   /* Print Elf32_Half in hexadecimal. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
enum elf32_ehdr_e_type_t{
  ET_NONE, // No file type
  ET_REL, // Relocatable file
  ET_EXEC, // Executable file
  ET_DYN, // Shared object file
  ET_CORE, // Core file
  ET_LOPROC=0xff00, // Processor-specific
  ET_HIPRIC=0xffff
};

enum elf32_ehdr_machine_t{
  // 0 No Machine
  EM_M32=1, // AT&T WE 32100
  EM_SPARC, // SPARC
  EM_386, // Intel Arch
  EM_68K, // Motorola 68000
  EM_88K, // Motorola 88000
  EM_860=7, //Intel 80860
  EM_MIPS, // MIPS RS3000 Big-Endian
  EM_MIPS_RS4_BE=10, // MIPS RS4000 Big-Endian
  // 10-16 RESERVED
};

struct Elf32_Ehdr
  {
    unsigned char e_ident[16]; // 16 7f 45 4c 46 02 01 01 00 00 00 00 00 00 00 00 00
    Elf32_Half    e_type;      // 
    Elf32_Half    e_machine;   // 
    Elf32_Word    e_version;   // NONE or CURRENT
    Elf32_Addr    e_entry;     // initial program counter,eip
    Elf32_Off     e_phoff;     // program header offset
    Elf32_Off     e_shoff;     // section header offset
    Elf32_Word    e_flags;
    Elf32_Half    e_ehsize;
    Elf32_Half    e_phentsize; // program header size
    Elf32_Half    e_phnum;
    Elf32_Half    e_shentsize;
    Elf32_Half    e_shnum;      // # of section header
    Elf32_Half    e_shstrndx;
  };

/* Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
struct Elf32_Phdr
  {
    Elf32_Word p_type;
    Elf32_Off  p_offset; // beginning of segment
    Elf32_Addr p_vaddr; // where segment reside in memory
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
  };

struct Elf32_Shdr{
  Elf32_Word sh_name;
  Elf32_Word sh_type;
  Elf32_Word sh_flags;
  Elf32_Addr sh_addr;
  Elf32_Off sh_offset;
  Elf32_Word sh_size;
  Elf32_Word sh_link;
  Elf32_Word sh_info;
  Elf32_Word sh_addralign;
  Elf32_Word sh_entsize;
};

typedef struct {
  Elf32_Word st_name;
  Elf32_Addr st_value;
  Elf32_Word st_size;
  unsigned char st_info;
  unsigned char st_other;
  Elf32_Half st_shndx;
} Elf32_Sym;

/* Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

static bool setup_stack (void **esp);
static bool validate_segment (const struct Elf32_Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */
bool
load (const char *file_name, void (**eip) (void), void **esp) 
{
  
  struct thread *t = thread_current ();
  struct Elf32_Ehdr ehdr;
  struct file *file = NULL;
  off_t file_ofs;
  bool success = false;
  int i;
  char ELF_NAME[1024];
  /* Allocate and activate page directory. */
  t->pagedir = pagedir_create (); // kernel space 4KB
  // printf("pageddir : %p\n",t->pagedir);
  if (t->pagedir == NULL) 
    goto done;
  process_activate ();
  parse_elf_name(file_name,ELF_NAME);
  /* Open executable file. */

  sema_down(file_handle_lock);
  file = filesys_open (ELF_NAME);


  if (file == NULL) 
    {
      printf ("load: %s: open failed\n", ELF_NAME);
      t->exit_status=-1;

      goto done; 
    }else{

    }
  /* Read and verify executable header. */
  if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
      || memcmp (ehdr.e_ident, "\177ELF\1\1\1", 7)
      || ehdr.e_type != ET_EXEC // Executable file flag
      || ehdr.e_machine != EM_386 // Intel Architecture
      || ehdr.e_version != 1
      || ehdr.e_phentsize != sizeof (struct Elf32_Phdr)
      || ehdr.e_phnum > 1024) 
    {
      printf ("load: %s: error loading executable\n", file_name);
      goto done; 
    }

  /* Read program headers. */
  file_ofs = ehdr.e_phoff;
  // printf("e_phnum : %d\n\n",ehdr.e_phnum);
  for (i = 0; i < ehdr.e_phnum; i++) 
    {
      struct Elf32_Phdr phdr;

      if (file_ofs < 0 || file_ofs > file_length (file))
        goto done;
      file_seek (file, file_ofs);

      if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
        goto done;
      file_ofs += sizeof phdr;
      switch (phdr.p_type) 
        {
        case PT_NULL:
        case PT_NOTE:
        case PT_PHDR:
        case PT_STACK:
        default:
          /* Ignore this segment. */
          break;
        case PT_DYNAMIC:
        case PT_INTERP:
        case PT_SHLIB:
          goto done;
        case PT_LOAD:
          if (validate_segment (&phdr, file)) 
            {
              bool writable = (phdr.p_flags & PF_W) != 0;
              uint32_t file_page = phdr.p_offset & ~PGMASK;
              uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
              uint32_t page_offset = phdr.p_vaddr & PGMASK;
              uint32_t read_bytes, zero_bytes;

              if (phdr.p_filesz > 0)
                {
                  /* Normal segment.
                     Read initial part from disk and zero the rest. */
                  read_bytes = page_offset + phdr.p_filesz;
                  zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
                                - read_bytes);
                }
              else 
                {
                  /* Entirely zero.
                     Don't read anything from disk. */
                  read_bytes = 0;
                  zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
                }
              if (!load_segment (file, file_page, (void *) mem_page,
                                 read_bytes, zero_bytes, writable))
                goto done;
            }
          else
            goto done;
          break;
        }
    }

  /* Set up stack. */
  if (!setup_stack (esp))
    goto done;

  construct_argument_stack(file_name,esp);
  /* Start address. */
  
  *eip = (void (*) (void)) ehdr.e_entry;

  success = true;
  t->executing=file;
  file_deny_write(file);
 done:

  sema_up(file_handle_lock);

  /* We arrive here whether the load is successful or not. */



  return success;
}

/* load() helpers. */

static bool install_page (void *upage, void *kpage, bool writable);

/* Checks whether PHDR describes a valid, loadable segment in
   FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Elf32_Phdr *phdr, struct file *file) 
{
  /* p_offset and p_vaddr must have the same page offset. */
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK)) 
    return false; 

  /* p_offset must point within FILE. */
  if (phdr->p_offset > (Elf32_Off) file_length (file)) 
    return false;

  /* p_memsz must be at least as big as p_filesz. */
  if (phdr->p_memsz < phdr->p_filesz) 
    return false; 

  /* The segment must not be empty. */
  if (phdr->p_memsz == 0)
    return false;
  
  /* The virtual memory region must both start and end within the
     user address space range. */
  if (!is_user_vaddr ((void *) phdr->p_vaddr))
    return false;
  if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
    return false;

  /* The region cannot "wrap around" across the kernel virtual
     address space. */
  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
    return false;

  /* Disallow mapping page 0.
     Not only is it a bad idea to map page 0, but if we allowed
     it then user code that passed a null pointer to system calls
     could quite likely panic the kernel by way of null pointer
     assertions in memcpy(), etc. */
  if (phdr->p_vaddr < PGSIZE)
    return false;

  /* It's okay. */
  return true;
}

/* Loads a segment starting at offset OFS in FILE at address
   UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
   memory are initialized, as follows:

        - READ_BYTES bytes at UPAGE must be read from FILE
          starting at offset OFS.

        - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.

   The pages initialized by this function must be writable by the
   user process if WRITABLE is true, read-only otherwise.

   Return true if successful, false if a memory allocation error
   or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable) 
{
  struct thread* t=thread_current();
  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (ofs % PGSIZE == 0);
  struct vm_entry* vme;
  struct file* reopen_file=file_reopen(file);
  list_push_back(&t->open_file_list,&reopen_file->elem);
  file_seek (file, ofs);
  while (read_bytes > 0 || zero_bytes > 0) 
    {
      vme=malloc(sizeof *vme);
      if(vme==NULL){
        // printf("vme malloc fail\n");
        return false;
      }

      /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;

/////////////////////////////////////////////////////////////
      vme->file=reopen_file;
      vme->read_bytes=read_bytes < PGSIZE ? read_bytes : PGSIZE;
      vme->zero_bytes=PGSIZE - page_read_bytes;
      vme->offset=ofs;
      vme->vaddr=upage;
      vme->loaded_on_phys=false;
      vme->writable=writable;
      vme->type=VM_BIN;
      ofs+=PGSIZE;
      insert_vme(&t->vm,vme);

/////////////////////////////////////////////////////////////
      /* Get a page of memory. */
      // uint8_t *kpage = palloc_get_page (PAL_USER);
      // if (kpage == NULL)
      //   return false;

      // /* Load this page. */
      // if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes)
      //   {
      //     palloc_free_page (kpage);
      //     return false; 
      //   }
      // memset (kpage + page_read_bytes, 0, page_zero_bytes);

      // /* Add the page to the process's address space. */

      // if (!install_page (upage, kpage, writable)) 
      //   {
      //     palloc_free_page (kpage);
      //     return false; 
      //   }
/////////////////////////////////////////////////////////////

      /* Advance. */
      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      upage += PGSIZE;
    }
  return true;
}

/* Create a minimal stack by mapping a zeroed page at the top of
   user virtual memory. */
static bool
setup_stack (void **esp) 
{
  bool success = false;
  success=expand_stack(PHYS_BASE-PGSIZE);
  if (success){
    *esp = PHYS_BASE;
  }
  return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();
  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}

void bf(){
  // printf("breakpoint\n");
}
static uint32_t* demand_paging(void){

  struct list_elem* iter;
  struct kpage_t* kp_iter;
  void* kaddr;
  void* vaddr;
  uint32_t * pte;
  int i=0;
  kaddr=palloc_get_page(PAL_USER|PAL_ZERO);
  if(kaddr!=NULL){
    return kaddr;
  }

  while(1){
    for(iter=list_begin(&lru_list);iter!=list_end(&lru_list);iter=list_next(iter)){
      kp_iter=list_entry(iter,struct kpage_t,lru_elem);
      ASSERT(kp_iter->vme->loaded_on_phys==true);
      kaddr=kp_iter->kaddr;
      vaddr=kp_iter->vme->vaddr;
      pte=lookup_page(kp_iter->thread->pagedir,vaddr,false);

      i++;
      if(kp_iter==lru_selected){
        continue;
      }
      if(*pte&(uint32_t)PTE_A){
        *pte&=~(uint32_t)PTE_A;
        invalidate_pagedir(kp_iter->thread->pagedir);
        continue;
      }
      lru_selected=kp_iter;
      if(kp_iter->vme->type==VM_FILE&&*pte&PTE_D){
        lock_acquire(file_handle_lock);
        file_write_at(kp_iter->vme->file,kp_iter->vme->vaddr,PGSIZE,kp_iter->vme->offset);
        lock_release(file_handle_lock);
        *pte&=~PTE_D;
      }
      else if(*pte&PTE_D||kp_iter->vme->type==VM_ANON){
          *pte&=~PTE_D; // clear DIRTY BIT
          kp_iter->vme->type=VM_ANON; // type is now anon
          swap_out(kp_iter);
      }

      list_remove(&kp_iter->elem);
      list_remove(&kp_iter->lru_elem);

      *pte&=~PTE_P;
      invalidate_pagedir(kp_iter->thread->pagedir);
      kp_iter->vme->loaded_on_phys=false;

      memset(kaddr,0,PGSIZE);
      free(kp_iter);

      return kaddr;
    }

  }
  NOT_REACHED();
}

static inline bool is_stack_boundary(uint32_t* sp,void* uaddr){
  if(sp-8<=uaddr && uaddr>=LOADER_PHYS_BASE-ULIMIT && uaddr<=PHYS_BASE){
    return true;
  }
  return false;
}

static bool expand_stack(uint32_t* uaddr){
  uint32_t* round_down_uaddr=pg_round_down(uaddr);
  struct vm_entry* vme;
  struct kpage_t* page;
  struct thread* cur=thread_current();

  uint8_t *kpage;
  vme=malloc(sizeof* vme);
  if(vme==NULL){
    return false;
  }
  page=malloc(sizeof* page);
  if(page==NULL){
    return false;
  }
  vme->type=VM_ANON;
  vme->writable=true;
  vme->loaded_on_phys=true;
  vme->swap_sector=NOT_IN_SWAP;
  vme->vaddr=round_down_uaddr;
  page->vme=vme;
  page->thread=cur;

  lock_acquire(&lru_lock);
  kpage=demand_paging();
  ASSERT(kpage!=NULL);
  lock_release(&lru_lock);

  page->kaddr=kpage;

  if(!install_page(round_down_uaddr,kpage,true)){
    printf(" isntall page error\n");
    free(page);
    free(vme);
    palloc_free_page(kpage);
    return false;
  }
  insert_vme(&cur->vm,vme);

  lock_acquire(&lru_lock);
  list_push_back(&cur->kpage_list,&page->elem);
  list_push_back(&lru_list,&page->lru_elem);
  lock_release(&lru_lock);
  return true;
}

bool handle_mm_fault(uint32_t* uaddr,uint32_t* sp){

   struct thread* cur=thread_current();
   uint32_t* round_down_uaddr=pg_round_down(uaddr);
   struct vm_entry* vme=find_vme(round_down_uaddr);
   struct kpage_t* page;
   uint8_t *kpage;
   if(vme==NULL){
      /*
        IF STACK
          ALLOCATE NEW
        ELSE GOTO ERROR
      */
      if(is_stack_boundary(sp,uaddr)){
        if(expand_stack(uaddr)){
          return true;
        }
      }
      goto error;
   }
   if(vme->loaded_on_phys){
    goto error;
   }

    lock_acquire(&lru_lock);
    kpage=demand_paging();
    lock_release(&lru_lock);
    page=malloc(sizeof* page);

  if(page==NULL){
    palloc_free_page(kpage);
    goto error;
  }
   page->vme=vme;
   page->kaddr=kpage;

    switch (vme->type)
    {
    case VM_FILE:
    case VM_BIN:

      file_seek(vme->file,vme->offset);
      EXPECT_EQ(vme->file->pos,vme->offset);
      size_t file_read_size=file_read(vme->file,page->kaddr,vme->read_bytes);
      // if(vme->type==VM_FILE)
      // EXPECT_NE(file_read_size,vme->read_bytes);
      if( file_read_size!=(int)vme->read_bytes){
          goto error;
      }
      break;
    case VM_ANON:
      swap_in(page);
      break;

    default:
      break;
    }



   if(!install_page(round_down_uaddr,kpage,vme->writable)){
      palloc_free_page(kpage);
      goto error;
   }

   vme->loaded_on_phys=true;
   page->vme=vme;
   page->kaddr=kpage;
   page->thread=cur;
   ASSERT(page->vme->vaddr!=NULL);
   lock_acquire(&lru_lock);
   list_push_back(&lru_list,&page->lru_elem);
   list_push_back(&cur->kpage_list,&page->elem);
   lock_release(&lru_lock);
   return true;
error:
  // printf("exit here\n");
   thread_current()->exit_status=-1;
   thread_exit();
  //  return false;
}