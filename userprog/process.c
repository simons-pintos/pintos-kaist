#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/tss.h"
#include "userprog/syscall.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/mmu.h"
#include "threads/vaddr.h"
#include "intrinsic.h"

// 편의상
#define VM

#ifdef VM
#include "vm/vm.h"
#endif

static void process_cleanup(void);
static bool load(const char *file_name, struct intr_frame *if_);
static void initd(void *f_name);
static void __do_fork(void *);

/*
부모 thread가 자신의 child_list에서 입력 받은 pid를 가지고 있는 child thread를 검색
child_list 전체 순회
있으면 child의 struct thread, 없으면 NULL return
*/
struct thread *get_child_process(int pid)
{
	struct list_elem *temp_elem = temp_elem = list_begin(&thread_current()->child_list);

	for (; temp_elem != list_tail(&thread_current()->child_list); temp_elem = list_next(temp_elem))
	{
		struct thread *temp_t = list_entry(temp_elem, struct thread, child_elem);

		if (temp_t->tid == pid)
			return temp_t;
	}

	return NULL;
}

/*
struct file *f를 현재 thread의 file descripter table(= fdt)에 저장
fdt에서의 struct file *f의 index(= fd)를 return
EXTRA multi-oom을 위해 바꿨지만 문제를 해결한 것 같지 않음
*/
int process_add_file(struct file *f)
{
	struct thread *curr = thread_current();

	while (curr->fd_idx < FDT_LIMIT && curr->fdt[curr->fd_idx])
		curr->fd_idx++;

	if (curr->fd_idx >= FDT_LIMIT)
		return -1;

	curr->fdt[curr->fd_idx] = f;
	return curr->fd_idx;

	// int fd = 2;
	// while (curr->fdt[fd] != NULL && fd < FDT_LIMIT)
	// 	fd++;

	// if (fd >= FDT_LIMIT)
	// 	return -1;

	// curr->fdt[fd] = f;
	// if (curr->fd_idx == fd)
	// 	curr->fd_idx++;

	// return fd;
}

/*
입력 받은 fd가 가리키는 struct file pointer를 return
*/
struct file *process_get_file(int fd)
{
	if (fd < 0 || fd >= FDT_LIMIT)
		return NULL;

	return thread_current()->fdt[fd];
}

/* General process initializer for initd and other process. */
static void
process_init(void)
{
	struct thread *current = thread_current();
}

/* Starts the first userland program, called "initd", loaded from FILE_NAME.
 * The new thread may be scheduled (and may even exit)
 * before process_create_initd() returns. Returns the initd's
 * thread id, or TID_ERROR if the thread cannot be created.
 * Notice that THIS SHOULD BE CALLED ONCE. */
tid_t process_create_initd(const char *file_name)
{
	char *fn_copy, *save_ptr, *token;
	tid_t tid;

	/* Make a copy of FILE_NAME.
	 * Otherwise there's a race between the caller and load(). */
	fn_copy = palloc_get_page(0);
	if (fn_copy == NULL)
		return TID_ERROR;
	strlcpy(fn_copy, file_name, PGSIZE);

	// thread name을 위해 앞부분만 parsing
	strtok_r(file_name, " ", &save_ptr);

	/* Create a new thread to execute FILE_NAME. */
	tid = thread_create(file_name, PRI_DEFAULT, initd, fn_copy);
	if (tid == TID_ERROR)
		palloc_free_page(fn_copy);
	return tid;
}

/* A thread function that launches first user process. */
static void
initd(void *f_name)
{
#ifdef VM
	supplemental_page_table_init(&thread_current()->spt);
#endif

	process_init();

	if (process_exec(f_name) < 0)
		PANIC("Fail to launch initd\n");
	NOT_REACHED();
}

/* Clones the current process as `name`. Returns the new process's thread id, or
 * TID_ERROR if the thread cannot be created. */
tid_t process_fork(const char *name, struct intr_frame *if_ UNUSED)
{
	/*
	부모 process가 system call인 fork를 호출해서 user mode에서 kernel mode로 context swtching
	kernel에서의 작업이 끝난 후에 user mode로 돌아가기 위해 context(= if)를 kernel stack에 저장
	fork를 통해 만들어진 자식 process는 kernel stack에 저장된 부모 process의 user 상태 if를 복사해서 그대로 사용
	thread_create로 만들어지는 자식 process에게 if를 전달하기 위해 인자로 받는 부모의 struct thread에 저장해서 전달
	*/
	struct thread *parent = thread_current();
	memcpy(&parent->parent_if, if_, sizeof(struct intr_frame));

	int pid = thread_create(name, PRI_DEFAULT, __do_fork, parent);
	if (pid == TID_ERROR)
		return TID_ERROR;

	// 자식 process가 load가 완료되기 전까지 부모 process는 대기
	struct thread *child = get_child_process(pid);
	sema_down(&child->fork);

	/*
	자식 process가 제대로 만들어지지 않은 경우
	process_wait의 semaphore 작업을 통해 자식 process가 exit할 수 있도록 함

	다른 코드는 그냥 TID_ERROR를 return 하는데 둘의 차이점을 좀 더 살펴볼 필요가 있음
	*/
	if (child->exit_status < 0)
		return process_wait(pid);

	return pid;
}

#ifndef VM
/* Duplicate the parent's address space by passing this function to the
 * pml4_for_each. This is only for the project 2. */
static bool
duplicate_pte(uint64_t *pte, void *va, void *aux)
{
	struct thread *current = thread_current();
	struct thread *parent = (struct thread *)aux;
	void *parent_page;
	void *newpage;
	bool writable;

	/* 1. TODO: If the parent_page is kernel page, then return immediately. */
	if (is_kernel_vaddr(va))
		return true;

	/* 2. Resolve VA from the parent's page map level 4. */
	parent_page = pml4_get_page(parent->pml4, va);
	if (parent_page == NULL)
		return false;

	/* 3. TODO: Allocate new PAL_USER page for the child and set result to
	 *    TODO: NEWPAGE. */
	newpage = palloc_get_page(PAL_USER);
	if (newpage == NULL)
		return false;

	/* 4. TODO: Duplicate parent's page to the new page and
	 *    TODO: check whether parent's page is writable or not (set WRITABLE
	 *    TODO: according to the result). */
	memcpy(newpage, parent_page, PGSIZE);
	writable = is_writable(pte);

	/* 5. Add new page to child's page table at address VA with WRITABLE
	 *    permission. */
	if (!pml4_set_page(current->pml4, va, newpage, writable))
	{
		/* 6. TODO: if fail to insert page, do error handling. */
		return false;
	}

	return true;
}
#endif

struct MapElem
{
	struct file *key;
	struct file *value;
};

/* A thread function that copies parent's execution context.
 * Hint) parent->tf does not hold the userland context of the process.
 *       That is, you are required to pass second argument of process_fork to
 *       this function. */
static void
__do_fork(void *aux)
{
	struct intr_frame if_;
	struct thread *parent = (struct thread *)aux;
	struct thread *current = thread_current();
	bool succ = true;

	/*
	1. Read the cpu context to local stack.
	전달 받아온 부모 process의 user 모드 if를 복사
	*/
	struct intr_frame *parent_if = &parent->parent_if;
	memcpy(&if_, parent_if, sizeof(struct intr_frame));

	/* 2. Duplicate PT */
	current->pml4 = pml4_create();
	if (current->pml4 == NULL)
		goto error;

	process_activate(current);

#ifdef VM
	supplemental_page_table_init(&current->spt);
	if (!supplemental_page_table_copy(&current->spt, &parent->spt))
		goto error;
#else
	if (!pml4_for_each(parent->pml4, duplicate_pte, parent))
		goto error;
#endif

	/* TODO: Your code goes here.
	 * TODO: Hint) To duplicate the file object, use `file_duplicate`
	 * TODO:       in include/filesys/file.h. Note that parent should not return
	 * TODO:       from the fork() until this function successfully duplicates
	 * TODO:       the resources of parent.*/

	if (parent->fd_idx >= FDT_LIMIT)
		goto error;

	/*
	process.h에서 struct MapElem 선언
	key, value 값을 갖는 struct(dictionary와 유사)
	*/
	const int MAPLEN = 10;	// 배열 크기(숫자에 큰 의미는 없음)
	struct MapElem map[10]; // struct MapElem 배열 선언
	int dup_idx = 0;		// 배열에서 사용할 index
	bool found;				// flag

	for (int i = 0; i < FDT_LIMIT; i++)
	{
		struct file *f = parent->fdt[i];
		if (f == NULL)
			continue;

		/*
		해당 file pointer가 map에 있나 검색(선형 검색)
		map에 있으면 map의 value를 current의 fdt에 가져온다
		map에 없으면 새롭게 duplicate한 후에 map에 추가
		*/
		found = false;
		for (int j = 0; j < MAPLEN; j++)
		{
			if (map[j].key == f)
			{
				found = true;
				current->fdt[i] = map[j].value;
				break;
			}
		}

		if (!found)
		{
			struct file *new_f;
			if (f > 2)
				new_f = file_duplicate(f);
			else // STD_IN or STD_OUT인 경우
				new_f = f;

			current->fdt[i] = new_f;

			if (dup_idx < MAPLEN)
			{
				map[dup_idx].key = f;
				map[dup_idx++].value = new_f;
			}
		}
	}

	current->fd_idx = parent->fd_idx;

	// fork에서 load가 다끝났으므로 부모 process를 다시 wakeup
	sema_up(&current->fork);

	// 만들어진 자식 process는 fork()에 대한 return 값을 0으로 받고 시작한다
	if_.R.rax = 0;

	/* Finally, switch to the newly created process. */
	if (succ)
		do_iret(&if_);

error:
	// 자식 process를 load하는 과정에서 error가 나면 부모 process를 일단 깨우고 load 실패한 자식 process를 TID_ERROR로 종료
	sema_up(&current->fork);
	exit(-1);
}

void argument_stack(int argc, char **argv, struct intr_frame *_if)
{
	// argument를 역순으로 stack에 push
	for (int i = argc - 1; i > -1; i--)
	{
		size_t len = strlen(argv[i]) + 1; // '\0'을 포함해야하므로 +1
		_if->rsp -= len;
		memcpy(_if->rsp, argv[i], len);
		argv[i] = (char *)_if->rsp; // 나중에 argument 주소 삽입을 위해 저장
	}

	// data align
	int align = _if->rsp % 8;
	_if->rsp -= align;
	memset(_if->rsp, 0, align);

	// argv[argc] = NULL
	_if->rsp -= 8;
	memset(_if->rsp, 0, 8);

	// argument의 stack 주소를 역순으로 push
	for (int i = argc - 1; i > -1; i--)
	{
		_if->rsp -= 8;
		memcpy(_if->rsp, &argv[i], 8);
	}

	// fake return 주소 push
	_if->rsp -= 8;
	memset(_if->rsp, 0, 8);

	// rdi(첫번째 인자 register)와 rsi(두번째 인자 register)에 argc와 argv 삽입
	_if->R.rdi = argc;
	_if->R.rsi = _if->rsp + 8;
}

/* Switch the current execution context to the f_name.
 * Returns -1 on fail. */
int process_exec(void *f_name)
{
	char *file_name = f_name;
	bool success;

	/* We cannot use the intr_frame in the thread structure.
	 * This is because when current thread rescheduled,
	 * it stores the execution information to the member. */
	struct intr_frame _if;
	_if.ds = _if.es = _if.ss = SEL_UDSEG;
	_if.cs = SEL_UCSEG;
	_if.eflags = FLAG_IF | FLAG_MBS;

	/* We first kill the current context */
	process_cleanup();

	/* And then load the binary */
	success = load(file_name, &_if);

	// stack에 인자 전달 확인
	// hex_dump(_if.rsp, _if.rsp, USER_STACK - _if.rsp, 1);

	/* If load failed, quit. */
	palloc_free_page(file_name);
	if (!success)
		return -1;

	/* Start switched process. */
	do_iret(&_if);
	NOT_REACHED();
}

/* Waits for thread TID to die and returns its exit status.  If
 * it was terminated by the kernel (i.e. killed due to an
 * exception), returns -1.  If TID is invalid or if it was not a
 * child of the calling process, or if process_wait() has already
 * been successfully called for the given TID, returns -1
 * immediately, without waiting.
 *
 * This function will be implemented in problem 2-2.  For now, it
 * does nothing. */
int process_wait(tid_t child_tid UNUSED)
{
	/* XXX: Hint) The pintos exit if process_wait (initd), we recommend you
	 * XXX:       to add infinite loop here before
	 * XXX:       implementing the process_wait. */
	if (child_tid < 0)
		return -1;

	struct thread *child = get_child_process(child_tid);
	if (child == NULL)
		return -1;

	// 자식 process가 exit될 때까지 sleep
	sema_down(&child->wait);

	/*
	자식 process가 process_exit 실행 중에 wait하는 부모 process를 wakeup
	종료하는 자식 process의 exit status를 읽어오고 child_list에서 삭제
	*/
	int status = child->exit_status;
	list_remove(&child->child_elem);

	// 위의 작업이 끝나기를 기다리는 자식 process를 wakeup
	sema_up(&child->exit);

	return status;
}

/* Exit the process. This function is called by thread_exit (). */
void process_exit(void)
{
	struct thread *curr = thread_current();
	/* TODO: Your code goes here.
	 * TODO: Implement process termination message (see
	 * TODO: project2/process_termination.html).
	 * TODO: We recommend you to implement process resource cleanup here. */

	/*
	실행 중인 file을 닫음
	deny write on executables를 위해 실행 중인 파일을 계속 open해 놓는다
	process 종료하기 전에 닫음
	*/
	file_close(curr->run_file);

	// fdt에 있는 모든 file descripter를 모두 닫는다
	for (int i = 0; i < FDT_LIMIT; i++)
		close(i);

	// palloc으로 memory를 할당 받는 fdt를 free한다
	palloc_free_multiple(curr->fdt, 3);

	process_cleanup();

	// wait을 하고 있는 부모 process를 wakeup
	sema_up(&thread_current()->wait);

	// 부모 process가 삭제 작업을 마치기 전까지 sleep
	sema_down(&thread_current()->exit);
}

/* Free the current process's resources. */
static void
process_cleanup(void)
{
	struct thread *curr = thread_current();

#ifdef VM
	supplemental_page_table_kill(&curr->spt);
#endif

	uint64_t *pml4;
	/* Destroy the current process's page directory and switch back
	 * to the kernel-only page directory. */
	pml4 = curr->pml4;
	if (pml4 != NULL)
	{
		/* Correct ordering here is crucial.  We must set
		 * cur->pagedir to NULL before switching page directories,
		 * so that a timer interrupt can't switch back to the
		 * process page directory.  We must activate the base page
		 * directory before destroying the process's page
		 * directory, or our active page directory will be one
		 * that's been freed (and cleared). */
		curr->pml4 = NULL;
		pml4_activate(NULL);
		pml4_destroy(pml4);
	}
}

/* Sets up the CPU for running user code in the nest thread.
 * This function is called on every context switch. */
void process_activate(struct thread *next)
{
	/* Activate thread's page tables. */
	pml4_activate(next->pml4);

	/* Set thread's kernel stack for use in processing interrupts. */
	tss_update(next);
}

/* We load ELF binaries.  The following definitions are taken
 * from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
#define EI_NIDENT 16

#define PT_NULL 0			/* Ignore. */
#define PT_LOAD 1			/* Loadable segment. */
#define PT_DYNAMIC 2		/* Dynamic linking info. */
#define PT_INTERP 3			/* Name of dynamic loader. */
#define PT_NOTE 4			/* Auxiliary info. */
#define PT_SHLIB 5			/* Reserved. */
#define PT_PHDR 6			/* Program header table. */
#define PT_STACK 0x6474e551 /* Stack segment. */

#define PF_X 1 /* Executable. */
#define PF_W 2 /* Writable. */
#define PF_R 4 /* Readable. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
 * This appears at the very beginning of an ELF binary. */
struct ELF64_hdr
{
	unsigned char e_ident[EI_NIDENT];
	uint16_t e_type;
	uint16_t e_machine;
	uint32_t e_version;
	uint64_t e_entry;
	uint64_t e_phoff;
	uint64_t e_shoff;
	uint32_t e_flags;
	uint16_t e_ehsize;
	uint16_t e_phentsize;
	uint16_t e_phnum;
	uint16_t e_shentsize;
	uint16_t e_shnum;
	uint16_t e_shstrndx;
};

struct ELF64_PHDR
{
	uint32_t p_type;
	uint32_t p_flags;
	uint64_t p_offset;
	uint64_t p_vaddr;
	uint64_t p_paddr;
	uint64_t p_filesz;
	uint64_t p_memsz;
	uint64_t p_align;
};

/* Abbreviations */
#define ELF ELF64_hdr
#define Phdr ELF64_PHDR

static bool setup_stack(struct intr_frame *if_);
static bool validate_segment(const struct Phdr *, struct file *);
static bool load_segment(struct file *file, off_t ofs, uint8_t *upage,
						 uint32_t read_bytes, uint32_t zero_bytes,
						 bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
 * Stores the executable's entry point into *RIP
 * and its initial stack pointer into *RSP.
 * Returns true if successful, false otherwise. */
static bool
load(const char *file_name, struct intr_frame *if_)
{
	struct thread *t = thread_current();
	struct ELF ehdr;
	struct file *file = NULL;
	off_t file_ofs;
	bool success = false;
	int i;

	char *save_ptr, *token;
	char *argv[64];
	int argc = 0;

	// argument parsing
	for (token = strtok_r(file_name, " ", &save_ptr); token != NULL; token = strtok_r(NULL, " ", &save_ptr))
		argv[argc++] = token;

	/* Allocate and activate page directory. */
	t->pml4 = pml4_create();
	if (t->pml4 == NULL)
		goto done;
	process_activate(thread_current());

	/* Open executable file. */
	file = filesys_open(argv[0]);
	if (file == NULL)
	{
		printf("load: %s: open failed\n", argv[0]);
		goto done;
	}

	// process를 종료할 때 닫기 위해 file descripter를 따로 저장해 둔다
	thread_current()->run_file = file;

	// 실행하려고 open한 file을 write하지 못하게 한다
	file_deny_write(file);

	/* Read and verify executable header. */
	if (file_read(file, &ehdr, sizeof ehdr) != sizeof ehdr || memcmp(ehdr.e_ident, "\177ELF\2\1\1", 7) || ehdr.e_type != 2 || ehdr.e_machine != 0x3E // amd64
		|| ehdr.e_version != 1 || ehdr.e_phentsize != sizeof(struct Phdr) || ehdr.e_phnum > 1024)
	{
		printf("load: %s: error loading executable\n", argv[0]);
		goto done;
	}

	/* Read program headers. */
	file_ofs = ehdr.e_phoff;
	for (i = 0; i < ehdr.e_phnum; i++)
	{
		struct Phdr phdr;

		if (file_ofs < 0 || file_ofs > file_length(file))
			goto done;
		file_seek(file, file_ofs);

		if (file_read(file, &phdr, sizeof phdr) != sizeof phdr)
			goto done;

		file_ofs += sizeof phdr;

		switch (phdr.p_type)
		{
		case PT_NULL:
		case PT_NOTE:
		case PT_PHDR:
		case PT_STACK:
		default:
			/* Ignore this segment. */
			break;
		case PT_DYNAMIC:
		case PT_INTERP:
		case PT_SHLIB:
			goto done;
		case PT_LOAD:
			if (validate_segment(&phdr, file))
			{
				bool writable = (phdr.p_flags & PF_W) != 0;
				uint64_t file_page = phdr.p_offset & ~PGMASK;
				uint64_t mem_page = phdr.p_vaddr & ~PGMASK;
				uint64_t page_offset = phdr.p_vaddr & PGMASK;
				uint32_t read_bytes, zero_bytes;

				// printf("[%d]phdr.p_flags: %d\n", i, phdr.p_flags);
				// printf("[%d]phdr.p_filesz: %x\n", i, phdr.p_filesz);
				// printf("[%d]phdr.p_memsz: %x\n", i, phdr.p_memsz);
				// printf("[%d]file_page: %p\n", i, file_page);
				// printf("[%d]mem_page: %p\n", i, mem_page);
				// printf("[%d]page_offset: %p\n", i, page_offset);
				// printf("\n");

				if (phdr.p_filesz > 0)
				{
					/* Normal segment.
					 * Read initial part from disk and zero the rest. */
					read_bytes = page_offset + phdr.p_filesz;
					zero_bytes = (ROUND_UP(page_offset + phdr.p_memsz, PGSIZE) - read_bytes);
				}
				else
				{
					/* Entirely zero.
					 * Don't read anything from disk. */
					read_bytes = 0;
					zero_bytes = ROUND_UP(page_offset + phdr.p_memsz, PGSIZE);
				}

				// printf("[%d]read_bytes: %x\n", i, read_bytes);
				// printf("[%d]zero_bytes: %x\n\n", i, zero_bytes);

				if (!load_segment(file, file_page, (void *)mem_page,
								  read_bytes, zero_bytes, writable))
					goto done;
			}
			else
				goto done;
			break;
		}
	}

	/* Set up stack. */
	if (!setup_stack(if_))
		goto done;

	/* Start address. */
	if_->rip = ehdr.e_entry;

	/*
	user stack에 argument를 쌓는다
	%rdi와 %rsi에 argc, argv를 각각 삽입
	*/

	argument_stack(argc, argv, if_);

	success = true;

done:
	/* We arrive here whether the load is successful or not. */
	// file_close(file);
	return success;
}

/* Checks whether PHDR describes a valid, loadable segment in
 * FILE and returns true if so, false otherwise. */
static bool
validate_segment(const struct Phdr *phdr, struct file *file)
{
	/* p_offset and p_vaddr must have the same page offset. */
	if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK))
		return false;

	/* p_offset must point within FILE. */
	if (phdr->p_offset > (uint64_t)file_length(file))
		return false;

	/* p_memsz must be at least as big as p_filesz. */
	if (phdr->p_memsz < phdr->p_filesz)
		return false;

	/* The segment must not be empty. */
	if (phdr->p_memsz == 0)
		return false;

	/* The virtual memory region must both start and end within the
	   user address space range. */
	if (!is_user_vaddr((void *)phdr->p_vaddr))
		return false;
	if (!is_user_vaddr((void *)(phdr->p_vaddr + phdr->p_memsz)))
		return false;

	/* The region cannot "wrap around" across the kernel virtual
	   address space. */
	if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
		return false;

	/* Disallow mapping page 0.
	   Not only is it a bad idea to map page 0, but if we allowed
	   it then user code that passed a null pointer to system calls
	   could quite likely panic the kernel by way of null pointer
	   assertions in memcpy(), etc. */
	if (phdr->p_vaddr < PGSIZE)
		return false;

	/* It's okay. */
	return true;
}

#ifndef VM
/* Codes of this block will be ONLY USED DURING project 2.
 * If you want to implement the function for whole project 2, implement it
 * outside of #ifndef macro. */

/* load() helpers. */
static bool install_page(void *upage, void *kpage, bool writable);

/* Loads a segment starting at offset OFS in FILE at address
 * UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
 * memory are initialized, as follows:
 *
 * - READ_BYTES bytes at UPAGE must be read from FILE
 * starting at offset OFS.
 *
 * - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.
 *
 * The pages initialized by this function must be writable by the
 * user process if WRITABLE is true, read-only otherwise.
 *
 * Return true if successful, false if a memory allocation error
 * or disk read error occurs. */
static bool
load_segment(struct file *file, off_t ofs, uint8_t *upage,
			 uint32_t read_bytes, uint32_t zero_bytes, bool writable)
{
	ASSERT((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT(pg_ofs(upage) == 0);
	ASSERT(ofs % PGSIZE == 0);

	file_seek(file, ofs);
	while (read_bytes > 0 || zero_bytes > 0)
	{
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* Get a page of memory. */
		uint8_t *kpage = palloc_get_page(PAL_USER);
		if (kpage == NULL)
			return false;

		/* Load this page. */
		if (file_read(file, kpage, page_read_bytes) != (int)page_read_bytes)
		{
			palloc_free_page(kpage);
			return false;
		}
		memset(kpage + page_read_bytes, 0, page_zero_bytes);

		/* Add the page to the process's address space. */
		if (!install_page(upage, kpage, writable))
		{
			printf("fail\n");
			palloc_free_page(kpage);
			return false;
		}

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
	}
	return true;
}

/* Create a minimal stack by mapping a zeroed page at the USER_STACK */
static bool
setup_stack(struct intr_frame *if_)
{
	uint8_t *kpage;
	bool success = false;

	kpage = palloc_get_page(PAL_USER | PAL_ZERO);
	if (kpage != NULL)
	{
		success = install_page(((uint8_t *)USER_STACK) - PGSIZE, kpage, true);
		if (success)
			if_->rsp = USER_STACK;
		else
			palloc_free_page(kpage);
	}
	return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
 * virtual address KPAGE to the page table.
 * If WRITABLE is true, the user process may modify the page;
 * otherwise, it is read-only.
 * UPAGE must not already be mapped.
 * KPAGE should probably be a page obtained from the user pool
 * with palloc_get_page().
 * Returns true on success, false if UPAGE is already mapped or
 * if memory allocation fails. */
static bool
install_page(void *upage, void *kpage, bool writable)
{
	struct thread *t = thread_current();

	/* Verify that there's not already a page at that virtual
	 * address, then map our page there. */
	return (pml4_get_page(t->pml4, upage) == NULL && pml4_set_page(t->pml4, upage, kpage, writable));
}
#else
/* From here, codes will be used after project 3.
 * If you want to implement the function for only project 2, implement it on the
 * upper block. */

struct file_info
{
	struct file *file;
	off_t ofs;
	uint32_t page_read_bytes;
	uint32_t page_zero_bytes;
};

static bool
lazy_load_segment(struct page *page, void *aux)
{
	/* TODO: Load the segment from the file */
	/* TODO: This called when the first page fault occurs on address VA. */
	/* TODO: VA is available when calling this function. */
	// aux로 전달 받은 file data
	struct file_info *file_info = (struct file_info *)aux;
	int page_read_bytes = file_info->page_read_bytes;

	// load해야할 부분으로 file ofs 변경
	file_seek(file_info->file, file_info->ofs);

	// laod segment
	if (file_read(file_info->file, page->frame->kva, page_read_bytes) != page_read_bytes)
		return false;

	// setup zero bytes space
	memset(page->frame->kva + page_read_bytes, 0, file_info->page_zero_bytes);

	return true;
}

/* Loads a segment starting at offset OFS in FILE at address
 * UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
 * memory are initialized, as follows:
 *
 * - READ_BYTES bytes at UPAGE must be read from FILE
 * starting at offset OFS.
 *
 * - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.
 *
 * The pages initialized by this function must be writable by the
 * user process if WRITABLE is true, read-only otherwise.
 *
 * Return true if successful, false if a memory allocation error
 * or disk read error occurs. */
static bool
load_segment(struct file *file, off_t ofs, uint8_t *upage,
			 uint32_t read_bytes, uint32_t zero_bytes, bool writable)
{
	ASSERT((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT(pg_ofs(upage) == 0);
	ASSERT(ofs % PGSIZE == 0);

	// read_bytes와 zero_bytes가 모두 0일 때 = 현재 laod segment에 모두 page 할당을 했을 때
	while (read_bytes > 0 || zero_bytes > 0)
	{
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/*
		TODO: Set up aux to pass information to the lazy_load_segment.
		aux를 통해 file data 전달을 위해 만든 struct file_info
		*/
		struct file_info *file_info = (struct file_info *)malloc(sizeof(struct file_info));

		file_info->file = file;
		file_info->ofs = ofs;
		file_info->page_read_bytes = page_read_bytes;
		file_info->page_zero_bytes = page_zero_bytes;

		/*
		해당 virtual memory(=upage)에 struct page를 할당해줌
		load 되기 전에는 uninit page

		page fault로 pysical memory로 load될 때 할당될 때 입력 받은 page type으로 변환하고
		lazy_load_segment 함수를 실행시켜서 upload함
		*/
		if (!vm_alloc_page_with_initializer(VM_ANON, upage, writable, lazy_load_segment, file_info))
			return false;

		/* Advance. */
		read_bytes -= page_read_bytes; // 남은 read_bytes 갱신
		zero_bytes -= page_zero_bytes; // 남은 zero_bytes 갱신
		upage += PGSIZE;			   // virtual address를 옮겨서 다음 page space를 가리키게 함
		ofs += PGSIZE;				   // 다음 page에 mapping시킬 file 위치 갱신
	}
	return true;
}

/* Create a PAGE of stack at the USER_STACK. Return true on success. */
static bool
setup_stack(struct intr_frame *if_)
{
	struct thread *curr = thread_current();

	/*
	stack은 위에서 아래로 커진다
	stack의 시작 위치는 USER_STACK
	PGSIZE만큼 빼서 page space 확보 -> 해당 page 시작 virtual address = stack_botton
	*/
	void *stack_bottom = (void *)(((uint8_t *)USER_STACK) - PGSIZE);

	/* TODO: Map the stack on stack_bottom and claim the page immediately.
	 * TODO: If success, set the rsp accordingly.
	 * TODO: You should mark the page is stack. */
	/* TODO: Your code goes here */

	// 확보한 page space에 struct page 할당
	if (!vm_alloc_page_with_initializer(VM_ANON, stack_bottom, true, NULL, NULL))
		return false;

	// page를 pysical memory에 바로 올림 -> 바로 argument들을 stack에 쌓아야하기 때문에 lazy load할 필요 없음
	if (!vm_claim_page(stack_bottom))
		return false;

	// stack pointer 설정
	if_->rsp = USER_STACK;

	return true;
}
#endif /* VM */
