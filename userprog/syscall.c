#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "userprog/process.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "filesys/filesys.h"
#include "filesys/file.h"

void syscall_entry(void);
void syscall_handler(struct intr_frame *);

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081			/* Segment selector msr */
#define MSR_LSTAR 0xc0000082		/* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void halt(void);
void exit(int status);
tid_t fork(const char *thread_name, struct intr_frame *if_);
tid_t exec(const char *file);
int wait(tid_t pid);
bool create(const char *file, unsigned initial_size);
bool remove(const char *file);
int open(const char *file);
int filesize(int fd);
int read(int fd, void *buffer, unsigned size);
int write(int fd, const void *buffer, unsigned size);
void close(int fd);

void syscall_init(void)
{
	lock_init(&file_lock);

	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48 |
							((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t)syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			  FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
}

void check_address(uint64_t addr)
{
	if (is_kernel_vaddr(addr) || addr == NULL || pml4_get_page(thread_current()->pml4, addr) == NULL)
		exit(-1);
}

/* The main system call interface */
void syscall_handler(struct intr_frame *f UNUSED)
{
	// SYS_HALT,		/* Halt the operating system. */
	// SYS_EXIT,		/* Terminate this process. */
	// SYS_FORK,		/* Clone current process. */
	// SYS_EXEC,		/* Switch current process. */
	// SYS_WAIT,		/* Wait for a child process to die. */
	// SYS_CREATE,		/* Create a file. */
	// SYS_REMOVE,		/* Delete a file. */
	// SYS_OPEN,		/* Open a file. */
	// SYS_FILESIZE,	/* Obtain a file's size. */
	// SYS_READ,		/* Read from a file. */
	// SYS_WRITE,		/* Write to a file. */
	// SYS_SEEK,		/* Change position in a file. */
	// SYS_TELL,		/* Report current position in a file. */
	// SYS_CLOSE,		/* Close a file. */

	switch (f->R.rax)
	{
	case SYS_HALT:
		halt();
		break;

	case SYS_EXIT:
		// argv[0]: int status
		exit(f->R.rdi);
		break;

	case SYS_FORK:
		// argv[0]: const char *thread_name
		check_address(f->R.rdi);

		f->R.rax = fork(f->R.rdi, f);
		break;

	case SYS_EXEC:
		// argv[0]: const char *file
		check_address(f->R.rdi);

		exec(f->R.rdi);
		break;

	case SYS_WAIT:
		// argv[0]: tid_t pid
		f->R.rax = wait(f->R.rdi);
		break;

	case SYS_CREATE:
		// argv[0]: const char *file
		// argv[1]: unsigned initial_size
		check_address(f->R.rdi);

		f->R.rax = create(f->R.rdi, f->R.rsi);
		break;

	case SYS_REMOVE:
		// argv[0]: const char *file
		check_address(f->R.rdi);

		remove(f->R.rdi);
		break;

	case SYS_OPEN:
		// argv[0]: const char *file
		check_address(f->R.rdi);

		f->R.rax = open(f->R.rdi);
		break;

	case SYS_FILESIZE:
		// argv[0]: int fd
		f->R.rax = filesize(f->R.rdi);
		break;

	case SYS_READ:
		// argv[0]: int fd
		// argv[1]: void *buffer
		// argv[2]: unsigned size
		check_address(f->R.rsi);

		f->R.rax = read(f->R.rdi, f->R.rsi, f->R.rdx);
		break;

	case SYS_WRITE:
		// argv[0]: int fd
		// argv[1]: const void *buffer
		// argv[2]: unsigned size
		check_address(f->R.rsi);

		f->R.rax = write(f->R.rdi, f->R.rsi, f->R.rdx);
		break;

	case SYS_CLOSE:
		// argv[0]: int fd
		close(f->R.rdi);
		break;
	}
}

void halt(void)
{
	power_off();
}

void exit(int status)
{
	thread_current()->exit_status = status;
	printf("%s: exit(%d)\n", thread_name(), thread_current()->exit_status);

	thread_exit();
}

tid_t fork(const char *thread_name, struct intr_frame *if_)
{
	return process_fork(thread_name, if_);
}

int exec(const char *file)
{
}

int wait(tid_t pid)
{
	return process_wait(pid);
}

bool create(const char *file, unsigned initial_size)
{
	return filesys_create(file, initial_size);
}

bool remove(const char *file)
{
	return filesys_remove(file);
}

int open(const char *file)
{
	struct file *f = filesys_open(file);
	if (f == NULL)
		return -1;

	int fd = process_add_file(f);
	if (fd < 2)
		return -1;

	return fd;
}

int filesize(int fd)
{
	struct file *f = process_get_file(fd);
	if (f == NULL)
		return -1;

	return file_length(f);
}

int read(int fd, void *buffer, unsigned size)
{
	check_address(buffer + size - 1);
	int read_result;

	if (fd == 0)
	{
		for (read_result = 0; read_result < size; read_result++)
		{
			char key = input_getc();
			*(char *)buffer = key;
			(char *)buffer++;

			if (key == '\0')
				break;
		}
	}
	else
	{
		struct file *f = process_get_file(fd);
		if (f == NULL)
			return -1;

		lock_acquire(&file_lock);
		read_result = file_read(f, buffer, size);
	}

	return read_result;
}

int write(int fd, const void *buffer, unsigned size)
{
	int write_result;

	if (fd == 1)
	{
		putbuf(buffer, size);
		write_result = size;
	}
	else
	{
		struct file *f = process_get_file(fd);
		if (f == NULL)
			return -1;

		lock_acquire(&file_lock);
		write_result = file_write(f, buffer, size);
		lock_release(&file_lock);
	}

	return write_result;
}

void close(int fd)
{
	struct file *f = process_get_file(fd);
	if (f == NULL)
		return;

	file_close(f);
	thread_current()->fdt[fd] = NULL;
}
