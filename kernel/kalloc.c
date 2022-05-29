// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

int reference_remove(uint64 pa);
extern uint64 cas(volatile void *add, int expected, int newval);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

// TODO: Add CAS as we did in the last assignment.

int references[NUM_PYS_PAGES];
// int ref_lock;                 // Lock of the references array. ACTUALLY DON'T NEED THIS.

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  freerange(end, (void*)PHYSTOP);

  // Added:
  // ref_lock = 0;  // Initialize ref_lock. 0 corresponds to an open lock.  DON'T NEED THIS (using cas)
  memset(references, 0, sizeof(int)*NUM_PYS_PAGES);   // Need the 'sizeof(int)??
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by v (should be pa),
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  if (reference_remove((uint64)pa) > 0)     // Other pages are still pointing here (using the physical page).
    return;

  // references[PA_TO_IND(pa)] = 0;
  // int old;
  // do{
  //   old = references[PA_TO_IND(pa)];
  // } while(cas(&references[PA_TO_IND(pa)], old, 0));
  
  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r){
    references[PA_TO_IND(r)] = 1;
    kmem.freelist = r->next;
  }
  release(&kmem.lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}

// Find the amount of references to a physical page in the main memory.
int 
reference_find(uint64 pa)
{
  uint64 pa_d = PGROUNDDOWN(pa);
  return references[PA_TO_IND(pa_d)];
}

// Add one to the reference count of physical page pa.
int
reference_add(uint64 pa)
{
  uint64 pa_d = PGROUNDDOWN(pa);
  int old;
  do{
    old = references[PA_TO_IND(pa_d)];
  } while (cas(&references[PA_TO_IND(pa_d)], old, old+1));
  return references[PA_TO_IND(pa_d)];
}

// Decrease one from the reference count of the physical page pa.
int
reference_remove(uint64 pa)
{
  // uint64 pa_d = PGROUNDDOWN(pa);
  int old;
  do{
    old = references[PA_TO_IND(pa)];
  } while (cas(&references[PA_TO_IND(pa)], old, old-1));
  return references[PA_TO_IND(pa)];
}
