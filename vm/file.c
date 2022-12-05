/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
#include "threads/vaddr.h"

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

struct file_info
{
    struct file *file;
    off_t ofs;
    uint32_t page_read_bytes;
    uint32_t page_zero_bytes;
};

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
	struct file_page *file_page UNUSED = &page->file;
}

/* Swap out the page by writeback contents to the file. */
static bool
file_backed_swap_out(struct page *page)
{
	struct file_page *file_page UNUSED = &page->file;
}

/* Destory the file backed page. PAGE will be freed by the caller. */
static void
file_backed_destroy(struct page *page)
{
	struct file_page *file_page UNUSED = &page->file;
}

static bool
lazy_load_segment(struct page *page, void *aux)
{
    /* TODO: Load the segment from the file */
    /* TODO: This called when the first page fault occurs on address VA. */
    /* TODO: VA is available when calling this function. */

    // aux로 전달 받은 file data
    struct file_info *file_info = (struct file_info *)aux;

    struct file *file = file_info->file;
    off_t ofs = file_info->ofs;
    int page_read_bytes = file_info->page_read_bytes;
    int page_zero_bytes = file_info->page_zero_bytes;

    // free(file_info);

    // load해야할 부분으로 file ofs 변경
    file_seek(file, ofs);

    // laod segment
    if (file_read(file, page->frame->kva, page_read_bytes) != page_read_bytes)
    {
        free(file_info);
        return false;
    }

    // setup zero bytes space
    memset(page->frame->kva + page_read_bytes, 0, page_zero_bytes);

    return true;
}


/* Do the mmap */
void * do_mmap (void *addr, size_t length, int writable, struct file *file, off_t offset) {
    ASSERT(pg_ofs(addr) == 0);
    ASSERT(offset % PGSIZE == 0);

    file_seek(file, offset);
    void *start_addr = addr;

    while (length > 0)
    {
        size_t page_read_bytes = length < PGSIZE ? length : PGSIZE;
        size_t page_zero_bytes = PGSIZE - page_read_bytes;

        /* file_info 생성 */
        struct file_info *file_info = (struct file_info *)malloc(sizeof(struct file_info));

        file_info->file = file;
        file_info->ofs = offset;
        file_info->page_read_bytes = page_read_bytes;
        file_info->page_zero_bytes = page_zero_bytes;

        if (!vm_alloc_page_with_initializer(VM_FILE, addr, writable, lazy_load_segment, file_info))
            return false;
        
        length -= page_read_bytes;
        addr += PGSIZE;
        offset += PGSIZE;
    }
    return start_addr;
}


/* Do the munmap */
void do_munmap(void *addr)
{
}
