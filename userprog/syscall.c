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
tid_t exec(const char *cmd_line);
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

void check_address(void *addr)
{
	if (addr >= KERN_BASE)
	{
		printf("==invalid address==\n");
		thread_exit();
	}
}

void get_argument(struct intr_frame *if_, uint64_t *argv)
{
	argv[0] = *(int *)if_->R.rdx;
	argv[1] = *(int *)if_->R.rsi;
	argv[2] = *(int *)if_->R.rdx;
	argv[3] = *(int *)if_->R.r10;
	argv[4] = *(int *)if_->R.r8;
	argv[5] = *(int *)if_->R.r9;
}

/* The main system call interface */
void syscall_handler(struct intr_frame *f UNUSED)
{
	// TODO: Your implementation goes here.
	printf("system call!\n");

	uint64_t argv[6];
	get_argument(f, argv);

	switch (f->R.rax)
	{
	case SYS_HALT:
		halt();
		break;

	case SYS_EXIT:
		exit(argv[0]);
		break;

	case SYS_CREATE:
		check_address(argv[0]);

		create(argv[0], argv[1]);
		break;

	case SYS_REMOVE:
		check_address(argv[0]);

		remove(argv[0]);
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
	prinf("%s: exit(%d)\n", curr_t->name, curr_t->status);
	thread_exit();
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