/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include <bitmap.h>
#include "threads/vaddr.h"
#include "threads/mmu.h"
#include "vm/vm.h"
#include "devices/disk.h"

#define SECTORS_IN_PAGE PGSIZE / DISK_SECTOR_SIZE

/* DO NOT MODIFY BELOW LINE */
static struct disk *swap_disk;
struct bitmap *swap_table;

static bool anon_swap_in(struct page *page, void *kva);
static bool anon_swap_out(struct page *page);
static void anon_destroy(struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations anon_ops = {
	.swap_in = anon_swap_in,
	.swap_out = anon_swap_out,
	.destroy = anon_destroy,
	.type = VM_ANON,
};

/* Initialize the data for anonymous pages */
void vm_anon_init(void)
{
	uint32_t sip = SECTORS_IN_PAGE;

	swap_disk = disk_get(1, 1);
	int swap_size = disk_size(swap_disk) / sip;
	swap_table = bitmap_create(swap_size);
}

/* Initialize the file mapping */
bool anon_initializer(struct page *page, enum vm_type type, void *kva)
{
	struct uninit_page *uninit = &page->uninit;
	memset(uninit, 0, sizeof(struct uninit_page));

	page->operations = &anon_ops;
	struct anon_page *anon_page = &page->anon;

	// printf("[DEBUG]swap size: %d\n", swap_size);

	return true;
}

/* Swap in the page by read contents from the swap disk. */
static bool
anon_swap_in(struct page *page, void *kva)
{
	struct anon_page *anon_page = &page->anon;
	int page_no = anon_page->swap_idx;

	if (!bitmap_test(swap_table, page_no))
		return false;

	for (int i = 0; i < SECTORS_IN_PAGE; i++)
		disk_read(swap_disk, (page_no * SECTORS_IN_PAGE) + i, kva + (DISK_SECTOR_SIZE * i));

	bitmap_flip(swap_table, page_no);

	// printf("[DEBUG][anon][swap_in]%p\n", page->va);
	return true;
}

/* Swap out the page by writing contents to the swap disk. */
static bool
anon_swap_out(struct page *page)
{
	struct anon_page *anon_page = &page->anon;

	int page_no = bitmap_scan(swap_table, 0, 1, false);
	if (page_no == BITMAP_ERROR)
		return false;

	for (int i = 0; i < SECTORS_IN_PAGE; i++)
		disk_write(swap_disk, (page_no * SECTORS_IN_PAGE) + i, page->va + (DISK_SECTOR_SIZE * i));

	bitmap_flip(swap_table, page_no);

	anon_page->swap_idx = page_no;

	// printf("[DEBUG][anon][swap_out]%p\n", page->va);
	return true;
}
/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void
anon_destroy(struct page *page)
{
	struct anon_page *anon_page = &page->anon;
}
