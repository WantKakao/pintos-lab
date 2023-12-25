/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
#include "threads/vaddr.h"
#include "filesys/file.h"

static bool file_backed_swap_in(struct page *page, void *kva);
static bool file_backed_swap_out(struct page *page);
static void file_backed_destroy(struct page *page);

static bool lazy_load_segment_for_mmap(struct page *page, void *aux);

/* DO NOT MODIFY this struct */
static const struct page_operations file_ops = {
	.swap_in = file_backed_swap_in,
	.swap_out = file_backed_swap_out,
	.destroy = file_backed_destroy,
	.type = VM_FILE,
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

/* Do the mmap */
void *
do_mmap(void *addr, size_t length, int writable,
		struct file *file, off_t offset)
{
	// int fd = open(file);
	// if (fd == 0 || fd == 1 || length <= 0 || !(addr))
	// 	return NULL;
	// if (spt_find_page(&thread_current()->spt, addr))
	// 	return NULL;
	// uint32_t read_bytes;
	// uint32_t zero_bytes;
	// load_segment(file, offset, addr, read_bytes, zero_bytes, writable);

	void *start_addr = addr;
	/* 주어진 파일 길이와 length를 비교해서 length보다 file 크기가 작으면 파일 통으로 싣고 파일 길이가 더 크면 주어진 length만큼만 load*/
	size_t read_bytes = length > file_length(file) ? file_length(file) : length;
	size_t zero_bytes = PGSIZE - read_bytes % PGSIZE; // 마지막 페이지에 들어갈 자투리 바이트

	/* 파일을 페이지 단위로 잘라 해당 파일의 정보들을 container 구조체에 저장한다.
	   FILE-BACKED 타입의 UINIT 페이지를 만들어 lazy_load_segment()를 vm_init으로 넣는다. */
	while (read_bytes > 0 || zero_bytes > 0)
	{
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		struct aux_val *lazy_load_arg = (struct aux_val *)malloc(sizeof(struct aux_val));
		lazy_load_arg->file = file;
		lazy_load_arg->offset = offset;
		lazy_load_arg->read_bytes = page_read_bytes;
		// 여기서는 페이지 할당을 FILE-BACKED로 해줘야 하니 아래 VM_FILE로 넣어준다.
		if (!vm_alloc_page_with_initializer(VM_FILE, addr, writable, lazy_load_segment_for_mmap, lazy_load_arg))
		{
			return NULL;
		}
		// 다음 페이지로 이동
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		addr += PGSIZE;
		offset += page_read_bytes;
	}
	return start_addr;
}

/* Do the munmap */
void do_munmap(void *addr)
{
	while (true)
	{
		struct page *page = spt_find_page(&thread_current()->spt, addr);

		if (page == NULL)
		{
			return NULL;
		}

		struct aux_val *aux = (struct aux_val *)page->uninit.aux;

		/* 수정된 페이지(dirty bit == 1)는 파일에 업데이트해놓는다. 이후에 dirty bit을 0으로 만든다. */
		if (pml4_is_dirty(thread_current()->pml4, page->va))
		{
			file_write_at(aux->file, addr, aux->read_bytes, aux->offset);
			pml4_set_dirty(thread_current()->pml4, page->va, 0);
		}

		/* present bit을 0으로 만든다. */
		pml4_clear_page(thread_current()->pml4, page->va);
		addr += PGSIZE;
	}
}

static bool lazy_load_segment_for_mmap(struct page *page, void *aux)
{
	/* TODO: Load the segment from the file */
	/* TODO: This called when the first page fault occurs on address VA. */
	/* TODO: VA is available when calling this function. */
	/* Get a page of memory. */
	struct aux_val *lazy_load_arg = (struct aux_val *)aux;

	struct file *file = lazy_load_arg->file;
	uint8_t *kpage = page->frame->kva;
	size_t page_read_bytes = lazy_load_arg->read_bytes;
	size_t page_zero_bytes = PGSIZE - page_read_bytes;
	off_t offset = lazy_load_arg->offset;

	file_seek(file, offset);

	/* Load this page. */
	if (file_read(file, kpage, page_read_bytes) != (int)page_read_bytes)
	{
		palloc_free_page(kpage);
		free(lazy_load_arg);
		return false;
	}
	memset(kpage + page_read_bytes, 0, page_zero_bytes);

	return true;
}