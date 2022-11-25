#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/synch.h"
#include "threads/loader.h"
#include "threads/flags.h"
#include "userprog/gdt.h"
#include "userprog/process.h"
#include "intrinsic.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "threads/palloc.h"
#include "lib/string.h"

void syscall_entry(void);
void syscall_handler(struct intr_frame *);
void check_address(void *addr);

void halt(void);
void exit(int status);
tid_t fork(const char *thread_name);
int exec(const char *cmd_line);
int wait(tid_t pid);
bool create(const char *file, unsigned initial_size);
bool remove(const char *file);
int open(const char *file);
int filesize(int fd);
int read(int fd, void *buffer, unsigned size);
int write(int fd, const void *buffer, unsigned size);
void seek(int fd, unsigned position);
unsigned tell(int fd);
void close(int fd);
// struct file *fd_to_file(int fd);

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

void syscall_init(void)
{
	lock_init(&filesys_lock);

	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48 |
							((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t)syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			  FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
}

/* The main system call interface */
void syscall_handler(struct intr_frame *f UNUSED)
{
	// TODO: Your implementation goes here.

	switch (f->R.rax)
	{
	case SYS_WRITE:
		f->R.rax = write(f->R.rdi, f->R.rsi, f->R.rdx);
		break;

	case SYS_HALT:
		halt();
		break;

	case SYS_EXIT:
		exit(f->R.rdi);
		break;

	case SYS_CREATE:
		f->R.rax = create(f->R.rdi, f->R.rsi);
		break;

	case SYS_REMOVE:
		f->R.rax = remove(f->R.rdi);
		break;

	case SYS_OPEN:
		f->R.rax = open(f->R.rdi);
		break;

	case SYS_FILESIZE:
		f->R.rax = filesize(f->R.rdi);
		break;

	case SYS_READ:
		f->R.rax = read(f->R.rdi, f->R.rsi, f->R.rdx);
		break;

	case SYS_CLOSE:
		close(f->R.rdi);
		break;

	case SYS_SEEK:
		printf("*****************seek\n");
		seek(f->R.rdi, f->R.rsi);
		break;

	case SYS_TELL:
		f->R.rax = tell(f->R.rdi);
		break;

	case SYS_EXEC:
		f->R.rax = exec(f->R.rdi);
		break;

	case SYS_FORK:
		f->R.rax = fork(f->R.rdi);
		break;

	case SYS_WAIT:
		f->R.rax = wait(f->R.rdi);
		break;

	default:
		break;
	}
}

void check_address(void *addr)
{
	struct thread *t = thread_current();
	if (is_kernel_vaddr(addr) || addr == NULL || pml4_get_page(t->pml4, addr) == NULL)
		// if (addr == NULL || is_kernel_vaddr(addr) || pml4e_walk(t->pml4, addr, false) == NULL)
		exit(-1);
}

void halt(void)
{
	power_off();
	// Terminates Pintos by calling power_off() (declared in src/include/threads/init.h).
	// This should be seldom used, because you lose some information about possible deadlock situations, etc.
}
void exit(int status)
{
	thread_current()->exit_status = status;
	printf("%s: exit(%d)\n", thread_name(), status);
	thread_exit();

	// Terminates the current user program, returning status to the kernel.
	// If the process's parent waits for it (see below), this is the status that will be returned.
	// Conventionally, a status of 0 indicates success and nonzero values indicate errors.
}
tid_t fork(const char *thread_name)
{
	struct thread *t = thread_current();
	return process_fork(thread_name, &t->tf);

	// Create new process which is the clone of current process with the name THREAD_NAME.
	// You don't need to clone the value of the registers except %RBX, %RSP, %RBP, and %R12 - %R15, which are callee-saved registers.
	// Must return pid of the child process, otherwise shouldn't be a valid pid. In child process, the return value should be 0.
	// The child should have DUPLICATED resources including file descriptor and virtual memory space.
	// Parent process should never return from the fork until it knows whether the child process successfully cloned.
	// That is, if the child process fail to duplicate the resource, the fork () call of parent should return the TID_ERROR.
	// The template utilizes the pml4_for_each() in threads/mmu.c to copy entire user memory space,
	// including corresponding pagetable structures, but you need to fill missing parts of passed pte_for_each_func (See virtual address).
}
int exec(const char *cmd_line)
{
	check_address(cmd_line);
	char *file_name_copy = palloc_get_page(PAL_ZERO);

	if (file_name_copy == NULL)
		return -1;
	// strlcpy(file_name_copy, cmd_line, strlen(cmd_line) + 1);
	strlcpy(file_name_copy, cmd_line, PGSIZE);

	if (process_exec(file_name_copy) == -1)
		exit(-1);

	NOT_REACHED();
	// Change current process to the executable whose name is given in cmd_line, passing any given arguments.
	// This never returns if successful. Otherwise the process terminates with exit state -1, if the program cannot load or run for any reason.
	// This function does not change the name of the thread that called exec. Please note that file descriptors remain open across an exec call.
}

int wait(tid_t pid)
{

	return process_wait(pid);

	// Waits for a child process pid and retrieves the child's exit status.

	// If pid is still alive, waits until it terminates.
	// Then, returns the status that pid passed to exit.
	// If pid did not call exit(), but was terminated by the kernel (e.g. killed due to an exception), wait(pid) must return -1.
	// It is perfectly legal for a parent process to wait for child processes
	// that have already terminated by the time the parent calls wait,
	// but the kernel must still allow the parent to retrieve its child’s exit status,
	// or learn that the child was terminated by the kernel.
	// wait must fail and return -1 immediately if any of the following conditions is true:

	// pid does not refer to a direct child of the calling process.
	// pid is a direct child of the calling process if and only if the calling process received pid as a return value from a successful call to fork.
	// Note that children are not inherited: if A spawns child B and B spawns child process C, then A cannot wait for C, even if B is dead.
	// A call to wait(C) by process A must fail. Similarly, orphaned processes are not assigned to a new parent if their parent process exits before they do.

	// The process that calls wait has already called wait on pid. That is, a process may wait for any given child at most once.

	// Processes may spawn any number of children, wait for them in any order, and may even exit without having waited for some or all of their children.
	// Your design should consider all the ways in which waits can occur. All of a process's resources, including its struct thread,
	// must be freed whether its parent ever waits for it or not, and regardless of whether the child exits before or after its parent.
	// You must ensure that Pintos does not terminate until the initial process exits.
	// The supplied Pintos code tries to do this by calling process_wait() (in userprog/process.c) from main() (in threads/init.c).
	// We suggest that you implement process_wait() according to the comment at the top of the function and then implement the wait system call in terms of process_wait().

	// Implementing this system call requires considerably more work than any of the rest.
}
bool create(const char *file, unsigned initial_size)
{
	check_address(file);
	if (filesys_create(file, initial_size))
		return true;
	else
		return false;
	// Creates a new file called file initially initial_size bytes in size.
	// Returns true if successful, false otherwise. Creating a new file does not open it: opening the new file is a separate operation which would require a open system call.
}
bool remove(const char *file)
{
	check_address(file);
	if (filesys_remove(file))
		return true;
	else
		return false;
	// Deletes the file called file. Returns true if successful, false otherwise.
	// A file may be removed regardless of whether it is open or closed, and removing an open file does not close it.
	// See Removing an Open File in FAQ for details.
}
int open(const char *file)
{
	check_address(file);

	struct thread *t = thread_current();
	struct file **file_descriptor_table = t->file_descriptor_table;
	int fd = t->fd_number;
	struct file *f = filesys_open(file);

	if (f == NULL)
		return -1;

	while (t->file_descriptor_table[fd] != NULL && fd < FDT_COUNT_LIMIT)
		fd++;

	if (fd >= FDT_COUNT_LIMIT)
		file_close(f);

	t->fd_number = fd;
	file_descriptor_table[fd] = f;

	return fd;

	// Opens the file called file. Returns a nonnegative integer handle called a "file descriptor" (fd), or -1 if the file could not be opened.
	// File descriptors numbered 0 and 1 are reserved for the console: fd 0 (STDIN_FILENO) is standard input, fd 1 (STDOUT_FILENO) is standard output.
	// The open system call will never return either of these file descriptors, which are valid as system call arguments only as explicitly described below.

	// Each process has an independent set of file descriptors.
	// File descriptors are inherited by child processes.
	// When a single file is opened more than once, whether by a single process or different processes, each open returns a new file descriptor.
	// Different file descriptors for a single file are closed independently in separate calls to close and they do not share a file position.
	// You should follow the linux scheme, which returns integer starting from zero, to do the extra.
}

int filesize(int fd)
{
	struct file *f = get_file(fd);

	if (f == NULL)
		return -1;

	int result = file_length(f);

	return result;
	// Returns the size, in bytes, of the file open as fd.
}

struct file *fd_to_file(int fd)
{
	if (fd < 0 || fd >= FDT_COUNT_LIMIT || fd == NULL)
		return NULL;

	// if (fd < 0 || fd >= FDT_COUNT_LIMIT)
	// 	return NULL;

	struct thread *t = thread_current();
	struct file *f;
	f = t->file_descriptor_table[fd];

	return f;
}

int read(int fd, void *buffer, unsigned size)
{

	// if (fd < 0 || fd >= FDT_COUNT_LIMIT)
	// 	return -1;

	// struct thread *t = thread_current();
	// struct file *f;
	// f = t->file_descriptor_table[fd];

	// check_address(f);

	struct file *f = fd_to_file(fd);

	check_address(buffer);
	check_address(buffer + size - 1);

	unsigned char *buf = buffer;
	char key;

	if (f == NULL)
		return -1;

	int i = 0;

	if (size == 0)
		return 0;

	if (fd == 0)
	{
		while (i <= size)
		{
			key = input_getc();
			buf = key;
			buffer++;

			if (key == "\0")
				break;

			i++;
		}
	}

	else if (fd == 1)
		return -1;

	lock_acquire(&filesys_lock);
	i = file_read(f, buffer, size);

	lock_release(&filesys_lock);

	return i;

	// Reads size bytes from the file open as fd into buffer.
	// Returns the number of bytes actually read (0 at end of file),

	// or -1 if the file could not be read (due to a condition other than end of file).

	// fd 0 reads from the keyboard using input_getc().
}

int write(int fd, const void *buffer, unsigned size)
{
	struct file *f = fd_to_file(fd);

	if (fd == 0)
		return 0;

	check_address(buffer);

	if (fd == 1)
	{
		putbuf(buffer, size);
		return size;
	}

	// struct thread *t = thread_current();
	// struct file *f;
	// f = t->file_descriptor_table[fd];

	// check_address(f);
	// check_address(f);

	if (f == NULL)
		return 0;

	if (size == 0)
		return 0;

	lock_acquire(&filesys_lock);
	int i = file_write(f, buffer, size);
	lock_release(&filesys_lock);

	return i;

	// Writes size bytes from buffer to the open file fd.
	// Returns the number of bytes actually written, which may be less than size if some bytes could not be written.
	// Writing past end-of-file would normally extend the file, but file growth is not implemented by the basic file system.
	// The expected behavior is to write as many bytes as possible up to end-of-file and return the actual number written, or 0 if no bytes could be written at all.
	// fd 1 writes to the console. Your code to write to the console should write all of buffer in one call to putbuf(),
	// at least as long as size is not bigger than a few hundred bytes (It is reasonable to break up larger buffers).
	// Otherwise, lines of text output by different processes may end up interleaved on the console, confusing both human readers and our grading scripts.
}

void seek(int fd, unsigned position)
{
	if (fd < 2)
		return;
	struct file *f = fd_to_file(fd);
	if (f == NULL)
		return;
	check_address(f);

	file_seek(f, position);

	// Changes the next byte to be read or written in open file fd to position, expressed in bytes from the beginning of the file
	// (Thus, a position of 0 is the file's start). A seek past the current end of a file is not an error.
	// A later read obtains 0 bytes, indicating end of file. A later write extends the file, filling any unwritten gap with zeros.
	// (However, in Pintos files have a fixed length until project 4 is complete, so writes past end of file will return an error.)
	// These semantics are implemented in the file system and do not require any special effort in system call implementation.
}
unsigned tell(int fd)
{
	if (fd < 2)
		return;
	struct file *f = fd_to_file(fd);
	if (f == NULL)
		return;
	check_address(f);

	return file_tell(f);
	// Returns the position of the next byte to be read or written in open file fd,
	// expressed in bytes from the beginning of the file.
}
void close(int fd)
{
	struct thread *t = thread_current();
	struct file *f = fd_to_file(fd);
	if (f == NULL)
		return;

	lock_acquire(&filesys_lock);
	file_close(f);
	lock_release(&filesys_lock);

	t->file_descriptor_table[fd] = NULL;
}
