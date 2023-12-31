#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"

#include "threads/init.h"
#include "threads/init.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "threads/synch.h"
#include "userprog/process.h"
#include "threads/palloc.h"

void syscall_entry(void);
void syscall_handler(struct intr_frame *);

bool create(const char *file, unsigned initial_size);
bool remove(const char *file);

struct lock filesys_lock;

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
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48 |
							((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t)syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			  FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);

	lock_init(&filesys_lock);
}

/* The main system call interface */
void syscall_handler(struct intr_frame *f UNUSED)
{
// TODO: Your implementation goes here.
#ifdef VM
	thread_current()->rsp_stack = f->rsp;
#endif

	switch (f->R.rax)
	{
	case SYS_HALT:
		halt();
		break;
	case SYS_EXIT:
		exit(f->R.rdi);
		break;
	case SYS_FORK:
		f->R.rax = fork(f->R.rdi, f);
		break;
	case SYS_EXEC:
		if (exec(f->R.rdi) == -1)
		{
			exit(-1);
		}
		break;
	case SYS_WAIT:
		f->R.rax = process_wait(f->R.rdi);
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
		// check_valid_buffer(f->R.rsi, f->R.rdx, f->rsp, 1);
		f->R.rax = read(f->R.rdi, f->R.rsi, f->R.rdx);
		break;
	case SYS_WRITE:
		// check_valid_buffer(f->R.rsi, f->R.rdx, f->rsp, 0);
		f->R.rax = write(f->R.rdi, f->R.rsi, f->R.rdx);
		break;
	case SYS_SEEK:
		seek(f->R.rdi, f->R.rsi);
		break;
	case SYS_TELL:
		f->R.rax = tell(f->R.rdi);
		break;
	case SYS_CLOSE:
		close(f->R.rdi);
		break;
	case SYS_MMAP:
		f->R.rax = mmap(f->R.rdi, f->R.rsi, f->R.rdx, f->R.r10, f->R.r8);
		break;
	case SYS_MUNMAP:
		munmap(f->R.rdi);
		break;
	default:
		exit(-1);
		break;
	}
}

void halt(void)
{
	power_off();
}

void exit(int status)
{
	struct thread *curr = thread_current();
	curr->exit_status = status;
	printf("%s: exit(%d)\n", thread_name(), status);
	thread_exit();
}

int open(const char *file)
{
	check_address(file);
	struct file *open_file = filesys_open(file);

	if (open_file == NULL)
	{
		return -1;
	}
	int fd = process_add_file(open_file);
	if (fd == -1)
	{
		file_close(open_file);
	}
	return fd;
}

int filesize(int fd)
{
	struct file *file = process_get_file(fd);

	if (file == NULL)
	{
		return -1;
	}
	return file_length(file);
}

int read(int fd, void *buffer, unsigned size)
{
	check_address(buffer);
	unsigned char *buf = buffer;
	int read_size;
	struct thread *curr = thread_current();

	if (fd == 1)
	{
		return -1;
	}
	else if (fd == 0)
	{
		char key;
		for (read_size = 0; read_size < size; read_size++)
		{
			key = input_getc();
			*buf++ = key;
			if (key == "\n")
			{
				break;
			}
		}
	}
	else
	{
		struct file *file = process_get_file(fd);
		if (file == NULL)
		{
			return -1;
		}
		lock_acquire(&filesys_lock);
		read_size = file_read(file, buffer, size);
		lock_release(&filesys_lock);
	}
	return read_size;
}

int write(int fd, const void *buffer, unsigned size)
{
	check_address(buffer);
	int write_size;

	if (fd == 0)
	{
		return -1;
	}
	else if (fd == 1)
	{
		putbuf(buffer, size);
		write_size = size;
	}
	else
	{
		struct file *file = process_get_file(fd);
		if (file == NULL)
		{
			return -1;
		}
		lock_acquire(&filesys_lock);
		write_size = file_write(file, buffer, size);
		lock_release(&filesys_lock);
	}
	return write_size;
}

void seek(int fd, unsigned position)
{
	struct file *file = process_get_file(fd);

	if (fd < 2)
	{
		return;
	}
	return file_seek(file, position);
}

unsigned tell(int fd)
{
	struct file *file = process_get_file(fd);

	if (fd < 2)
	{
		return;
	}
	return file_tell(file);
}

void close(int fd)
{
	struct file *file = process_get_file(fd);

	if (file == NULL)
	{
		return;
	}
	process_close_file(fd);
}

bool create(const char *file, unsigned initial_size)
{
	check_address(file);
	return filesys_create(file, initial_size);
}

bool remove(const char *file)
{
	check_address(file);
	return filesys_remove(file);
}

int exec(char *file_name)
{
	check_address(file_name);
	int file_size = strlen(file_name) + 1;
	char *fn_copy = palloc_get_page(PAL_ZERO);
	if (fn_copy == NULL)
	{
		exit(-1);
	}
	strlcpy(fn_copy, file_name, file_size); // file 이름만 복사
	if (process_exec(fn_copy) == -1)
	{
		return -1;
	}
	NOT_REACHED();
	return 0;
}

tid_t fork(const char *thread_name, struct intr_frame *f)
{
	return process_fork(thread_name, f);
}

int wait(tid_t pid)
{
	process_wait(pid);
}

void check_address(void *addr)
{
	if (is_kernel_vaddr(addr) || !(addr))
	{
		exit(-1);
	}
}

void *mmap(void *addr, size_t length, int writable, int fd, off_t offset)
{
	if (offset % PGSIZE != 0)
	{
		return NULL;
	}

	if (pg_round_down(addr) != addr || is_kernel_vaddr(addr) || addr == NULL || (long long)length <= 0)
		return NULL;

	if (fd == 0 || fd == 1)
		exit(-1);

	if (spt_find_page(&thread_current()->spt, addr))
		return NULL;

	struct file *file = process_get_file(fd);

	if (file == NULL || file_length(file) <= 0)
		return NULL;

	return do_mmap(addr, length, writable, file, offset);
}

void munmap(void *addr)
{
	do_munmap(addr);
}
