/* file.c: Implementation of memory backed file object (mmaped object). */
#include "vm/vm.h"
#include "threads/vaddr.h"
#include "userprog/process.h"
#include "threads/mmu.h"

static bool file_backed_swap_in(struct page *page, void *kva);
static bool file_backed_swap_out(struct page *page);
static void file_backed_destroy(struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations file_ops = {
	.swap_in = file_backed_swap_in,
	.swap_out = file_backed_swap_out,
	.destroy = file_backed_destroy,
	.type = VM_FILE,
};

/* Project 3 : Swapping in & out */


/* The initializer of file vm */
void vm_file_init(void)
{


}

/* Initialize the file backed page */
bool file_backed_initializer(struct page *page, enum vm_type type, void *kva)
{
	/* Set up the handler */
	page->operations = &file_ops;
	struct file_page *file_page = &page->file;
}

/* Swap in the page by read contents from the file. */
static bool
file_backed_swap_in(struct page *page, void *kva)
{
	struct file_page *file_page = &page->file;
	struct file_info *page_info = page->uninit.aux;
	
	size_t page_read_bytes = page_info->page_read_bytes;
	size_t page_zero_bytes = PGSIZE - page_read_bytes;

	file_seek(page_info->file, page_info->ofs);

	file_read_at(page_info->file, kva, page_read_bytes, page_info->ofs);
	
	return true;
}

/* Swap out the page by writeback contents to the file. */
static bool
file_backed_swap_out(struct page *page)
{
	struct file_page *file_page = &page->file;
	struct file_info *page_info = page->uninit.aux;

	if (file_write_at(page_info->file, page->frame->kva, page_info->page_read_bytes, page_info->ofs) != (int)page_info->page_read_bytes)
		return false;
	pml4_clear_page(thread_current()->pml4, page->va);

	return true;

}

/* Destory the file backed page. PAGE will be freed by the caller. */
static void
file_backed_destroy(struct page *page)
{
	struct file_page *file_page UNUSED = &page->file;
}

/* Do the mmap */
void *
do_mmap(void *addr, size_t length, int writable, struct file *file, off_t offset)
{
	ASSERT(pg_ofs(addr) == 0);
	ASSERT(offset % PGSIZE == 0);
	// file_seek(file, offset);
	struct thread *curr = thread_current();
	struct mmap_file *mmap_file = (struct mmap_file *)malloc(sizeof(struct mmap_file));

	mmap_file->file = file_reopen(file);
	if (mmap_file->file == NULL)
		return NULL;

	mmap_file->mapid = addr;
	
	size_t read_bytes = length > file_length(mmap_file->file) ? file_length(mmap_file->file) : length;
	size_t zero_bytes = PGSIZE - read_bytes % PGSIZE;
	uintptr_t upage = addr;
	off_t ofs = offset;

	list_init(&mmap_file->page_list);
	list_push_back(&curr->mmap_list, &mmap_file->elem);

	
	while (read_bytes > 0 || zero_bytes > 0)
	{
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;
		/* Set up aux to pass information to the lazy_load_segment */
		struct file_info *file_info = (struct file_info *)malloc(sizeof(struct file_info));

		file_info->file = mmap_file->file;
		file_info->ofs = ofs;
		file_info->page_read_bytes = page_read_bytes;
		file_info->page_zero_bytes = page_zero_bytes;


		/* 해당 Virtual Memory에 struct page 할당해주며 load 되기 전까지는 uninit page */
		/* page fault로 physical memory로 load 될 때 할당될 때 받은 page type으로 변환, */
		/* lazy_load_sement 함수를 실행시켜서 upload함 */
		if(!vm_alloc_page_with_initializer(VM_FILE, upage, writable, lazy_load_segment, file_info))
			return NULL;

		
		struct page *page = spt_find_page(&curr->spt, upage);
		if (page == NULL)
			return false;

		list_push_back(&mmap_file->page_list, &page->mapped_elem);
		
		/* Advance */
		read_bytes -= page_read_bytes;  // 남은 read_bytes 갱신
		zero_bytes -= page_zero_bytes;  // 남은 zero_bytes 갱신
		upage += PGSIZE; 		        // virtual address를 옮겨서 다음 page space를 가리키게 함
		ofs += PGSIZE; 	                // 다음 page에 mapping 시킬 file 위치 갱신
	}
	// printf("===[DEBUG] WHERE ARE U?\n");
	return addr;
}

/* Do the munmap */
void do_munmap(void *addr)
{
	/* 한양대 기준 */
	/* mmap_filed의 vme_list에 연결된 모든 vm_entry들을 제거 */
	/* 페이지 테이블 엔트리 제거 */
	/* vm_entry를 가리키는 가상 주소에 대한 물리페이지가 존재하고, dirty 하면 디스크에 메모리 내용을 기록 */
	
	/* 1) 주어진 스레드에 대해 파일을 찾아 해당 파일과 연관되는 mmap_list 찾기 */
	struct thread *curr = thread_current();
	void *munmap_addr = addr;
	struct mmap_file *mmap_file;
	struct page *page;
	struct file_info *file_info;

	struct list *mmap_list = &curr->mmap_list;
	struct list_elem *elem;
	int mapid;


	/* thread에 내재되어 있는 mmap_list 순회하여 addr과 일치하는 mapid 검색 */
	for (elem = list_begin(mmap_list); elem != list_end(mmap_list); elem = list_next(elem)) {
		mmap_file = list_entry(elem, struct mmap_file, elem);
		if (mmap_file->mapid == addr){
			break;
		}
	}

	// mmap_file->file = file_reopen(mmap_file->file);
	// if (mmap_file->file == NULL)
	// 	return NULL;

	/* page_list에서 page를 하나씩 꺼내어서 dirty일 시 파일에 쓰고 종료*/
	for (elem = list_begin(&mmap_file->page_list); elem != list_end(&mmap_file->page_list); elem = list_next(elem)){
		page = list_entry(elem, struct page, mapped_elem);
		// printf("===[DEBUG] page : %p\n", page->va);
		file_info = page->uninit.aux;   // ??? 이 코드를 제대로 모르겠음 (왜 aux가 file_info 인건지)
		size_t ofs = file_info -> ofs;
		
		if ((pml4_is_dirty(curr->pml4, page->va))){

			file_write_at(mmap_file->file, page->va, file_info->page_read_bytes, ofs);
			pml4_set_dirty(curr->pml4, page->va, 0);
		}
		pml4_clear_page(curr->pml4, page->va);
		// ofs =+ PGSIZE;
		// munmap_addr =+ PGSIZE;
		}
		// file_close(file_info->file);
		list_remove(&mmap_file->elem);
		// free(mmap_file);

		return;
	}


	/****** 중선 코드 ******/
	// /* pseudo code */
	// /* 1) 주어진 인자 addr으로부터 연결된 page를 찾음 */
	// struct thread *curr = thread_current();
	// struct supplemental_page_table *spt = &curr->spt;
	// struct page *page;
	// void *munmap_addr = addr;
	// struct file_info *file_info;


	// /* 2) page와 연결된 list에 대한 내용을 매핑해제 및 삭제 */
	// /* 페이지 테이블 엔트리 제거 */
	// /* dirty 일시 디스크에 메모리 내용을 기록 */
	// // struct list list = page->mapped_list;
	// while (true)
	// {
	// 	page = spt_find_page(spt, munmap_addr);
	// 	if (page == NULL)
	// 		return NULL;

	// 	file_info = (struct file_info *)page->uninit.aux;

	// 	if(pml4_is_dirty(curr->pml4, page->va)){
	// 		file_write_at(file_info->file, munmap_addr, file_info->page_read_bytes, file_info->ofs);
	// 		pml4_set_dirty(curr->pml4, page->va, 0);
	// 	}

	// 	pml4_clear_page(curr->pml4, munmap_addr);
	// 	munmap_addr += PGSIZE;
	// }
// }