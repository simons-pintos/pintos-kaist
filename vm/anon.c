/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include "vm/vm.h"
#include "devices/disk.h"
#include "lib/kernel/bitmap.h"
#include "threads/vaddr.h"
#include "threads/mmu.h"

/* DO NOT MODIFY BELOW LINE */
static struct disk *swap_disk;
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

/* Project 3 : Swapping in & out */
/* SECTOR_PER_PAGE = PGSIZE(4096bytes) / DISK_SECTOR_SIZE(512bytes) => 8 SECTOR */
struct bitmap *swap_table;
const size_t SECTOR_PER_PAGE = (PGSIZE / DISK_SECTOR_SIZE);
static struct disk *swap_disk;

/* Initialize the data for anonymous pages */
void vm_anon_init(void)
{
	/* TODO: Set up the swap_disk. */
	swap_disk = disk_get(1, 1); // swap_disk
	size_t swap_slot_cnt =  disk_size(swap_disk) / SECTOR_PER_PAGE; 

	swap_table = bitmap_create(swap_slot_cnt);
}

/* Initialize the file mapping */
bool anon_initializer(struct page *page, enum vm_type type, void *kva)
{
	struct uninit_page *uninit = &page->uninit;
	memset(uninit, 0, sizeof(struct uninit_page));

	/* Set up the handler */
	page->operations = &anon_ops;
	struct anon_page *anon_page = &page->anon;

	// anon_page->test = 4;

	return true;
}

/* Swap in the page by read contents from the swap disk. */
static bool
anon_swap_in(struct page *page, void *kva)
{
	struct anon_page *anon_page = &page->anon;

	/* page의 swap_slot_number를 불러들임 */
	size_t swap_slot_idx = anon_page->swap_slot_no;
	
	/* 해당 swap_slot이 false인지 확인*/
	if (bitmap_test(swap_table, swap_slot_idx) == false){
		return false;
	}

	/* disk -> kva로 페이지에 대한 내용을 써줌 */
	for(int i = 0; i < SECTOR_PER_PAGE; i++){
		disk_read(swap_disk, swap_slot_idx * SECTOR_PER_PAGE + i, kva + (DISK_SECTOR_SIZE * i));}

	/* bitmap 및 pml4에 대한 내용을 갱신*/
	bitmap_set(swap_table, swap_slot_idx, false);
	// pml4_set_page(thread_current()->pml4, page->va, kva, page->writable);

	return true;
}

/* Swap out the page by writing contents to the swap disk. */
static bool
anon_swap_out(struct page *page)
{
	struct anon_page *anon_page = &page->anon;
	
	/* swap table에서 swap_slot_idx를 찾아서 반환*/
	size_t swap_slot_idx = bitmap_scan(swap_table, 0, 1, false);
	if (swap_slot_idx == BITMAP_ERROR)
		return false;
	
	/* SECTOR_PER_PAGE 개의 섹터에 페이지에 대한 내용을 써줌 */
	for(int i = 0; i < SECTOR_PER_PAGE; i++){
		disk_write(swap_disk, swap_slot_idx * SECTOR_PER_PAGE + i, page->va + (DISK_SECTOR_SIZE * i));}

	/* 해당 swap_slot에 대한 비트를 true로 변경 후 pml4_clear_page */
	bitmap_set(swap_table, swap_slot_idx, true);
	pml4_clear_page(thread_current()->pml4, page->va);
	

	/* page swap_slot_number에 swap_slot_idx를 저장 */
	anon_page->swap_slot_no = swap_slot_idx;

	return true;
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void
anon_destroy(struct page *page)
{
	struct anon_page *anon_page = &page->anon;
}
