/* vm.c: Generic interface for virtual memory objects. */

#include <string.h>
#include "threads/malloc.h"
#include "threads/vaddr.h"
#include "threads/mmu.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include "vm/uninit.h"
#include "vm/file.h"
#include "vm/anon.h"
/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
struct list frame_table;
struct list_elem *clock_ptr;

void vm_init(void)
{
	vm_anon_init();
	vm_file_init();
#ifdef EFILESYS /* For project 4 */
	pagecache_init();
#endif
	register_inspect_intr();
	/* DO NOT MODIFY UPPER LINES. */
	/* TODO: Your code goes here. */

	list_init(&frame_table);
}

/* Get the type of the page. This function is useful if you want to know the
 * type of the page after it will be initialized.
 * This function is fully implemented now. */
enum vm_type
page_get_type(struct page *page)
{
	int ty = VM_TYPE(page->operations->type);
	switch (ty)
	{
	case VM_UNINIT:
		return VM_TYPE(page->uninit.type);
	default:
		return ty;
	}
}

/* Helpers */
static struct frame *vm_get_victim(void);
static bool vm_do_claim_page(struct page *page);
static struct frame *vm_evict_frame(void);

/* Create the pending page object with initializer. If you want to create a
 * page, do not create it directly and make it through this function or
 * `vm_alloc_page`. */
bool vm_alloc_page_with_initializer(enum vm_type type, void *upage, bool writable, vm_initializer *init, void *aux)
{
	ASSERT(VM_TYPE(type) != VM_UNINIT)

	struct supplemental_page_table *spt = &thread_current()->spt;

	if (spt_find_page(spt, upage) == NULL)
	{
		struct page *new_page = (struct page *)malloc(sizeof(struct page));

		switch (VM_TYPE(type))
		{
		case VM_ANON:
			uninit_new(new_page, upage, init, type, aux, anon_initializer);
			break;

		case VM_FILE:
			uninit_new(new_page, upage, init, type, aux, file_backed_initializer);
			break;
		}

		new_page->writable = writable;

		return spt_insert_page(spt, new_page);
	}

	return false;
}

/* Find VA from spt and return page. On error, return NULL. */
struct page *
spt_find_page(struct supplemental_page_table *spt, void *va)
{
	struct page temp_page;
	temp_page.va = pg_round_down(va);

	struct hash_elem *temp_hash_elem = hash_find(&spt->table, &temp_page.hash_elem);
	if (temp_hash_elem == NULL)
		return NULL;

	return hash_entry(temp_hash_elem, struct page, hash_elem);
}

/* Insert PAGE into spt with validation. */
bool spt_insert_page(struct supplemental_page_table *spt, struct page *page)
{
	struct hash_elem *temp_elem = hash_insert(&spt->table, &page->hash_elem);
	if (temp_elem == NULL)
		return true;

	return false;
}

void spt_remove_page(struct supplemental_page_table *spt, struct page *page)
{
	// vm_dealloc_page(page);
	// return true;
}

void list_clock_next(struct list *l)
{
	clock_ptr = clock_ptr->next;
	if (list_tail(l) == clock_ptr)
		clock_ptr = list_begin(l);
}

/* Get the struct frame, that will be evicted. */
static struct frame *vm_get_victim(void)
{
	struct thread *curr = thread_current();
	struct frame *victim = NULL;
	/* TODO: The policy for eviction is up to you. */

	while (1)
	{
		list_clock_next(&frame_table);
		victim = list_entry(clock_ptr, struct frame, elem);

		if (pml4_is_accessed(curr->pml4, victim->page->va))
		{
			pml4_set_accessed(curr->pml4, victim->page->va, false);
			continue;
		}

		break;
	}

	return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame(void)
{
	bool succ = false;
	struct frame *victim = vm_get_victim();

	if (victim->page == NULL)
		return victim;

	struct page *page = victim->page;

	if (swap_out(page) == false)
		return NULL;

	page->frame = NULL;
	victim->page = NULL;

	pml4_clear_page(thread_current()->pml4, page->va);

	return victim;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
static struct frame *
vm_get_frame(void)
{
	struct frame *frame = (struct frame *)malloc(sizeof(struct frame));
	if (frame == NULL)
		return NULL;

	frame->kva = palloc_get_page(PAL_USER);
	if (frame->kva)
		list_push_back(&frame_table, &frame->elem);
	else
		frame = vm_evict_frame();

	clock_ptr = &frame->elem;

	if (frame->kva == NULL)
		PANIC("NO FRAME");

	frame->page = NULL;

	ASSERT(frame != NULL);
	ASSERT(frame->page == NULL);

	return frame;
}

/* Growing the stack. */
static void
vm_stack_growth(void *addr)
{
	if (spt_find_page(&thread_current()->spt, addr))
		return;

	uintptr_t stack_bottom = pg_round_down(addr);
	vm_alloc_page(VM_ANON, stack_bottom, true);
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp(struct page *page UNUSED)
{
}

/* Return true on success */
bool vm_try_handle_fault(struct intr_frame *f, void *addr, bool user, bool write, bool not_present)
{
	// printf("[DEBUG]addr: %p\n", addr);

	struct supplemental_page_table *spt = &thread_current()->spt;
	bool succ = false;

	/* check validation */
	if (is_kernel_vaddr(addr) || addr == NULL)
		return false;

	/* stack growth */
	uintptr_t stack_limit = USER_STACK - (1 << 20);
	uintptr_t rsp = user ? f->rsp : thread_current()->user_rsp;

	if (addr >= rsp - 8 && addr <= USER_STACK && addr >= stack_limit)
		vm_stack_growth(addr);

	/* find page */
	struct page *page = spt_find_page(spt, addr);
	if (page == NULL)
		return false;

	if (!page->writable && write)
		return false;

	/* upload to pysical memory */
	succ = vm_do_claim_page(page);

	// if (page->va > 0x400000 && page->va < 0x600000)
	// {
	// 	printf("[DEBUG][%p]succ: %d\n", addr, succ);
	// 	for (int i = 0; i < 10; i++)
	// 		printf("[DEBUG][%p][%d]%x\n", addr, i, *(int *)(addr + (i * 4)));
	// }
	return succ;
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void vm_dealloc_page(struct page *page)
{
	destroy(page);
	free(page);
}

/* Claim the page that allocate on VA. */
bool vm_claim_page(void *va)
{
	struct page *page = spt_find_page(&thread_current()->spt.table, va);
	if (page == NULL)
		return false;

	return vm_do_claim_page(page);
}

/* Claim the PAGE and set up the mmu. */
static bool
vm_do_claim_page(struct page *page)
{
	struct thread *curr = thread_current();
	struct frame *frame = vm_get_frame();

	/* Set links */
	frame->page = page;
	page->frame = frame;

	if (!pml4_set_page(curr->pml4, page->va, frame->kva, page->writable))
		return false;

	return swap_in(page, frame->kva);
}

unsigned spt_hash(const struct hash_elem *elem, void *aux UNUSED);
static unsigned spt_less(const struct hash_elem *a, const struct hash_elem *b);
void hash_copy(struct hash_elem *hash_elem, void *aux);
void hash_destructor(struct hash_elem *hash_elem, void *aux);

/* Initialize new supplemental page table */
void supplemental_page_table_init(struct supplemental_page_table *spt)
{
	hash_init(&spt->table, spt_hash, spt_less, NULL);
}

/* Copy supplemental page table from src to dst */
bool supplemental_page_table_copy(struct supplemental_page_table *dst, struct supplemental_page_table *src)
{
	struct hash_iterator i;
	struct hash *parent_hash = &src->table;

	hash_first(&i, parent_hash);
	while (hash_next(&i))
	{
		struct page *parent_page = hash_entry(hash_cur(&i), struct page, hash_elem);
		void *aux = parent_page->uninit.aux;

		if (parent_page->operations->type == VM_UNINIT)
		{
			vm_initializer *init = parent_page->uninit.init;

			vm_alloc_page_with_initializer(parent_page->uninit.type, parent_page->va, parent_page->writable, init, aux);
		}
		else
		{
			// VM_MARKER_0: fork에서 실행된 vm_alloc_page -> 인자에 lazy_load_segment와 aux 없음
			vm_alloc_page_with_initializer(page_get_type(parent_page) | VM_MARKER_0, parent_page->va, parent_page->writable, NULL, aux);

			vm_claim_page(parent_page->va);

			struct page *child_page = spt_find_page(dst, parent_page->va);

			memcpy(&child_page->file, &parent_page->file, sizeof(struct file_page));
			memcpy(child_page->frame->kva, parent_page->frame->kva, PGSIZE);
		}
	}

	return true;
}

/* Free the resource hold by the supplemental page table */
void supplemental_page_table_kill(struct supplemental_page_table *spt)
{
	hash_destroy(&spt->table, hash_destructor);
}

/********** project 3: virtaul memory **********/
unsigned spt_hash(const struct hash_elem *elem, void *aux UNUSED)
{
	const struct page *temp_page = hash_entry(elem, struct page, hash_elem);
	return hash_bytes(&temp_page->va, sizeof(temp_page->va));
}

static unsigned spt_less(const struct hash_elem *a, const struct hash_elem *b)
{
	const struct page *page_a = hash_entry(a, struct page, hash_elem);
	const struct page *page_b = hash_entry(b, struct page, hash_elem);

	return page_a->va < page_b->va;
}

void hash_destructor(struct hash_elem *hash_elem, void *aux)
{
	struct page *page = hash_entry(hash_elem, struct page, hash_elem);

	vm_dealloc_page(page);
}

void hash_print(struct hash_elem *hash_elem, void *aux)
{
	struct page *page = hash_entry(hash_elem, struct page, hash_elem);

	printf("[Debug]page->va: %p\n", page->va);
	// printf("[Debug]page->operations->type: %d\n", page->operations->type);
}