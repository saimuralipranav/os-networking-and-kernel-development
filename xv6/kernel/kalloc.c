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

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

#define PA_TO_IDX(pa) (((uint64)(pa) - KERNBASE) / PGSIZE)
#define NUM_PHYS_PAGES ((PHYSTOP - KERNBASE) / PGSIZE)

struct {
  struct spinlock lock;
  int count[NUM_PHYS_PAGES];
} ref_counts;

void
kref_inc(void *pa)
{
  if ((uint64)pa < KERNBASE || (uint64)pa >= PHYSTOP)
    panic("kref_inc");
  acquire(&ref_counts.lock);
  ref_counts.count[PA_TO_IDX(pa)]++;
  release(&ref_counts.lock);
}

void
kref_dec(void *pa)
{
  kfree(pa);
}

int
kref_get(void *pa)
{
  if ((uint64)pa < KERNBASE || (uint64)pa >= PHYSTOP)
    panic("kref_get");
  acquire(&ref_counts.lock);
  int cnt = ref_counts.count[PA_TO_IDX(pa)];
  release(&ref_counts.lock);
  return cnt;
}

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  initlock(&ref_counts.lock, "kref");

  acquire(&ref_counts.lock);
  for (int i = 0; i < NUM_PHYS_PAGES; i++) {
    ref_counts.count[i] = 1;
  }
  release(&ref_counts.lock);

  freerange(end, (void *)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char *)PGROUNDUP((uint64)pa_start);
  for (; p + PGSIZE <= (char *)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if (((uint64)pa % PGSIZE) != 0 || (char *)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  acquire(&ref_counts.lock);
  if (ref_counts.count[PA_TO_IDX(pa)] < 1)
    panic("kfree: count < 1");
  ref_counts.count[PA_TO_IDX(pa)]--;
  if (ref_counts.count[PA_TO_IDX(pa)] > 0) {
    release(&ref_counts.lock);
    return;
  }
  release(&ref_counts.lock);

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run *)pa;

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
  if (r) {
    kmem.freelist = r->next;
    acquire(&ref_counts.lock);
    ref_counts.count[PA_TO_IDX(r)] = 1;
    release(&ref_counts.lock);
  }
  release(&kmem.lock);

  if (r)
    memset((char *)r, 5, PGSIZE); // fill with junk
  return (void *)r;
}
