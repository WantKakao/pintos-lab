/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"

/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
void
vm_init (void) {
	vm_anon_init ();
	vm_file_init ();
#ifdef EFILESYS  /* For project 4 */
	pagecache_init ();
#endif
	register_inspect_intr ();
	/* DO NOT MODIFY UPPER LINES. */
	/* TODO: Your code goes here. */
}

/* Get the type of the page. This function is useful if you want to know the
 * type of the page after it will be initialized.
 * This function is fully implemented now. */
enum vm_type
page_get_type (struct page *page) {
	int ty = VM_TYPE (page->operations->type);
	switch (ty) {
		case VM_UNINIT:
			return VM_TYPE (page->uninit.type);
		default:
			return ty;
	}
}

/* Helpers */
static struct frame *vm_get_victim (void);
static bool vm_do_claim_page (struct page *page);
static struct frame *vm_evict_frame (void);

/* Create the pending page object with initializer. If you want to create a
 * page, do not create it directly and make it through this function or
 * `vm_alloc_page`. */
bool
vm_alloc_page_with_initializer (enum vm_type type, void *upage, bool writable,
		vm_initializer *init, void *aux) {

	ASSERT (VM_TYPE(type) != VM_UNINIT)

	struct supplemental_page_table *spt = &thread_current ()->spt;

	/* Check wheter the upage is already occupied or not. */
	if (spt_find_page (spt, upage) == NULL) {
		/* TODO: Create the page, fetch the initialier according to the VM type,
		 * TODO: and then create "uninit" page struct by calling uninit_new. You
		 * TODO: should modify the field after calling the uninit_new. */
		struct page *page = (struct page *)malloc(sizeof(struct page));

		switch (VM_TYPE(type))
		{
		case VM_ANON:
			uninit_new(page, upage, init, type, aux, anon_initializer);
			break;
		case VM_FILE:
			uninit_new(page, upage, init, type, aux, file_backed_initializer);
			break;
		}
		page->write = writable;
		/* TODO: Insert the page into the spt. */
		return spt_insert_page(spt, page);
	}
err:
	return false;
}

/* Find VA from spt and return page. On error, return NULL. */
struct page *
spt_find_page (struct supplemental_page_table *spt UNUSED, void *va UNUSED) {
	struct page *page = NULL;
	/* TODO: Fill this function. */

	struct page p;
	struct hash_elem *e;

	p.va = pg_round_down(va);
	e = hash_find(&spt->pages, &p.hash_elem);
	return e != NULL ? hash_entry(e, struct page, hash_elem) : NULL;
}

/* Insert PAGE into spt with validation. */
bool
spt_insert_page (struct supplemental_page_table *spt UNUSED,
		struct page *page UNUSED) {
	int succ = false;
	/* TODO: Fill this function. */

	if (spt_find_page(spt, page->va))
	{
		return false;
	}
	hash_insert(&spt->pages, &page->hash_elem);
	return true;
}

void
spt_remove_page (struct supplemental_page_table *spt, struct page *page) {
	if (!hash_delete(&spt->pages, &page->hash_elem))
	{
		return;
	}
	vm_dealloc_page(page);
	return true;
}

/* Get the struct frame, that will be evicted. */
static struct frame *
vm_get_victim (void) {
	struct frame *victim = NULL;
	 /* TODO: The policy for eviction is up to you. */

	return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame (void) {
	struct frame *victim UNUSED = vm_get_victim ();
	/* TODO: swap out the victim and return the evicted frame. */

	return NULL;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
static struct frame *
vm_get_frame (void) {
    struct frame *frame = (struct frame *)malloc(sizeof(struct frame));
	/* TODO: Fill this function. */

	frame->kva = palloc_get_page(PAL_USER);
	if (frame->kva == NULL)
	{
		/* have to implement page replacement policy */
		PANIC("todo");
	}
	frame->page = NULL;
	ASSERT(frame != NULL);
	ASSERT(frame->page == NULL);
	return frame;
}

/* Growing the stack. */
static void
vm_stack_growth(void *addr UNUSED)
{
	// bool writable = spt_find_page(thread_current()->spt, addr);
	// vm_alloc_page(VM_ANON, addr, writable);
	/* stack에 해당하는 ANON 페이지를 UNINIT으로 만들고 SPT에 넣어준다.
	이후, 바로 claim해서 물리 메모리와 맵핑해준다. */
	if (vm_alloc_page(VM_ANON | VM_MARKER_0, pg_round_down(addr), 1))
	{
		vm_claim_page(addr);
		thread_current()->stack_bottom -= PGSIZE;
	}
	// vm_alloc_page(VM_ANON | VM_MARKER_0, addr, true);
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp (struct page *page UNUSED) {
}

/* Return true on success */
bool
vm_try_handle_fault (struct intr_frame *f UNUSED, void *addr UNUSED,
		bool user UNUSED, bool write UNUSED, bool not_present UNUSED) {
	struct supplemental_page_table *spt UNUSED = &thread_current ()->spt;
	struct page *page = NULL;
	if (addr == NULL)
		return false;
	/* TODO: Validate the fault */
	/* TODO: Your code goes here */

	if (is_kernel_vaddr(addr) || !is_user_vaddr(addr))
		return false;
	if (not_present)
	{
		void *rsp = f->rsp; // user access인 경우 rsp는 유저 stack을 가리킨다.
		if (!user)			// kernel access인 경우 thread에서 rsp를 가져와야 한다.
			rsp = thread_current()->rsp_stack;

		// 스택 확장으로 처리할 수 있는 폴트인 경우, vm_stack_growth를 호출한다.
		if (USER_STACK - (1 << 20) <= rsp - 8 && rsp - 8 == addr && addr <= USER_STACK)
			vm_stack_growth(addr);
		else if (USER_STACK - (1 << 20) <= rsp && rsp <= addr && addr <= USER_STACK)
			vm_stack_growth(addr);

		page = spt_find_page(spt, addr);
		if (page == NULL)
		{
			return false;
		}
		if (write == 1 && page->write == 0)
		{
			return false;
		}
		return vm_do_claim_page(page);
	}
	return false;
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void
vm_dealloc_page (struct page *page) {
	destroy (page);
	free (page);
}

/* Claim the page that allocate on VA. */
bool
vm_claim_page (void *va UNUSED) {
	struct page *page = NULL;
	/* TODO: Fill this function */
	page = spt_find_page(&thread_current()->spt, va);
	if (page == NULL)
	{
		return false;
	}
	return vm_do_claim_page(page);
}

/* Claim the PAGE and set up the mmu. */
static bool
vm_do_claim_page(struct page *page)
{
	if (!page || !is_user_vaddr(page->va))
	{
		return false;
	}
	struct frame *frame = vm_get_frame();
	/* Set links */
	frame->page = page;
	page->frame = frame;

	/* TODO: Insert page table entry to map page's VA to frame's PA. */
	if (!pml4_set_page(thread_current()->pml4, page->va, frame->kva, page->write))
	{
		return false;
	}
	return swap_in(page, frame->kva);
}

/* Initialize new supplemental page table */
void
supplemental_page_table_init (struct supplemental_page_table *spt UNUSED) {
    hash_init(&spt->pages, page_hash, page_less, NULL);
}

/* Copy supplemental page table from src to dst */
bool
supplemental_page_table_copy (struct supplemental_page_table *dst UNUSED,
								  struct supplemental_page_table *src UNUSED) {
	struct hash_iterator i;

	hash_first(&i, &src->pages);
	while (hash_next(&i))
	{
		struct page *src_page = hash_entry(hash_cur(&i), struct page, hash_elem);
		// enum vm_type type = src_page->operations->type;
		enum vm_type type = page_get_type(src_page);

		if (src_page->operations->type == VM_UNINIT)
		{
			struct uninit_page *uninit_page = &src_page->uninit;
			// struct aux_val *file_loader = (struct aux_val *)uninit_page->aux;

			// struct file_loader* new_file_loader = malloc(sizeof(struct file_loader));
			// memcpy(new_file_loader, uninit_page->aux, sizeof(struct file_loader));
			// new_file_loader->file = file_duplicate(file_loader->file);

			if (!vm_alloc_page_with_initializer(uninit_page->type, src_page->va, src_page->write, uninit_page->init, uninit_page->aux))
				return false;
			// if (!vm_claim_page(src_page->va))
			// 	return false;
		}
		else
		{
			if (!vm_alloc_page(src_page->operations->type, src_page->va, src_page->write))
				return false;
			if (!vm_claim_page(src_page->va))
				return false;
			struct page *dst_page = spt_find_page(dst, src_page->va);
			memcpy(dst_page->frame->kva, src_page->frame->kva, PGSIZE);
		}
	}

	return true;
}

/* Free the resource hold by the supplemental page table */
void
supplemental_page_table_kill (struct supplemental_page_table *spt UNUSED) {
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */
	hash_clear(&spt->pages, hash_action_destroy);
}

void hash_action_destroy(struct hash_elem *hash_elem_, void *aux)
{
	struct page *page = hash_entry(hash_elem_, struct page, hash_elem);

	// if (page != NULL)
	// {
	vm_dealloc_page(page);
	// }
}

/* Returns a hash value for page p. */
unsigned
page_hash(const struct hash_elem *p_, void *aux UNUSED)
{
	const struct page *p = hash_entry(p_, struct page, hash_elem);
	return hash_bytes(&p->va, sizeof p->va);
}

/* Returns true if page a precedes page b. */
bool page_less(const struct hash_elem *a_,
			   const struct hash_elem *b_, void *aux UNUSED)
{
	const struct page *a = hash_entry(a_, struct page, hash_elem);
	const struct page *b = hash_entry(b_, struct page, hash_elem);

	return a->va < b->va;
}
