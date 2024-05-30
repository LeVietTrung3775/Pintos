# Pintos code hacking by sg20180546

## 1. Thread (clear at 9/2)
### 1) Alarm Clock

 <img src = "https://user-images.githubusercontent.com/81512075/186709398-5358a3f6-97f0-4d59-b4ce-08bb008c3270.png" width="700" height="270">
 
![image](https://user-images.githubusercontent.com/81512075/186709495-cb2404d7-e8d9-4ba3-8db9-5acf0da6b30d.png)

- 1 tick = 10 ms = 0.01s
- if timer_sleep, insert into `struct list sleep_list`
- Each timer interrupt, test all elem in sleep list and get thread wake up.
### 2) Scheduler
-  When a thread is added to the ready list that has a higher priority than the currently running thread, the current thread should immediately yield the processor to the new thread.
- nested priority donation
### 3) Advanced Scheduler : Multi Level Feedback Queue Scheduler (mlfqs)
- integer : `priority`, `ready_threads nice`
- fixed point : `recent_cpu`, `load_avg` 
- `priority` = `PRI_MAX` - (`recent_cpu` / 4) - (`nice` * 2)
- `recent_cpu` = (2*`load_avg`)/(2*`load_avg` + 1) * `recent_cpu` + `nice`
- `load_avg` = (59/60) * `load_avg` + (1/60) * `ready_threads`
- Each Timer interrupt (10 ms, 1 tick)
- Every 40 ms (4 tick)
- Every 1 second(1 tick * TIME_FREQ)

- parameter dependecies
<img src = "https://user-images.githubusercontent.com/81512075/203112201-4c916d47-e152-428b-8360-90c283af7918.png" width="400" height="400">
- flow chart
<img src = "https://user-images.githubusercontent.com/81512075/188171945-e76f513b-709a-455a-964b-1f20129e4eef.png" width="500" height="800">
- disable thread_set_priority(int new_priority)
- disable priority donation
- little issue in mlfqs-load-avg

## 2. User Program (clear at 10/2)
### 1) process_wait
- process wait list in child, which is running
- process wait elem in parent, which is waiting for the process exit
- parent blocked and inserted to wait list of running ps
- if running process exit, update exit status for waiting processes and unblock those.
### 2) argument passing by stack (executing)
![image](https://user-images.githubusercontent.com/81512075/196643562-83a90000-bdc9-45d0-8832-60d976dd9a0f.png)

- use intr_frame
- *esp == system call number
- *(esp+i)== system call argument
### 3) system call handler
- Register handler
 
 <img src = "https://user-images.githubusercontent.com/81512075/196631762-e12d0af8-6009-4d0b-90ff-45f475ba17b2.png" width="600" height="100">

- Handling system call
 
 ![image](https://user-images.githubusercontent.com/81512075/196632021-a0b98f89-8ada-4335-a777-51bb73e38059.png)

- linux interrupt vector table (IVT)

 <img src = "https://user-images.githubusercontent.com/81512075/203111903-d99b5351-35e5-4e9d-9a24-365db28feb15.png" width="600" height="600">

fail at novm-oom

## 3. VM (clear at 11/22)
pintos page pool(palloc.c) layout
![image](https://user-images.githubusercontent.com/81512075/203113648-3495d1ec-c463-4270-bf24-b36c3fd7f00e.png)

### 1) Demand Paging
- load_segment : map vm_entry -> ELF file offset , not load file on phys
- Overview of DISK ELF transformed to MEMORY
![image](https://user-images.githubusercontent.com/81512075/203114255-71b7c3a3-e6ac-406d-a4e3-2adb406b96ae.png)


- page_fault : load file by certain file offset at vm entry
- Swapping by Clock Algorithm
    - IA32 3.7.6 : ACCESS,PRESENT / set by HARDWARE, clear by SOFTWARE(OS)
    - When vaddr Access : handle page fault and load(set PTE PRESENT by HARDWARE), insert lru_list -> Hardware set PTE access bit(reference bit) to 1
    - Replacement : traverse lru_list(circular, struct page) and check access bit 
        -> if access bit==1 : set access bit 0; next;
        -> if access bit==0 : evict page(pagedir_clear_page); load file to kaddr; map page(install_page);
        -> if dirty bit ==1 or vme_types==VM_ANON : need to be SWAP OUT, vme types=VM_ANON
### 2) Stack Growing
- Check vaddr is valid stack growing area (pintos ULIMIT: 1MB , pusha : 8 byte low than cur sp)
- new vm entry about new stack area, VM_ANON
- SWAP OUT to SWAP DISK
### 3) Swap Partition
`pintos-mkdisk swap.dsk --swap-size=4`
- IA32 3.7.6 : DIRTY set by HARDWARE, clear by SOFTWARE
- sizeof SECTOR = 512 bytes, sizeof PAGE = 4092 bytes
- If swap in/out , write/read to 8(PGSIZE/BLOCK_SECTOR_SIZE) sector

- Before filesys project, Overview of pintos SATA disk layout
![image](https://user-images.githubusercontent.com/81512075/203112701-0ffc5dee-cde1-441b-85df-110bc64d33d2.png)
- managing (free) swap partition by bitmap
### 4) Memory Mapping files
- Files to memory
- memory needs to be sync to file when unmap, process exited.

fail at page-merge-mm

## 5. Filesys (progressing)

 -------------------------------------
#### command
0. set gcc older version
`sudo update-alternatives --config gcc`

1. pintos gdb
shell1 )
`src/userprog/build $ pintos --gdb -v -k -T 60 --qemu  --filesys-size=2 -p tests/userprog/args-multiple -a args-multiple -- -q  -f run 'args-multiple some arguments for you!'`

shell @)
`src/userprog/build $ gdb kernel.o`
`(gdb) target remote localhost:1234`
`(gdb) continue`

2. threads
`pintos run alarm-multiple`
`gs201@gs201-14Z90N-VR5DK:~/Desktop/pintos/src/threads/build$` `make tests/threads/alarm-multiple.result.`
`gs201@gs201-14Z90N-VR5DK:~/Desktop/pintos/src/threads/build$` `make check`
3. userprog
`pintos-mkdisk filesys.dsk --filesys-size=2`
`pintos -f -q`
`pintos -p ../../examples/echo -a echo -- -q`
`pintos -q run 'echo x'`
`pintos --filesys-size=2 –p ../../examples/echo –a echo -- -f –q run ‘echo x’`

4. vm
src/userprog/Make.vars
`KERNEL_SUBDIRS = threads devices lib lib/kernel userprog filesys vm`
if there is new c file to compile, Makefile.build
`vm_SRC = vm/page.c `


