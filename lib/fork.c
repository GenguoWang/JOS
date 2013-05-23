// implement fork from user space

#include <inc/string.h>
#include <inc/lib.h>

// PTE_COW marks copy-on-write page table entries.
// It is one of the bits explicitly allocated to user processes (PTE_AVAIL).
#define PTE_COW		0x800

//
// Custom page fault handler - if faulting page is copy-on-write,
// map in our own private writable copy.
//
static void
pgfault(struct UTrapframe *utf)
{
	void *addr = (void *) utf->utf_fault_va;
    void *pddr = ROUNDDOWN(addr,PGSIZE);
	uint32_t err = utf->utf_err;
	int r;
	// Check that the faulting access was (1) a write, and (2) to a
	// copy-on-write page.  If not, panic.
	// Hint:
	//   Use the read-only page table mappings at vpt
	//   (see <inc/memlayout.h>).

	// LAB 4: Your code here.
    pte_t pte = vpt[PGNUM(addr)];
    if(!(pte&PTE_COW)||!(err&FEC_WR))
    {
        panic("pgfault error! %x %x %x\n",addr,pte,err);
    }

	// Allocate a new page, map it at a temporary location (PFTEMP),
	// copy the data from the old page to the new page, then move the new
	// page to the old page's address.
	// Hint:
	//   You should make three system calls.
	//   No need to explicitly delete the old page's mapping.

	// LAB 4: Your code here.
	// This is NOT what you should do in your fork.
	if ((r = sys_page_alloc(0, PFTEMP, PTE_P|PTE_U|PTE_W)) < 0)
		panic("pgfault sys_page_alloc: %e\n", r);
	memmove(PFTEMP, pddr, PGSIZE);
	if ((r = sys_page_map(0, PFTEMP, 0, pddr, PTE_P|PTE_U|PTE_W)) < 0)
		panic("pgfault sys_page_map: %e\n", r);
	if ((r = sys_page_unmap(0, PFTEMP)) < 0)
		panic("pgfault sys_page_unmap: %e", r);
	//panic("pgfault not implemented");
    cprintf("%x pgfault handled %x %x %x\n",thisenv->env_id,addr,err,pddr);
}

//
// Map our virtual page pn (address pn*PGSIZE) into the target envid
// at the same virtual address.  If the page is writable or copy-on-write,
// the new mapping must be created copy-on-write, and then our mapping must be
// marked copy-on-write as well.  (Exercise: Why do we need to mark ours
// copy-on-write again if it was already copy-on-write at the beginning of
// this function?)
//
// Returns: 0 on success, < 0 on error.
// It is also OK to panic on error.
//
/*
static void
duppage(envid_t dstenv, unsigned pn)
{
	int r;
    void * addr = (void*)(pn*PGSIZE);
	// This is NOT what you should do in your fork.
	if ((r = sys_page_alloc(dstenv, addr, PTE_P|PTE_U|PTE_W)) < 0)
		panic("sys_page_alloc: %e", r);
	if ((r = sys_page_map(dstenv, addr, 0, UTEMP, PTE_P|PTE_U|PTE_W)) < 0)
		panic("sys_page_map: %e", r);
	memmove(UTEMP, addr, PGSIZE);
	if ((r = sys_page_unmap(0, UTEMP)) < 0)
		panic("sys_page_unmap: %e", r);
}
*/

static int
duppage(envid_t envid, unsigned pn)
{
	int r;
    pte_t pte = vpt[pn];
    void * addr = (void*)(pn*PGSIZE);
    if((pte&PTE_U)&&((pte&PTE_W)||(pte&PTE_COW)))
    {
        //must map the child page first
        //because there write follows, and the page will be fault to a new page
        //the new page can be write, so if the new page map to child, 
        //the child will get a dutty page!!!!
        r=sys_page_map(0,(void*)(pn*PGSIZE),envid,(void*)(pn*PGSIZE),PTE_P|PTE_U|PTE_COW);
        if(r!=0)
            panic("duppage not implemented");
        r=sys_page_map(0,(void*)(pn*PGSIZE),0,(void*)(pn*PGSIZE),PTE_P|PTE_U|PTE_COW);
        if(r!=0) 
            panic("duppage not implemented");
    }
    else if(pte&PTE_U)
    {
        r=sys_page_map(0,(void*)(pn*PGSIZE),envid,(void*)(pn*PGSIZE),PTE_P|PTE_U);
        if(r!=0) return r;
    }
    else
    {
	    panic("duppage not implemented");
        return -1;
    }
	// LAB 4: Your code here.
	//panic("duppage not implemented");
	return 0;
}

//
// User-level fork with copy-on-write.
// Set up our page fault handler appropriately.
// Create a child.
// Copy our address space and page fault handler setup to the child.
// Then mark the child as runnable and return.
//
// Returns: child's envid to the parent, 0 to the child, < 0 on error.
// It is also OK to panic on error.
//
// Hint:
//   Use vpd, vpt, and duppage.
//   Remember to fix "thisenv" in the child process.
//   Neither user exception stack should ever be marked copy-on-write,
//   so you must allocate a new page for the child's user exception stack.
//
void _pgfault_upcall();
envid_t
fork(void)
{
	// LAB 4: Your code here.
	//panic("fork not implemented");
    envid_t envid;
	uint8_t *addr;
	int r;
	extern unsigned char end[];
    set_pgfault_handler(pgfault);
    envid = sys_exofork();
    if(envid<0) return envid;
    if(envid==0)
    {
        thisenv =&envs[ENVX(sys_getenvid())];
        return 0;
    }
	for (addr = (uint8_t*) UTEXT; addr < end; addr += PGSIZE)
		duppage(envid, PGNUM(addr));

	// Also copy the stack we are currently running on.
	duppage(envid, PGNUM(ROUNDDOWN(&addr, PGSIZE)));

    if((r=sys_page_alloc(envid,(void*)(UXSTACKTOP-PGSIZE),PTE_P|PTE_U|PTE_W))!=0)
    {
        panic("fork alloc error %e\n",r);
    }
	// Start the child environment running
    if((r=sys_env_set_pgfault_upcall(envid,_pgfault_upcall))!=0)
    {
        panic("fork set error %e\n",r);
    }
	if ((r = sys_env_set_status(envid, ENV_RUNNABLE)) < 0)
		panic("sys_env_set_status: %e", r);
    return envid;
}

// Challenge!
int
sfork(void)
{
	panic("sfork not implemented");
	return -E_INVAL;
}
