/*
 * mm_alloc.c
 *
 * Stub implementations of the mm_* routines. Remove this comment and provide
 * a summary of your allocator's design here.
 */

#include "mm_alloc.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>

 #include <sys/time.h>
#include <sys/resource.h>

#define BLOCK_SIZE sizeof(struct s_block) /*size of s_block */

s_block_ptr start = NULL; /* start of linked list */
s_block_ptr end = NULL; /* last block in linked list */

void* mm_malloc(size_t size)
{
	/* case when size is 0 */
	if (size == 0) {
		return NULL;
	}

	s_block_ptr block;
	
	/* check if can used previously allocated block that is now free */
	if ((block = find_free_space(size)) == NULL) {
		/* extend from the bottom of the heap if can't find free block big enough*/
		block = extend_heap(end, size);

		/* check if extend_heap failed */
		if (!block) {
			return NULL;
		}
	} 
	
	/* If using block that was previously allocated */
	else {
		/* split block if the size is too big*/


		block->free = 0;
		block->size = size;
	}
	memset(block->data, 0, size);
	return block->data;
}

void* mm_realloc(void* ptr, size_t size)
{
	/* if pointer is null, realloc is the same as malloc */
	if (!ptr) {
		return mm_malloc(size);
	}

	/* if pointer is not null and size equals zero, call is the same as free */
	if (size == 0) {
		mm_free(ptr);
		return NULL;
	}

	/* if block is bigger than SIZE, don't need to get more space*/
	s_block_ptr block = get_block(ptr);
	if (block->size >= size) {
		return ptr;
	}

	void* new_data = mm_malloc(size);
	if (!new_data) {
		return NULL;
	}
	memcpy(new_data, ptr, size);
	mm_free(ptr);
	return new_data;
}

void mm_free(void* ptr)
{
	/* do nothing if pointer is null */
	if (!ptr) {
		return;
	}
	s_block_ptr block = get_block(ptr);
	block->free = 1;

	/* check if the freed block can be combined */
	fusion(block);
}

/**
 * Given a pointer to the data member of a struct s_block, return a pointer to
 * the struct s_block.
 */
s_block_ptr get_block(void *ptr) {
    return (s_block_ptr) ( ((unsigned char*)ptr) - sizeof(struct s_block) );
}

/* Add a new block at the end of the heap,
 * return NULL if things go wrong
 */
s_block_ptr extend_heap (s_block_ptr last , size_t s) {
	s_block_ptr block;
	block = sbrk(s + BLOCK_SIZE);
	if (block == (void*) -1) {
		return NULL;
	}
	block->free = 0;
	block->size = s;
	block->prev = last;

	/* if allocating the first block in heap */
	if (!last) {		
		start = block;
	} else {
		last->next = block;
	}
	end = block;

	return block;
}

/* Find previously allocated blocks that are now free 
	and are at least size SIZE */
s_block_ptr find_free_space(size_t size) {

	s_block_ptr curr = start;
	while (curr) {
		if (curr->free && curr->size >= size) {
			return curr;
		}
		curr = curr->next;
	}
	return curr;
}

/* Try fusing block with neighbors */
s_block_ptr fusion(s_block_ptr b) {

	/* make sure b is free before continuing*/
	if (!b->free) {
		return NULL;
	}

	/* if b->prev is not null and free,
		combine b->prev and b's sizes 
		and remove b->prev from list  */
	if (b->prev && b->prev->free) {
		b->size += b->prev->size;
		b->prev = b->prev->prev;
		if (b->prev) {
			b->prev->next = b;
		}
	}

	/* if b->next is not null and free,
		combine b->next and b's sizes 
		and remove b->next from list  */
	if (b->next && b->next->free) {
		b->size += b->next->size;
		b->next = b->next->next;
		if (b->next) {
			b->next->prev = b;
		}
	}

	return b;
}
