#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "filesys/filesys.h"

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
bool create(const char *file, unsigned initial_size);
bool remove(const char *file);

void syscall_init(void)
{
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
	if (!is_user_vaddr(addr))
	{
		printf("==invalid address==\n");
		thread_exit();
	}
}

/* The main system call interface */
void syscall_handler(struct intr_frame *f UNUSED)
{
	// TODO: Your implementation goes here.
	// printf("system call!\n");

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

		fork(f->R.rdi, f);
		break;

	case SYS_EXEC:
		// argv[0]: const char *file
		check_address(f->R.rdi);

		exec(f->R.rdi);
		break;

	case SYS_WAIT:
		// argv[0]: pid_t pid
		wait(f->R.rdi);
		break;

	case SYS_CREATE:
		// argv[0]: const char *file
		// argv[1]: unsigned initial_size
		check_address(f->R.rdi);

		create(f->R.rdi, f->R.rsi);
		break;

	case SYS_REMOVE:
		// argv[0]: const char *file
		check_address(f->R.rdi);

		remove(f->R.rdi);
		break;

	default:
		break;
	}

	thread_exit();
}

void halt(void)
{
	power_off();
}

void exit(int status)
{
	struct thread *curr_t = thread_current();
	prinf("%s: exit(%d)\n", curr_t->name, curr_t->exit_status);
	thread_exit();
}

tid_t fork(const char *thread_name, struct intr_frame *if_)
{
	return process_fork(thread_name, if_);
}

tid_t exec(const char *file)
{
}

bool create(const char *file, unsigned initial_size)
{
	if (filesys_create(file, initial_size) > 0)
		return true;

	return false;
}

bool remove(const char *file)
{
	if (filesys_remove(file) > 0)
		return true;

	return false;
}