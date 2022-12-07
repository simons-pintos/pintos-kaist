#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

int process_add_file(struct file *f);
struct file *process_get_file(int fd);

tid_t process_create_initd(const char *file_name);
tid_t process_fork(const char *name, struct intr_frame *if_);
int process_exec(void *f_name);
int process_wait(tid_t);
void process_exit(void);
void process_activate(struct thread *next);

/********* project 3 ***********/
bool lazy_load_segment(struct page *page, void *aux);


struct file_info
{
	struct file *file;
	off_t ofs;
	uint32_t page_read_bytes;
	uint32_t page_zero_bytes;

    /* mmap */
    // struct list mmap_list;
    // struct list_elem mmap_elem;
};


#endif /* userprog/process.h */

