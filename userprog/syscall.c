#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "threads/flags.h"
#include "threads/palloc.h"
#include "userprog/gdt.h"
#include "userprog/process.h"
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

#define STDIN 1
#define STDOUT 2

struct semaphore mutex, wrt;
int read_cnt;

void halt(void);
void exit(int status);
tid_t fork(const char *thread_name, struct intr_frame *if_);
int exec(const char *file);
int wait(tid_t pid);
bool create(const char *file, unsigned initial_size);
bool remove(const char *file);
int open(const char *file);
int filesize(int fd);
int read(int fd, void *buffer, unsigned size);
int write(int fd, const void *buffer, unsigned size);
void seek(int fd, unsigned pos);
unsigned tell(int fd);
void close(int fd);

int dup2(int oldfd, int newfd);

void syscall_init(void)
{
	sema_init(&mutex, 1);
	sema_init(&wrt, 1);
	read_cnt = 0;

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

	// SYS_DUP2			/* Duplicate the file descriptor */

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

		if (exec(f->R.rdi) < 0)
			exit(-1);
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

	case SYS_SEEK:
		// argv[0]: int fd
		// argv[1]: unsigned position
		seek(f->R.rdi, f->R.rsi);
		break;

	case SYS_TELL:
		// argv[0]: int fd
		f->R.rax = tell(f->R.rdi);
		break;

	case SYS_CLOSE:
		// argv[0]: int fd
		close(f->R.rdi);
		break;

	case SYS_DUP2:
		// argv[0]: int oldfd
		// argv[1]: int newfd
		f->R.rax = dup2(f->R.rdi, f->R.rsi);
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
	char *fn_copy = palloc_get_page(PAL_ZERO);
	if (fn_copy == NULL)
		return -1;

	strlcpy(fn_copy, file, PGSIZE);

	if (process_exec(fn_copy) < 0)
		return -1;

	return 0;
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
	sema_down(&wrt);
	struct file *f = filesys_open(file);
	sema_up(&wrt);

	if (f == NULL)
		return -1;

	int fd = process_add_file(f);
	if (fd == -1)
		close(f);

	return fd;
}

int filesize(int fd)
{
	if (fd < 2)
		return -1;

	struct file *f = process_get_file(fd);
	if (f == NULL)
		return -1;

	return file_length(f);
}

int read(int fd, void *buffer, unsigned size)
{
	check_address(buffer + size - 1);

	struct file *f = process_get_file(fd);
	if (f == NULL || f == STDOUT)
		return -1;

	int read_result;

	if (f == STDIN)
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
		sema_down(&mutex);
		read_cnt++;
		if (read_cnt == 1)
			sema_down(&wrt);
		sema_up(&mutex);

		read_result = file_read(f, buffer, size);

		sema_down(&mutex);
		read_cnt--;
		if (read_cnt == 0)
			sema_up(&wrt);
		sema_up(&mutex);
	}

	return read_result;
}

int write(int fd, const void *buffer, unsigned size)
{
	struct file *f = process_get_file(fd);
	if (f == NULL || f == STDIN)
		return -1;

	int write_result;

	if (f == STDOUT)
	{
		putbuf(buffer, size);
		write_result = size;
	}
	else
	{
		sema_down(&wrt);
		write_result = file_write(f, buffer, size);
		sema_up(&wrt);
	}

	return write_result;
}

void seek(int fd, unsigned pos)
{
	if (fd < 2)
		return -1;

	struct file *f = process_get_file(fd);
	if (f == NULL)
		return -1;

	file_seek(f, pos);
}

unsigned tell(int fd)
{
	if (fd < 2)
		return -1;

	struct file *f = process_get_file(fd);
	if (f == NULL)
		return -1;

	return file_tell(f);
}

void close(int fd)
{
	struct thread *curr = thread_current();
	struct file *f = process_get_file(fd);
	if (f == NULL)
		return;

	if (f == STDIN)
		curr->stdin_cnt--;
	else if (f == STDOUT)
		curr->stdout_cnt--;
	else
	{
		if (f->dup_cnt == 0)
		{
			curr->fd_idx = fd;
			file_close(f);
		}
		else
			f->dup_cnt--;
	}

	thread_current()->fdt[fd] = NULL;
}

int dup2(int oldfd, int newfd)
{
	if (oldfd == newfd)
		return newfd;

	struct thread *curr = thread_current();
	struct file *f = process_get_file(oldfd);
	if (f == NULL)
		return -1;

	if (newfd < 0 || newfd >= FDT_LIMIT)
		return -1;

	if (f == STDIN)
		curr->stdin_cnt++;
	else if (f == STDOUT)
		curr->stdout_cnt++;
	else
		f->dup_cnt++;

	close(newfd);
	curr->fdt[newfd] = f;
	return newfd;
}
