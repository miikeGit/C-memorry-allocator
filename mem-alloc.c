#include <unistd.h>
#include <thread_db.h>
#include <memory.h>

typedef char ALIGN[16];

union header { // We use Union to make sure that we align with memory block (16 bytes)
	struct {
		size_t size;
		unsigned is_free;
		union header* next;
	} s; // declare struct
	ALIGN stub;
};
typedef union header header_t;

header_t *head, *tail;

header_t* get_free_block(size_t size) {
	header_t* curr = head;

	while(curr) {
		if (curr->s.is_free && curr->s.size >= size) {
			return curr;
		}
		curr = curr->s.next;
	}
	return NULL; // No available memory block found
}

pthread_mutex_t global_malloc_lock;

void* malloc(size_t size) {
	if (size == 0) return NULL; // Requested memmory is zero, exit
	
	pthread_mutex_lock(&global_malloc_lock);
	header_t* header = get_free_block(size);
	
	if (header) { // Free memory block found within previously used pool, reuse it
		header->s.is_free = 0;
		pthread_mutex_unlock(&global_malloc_lock);
		return (void*)(header+1); // Hide header by moving pointer by one byte to the right,
								  // this is incidentally also the first byte of the actual memory block. 
	}
	// No available block found, extend heap
	size_t total_size = sizeof(header_t) + size;
	void* block = sbrk(total_size);
	
	if (block == (void*)-1) { // Check if memory is successfully allocated, exit otherwise
		pthread_mutex_unlock(&global_malloc_lock);
		return NULL;
	}

	// Create new header object and add it to the list
	header = block;
	header->s.is_free = 0;
	header->s.next = NULL;
	header->s.size = size;

	if (!head) head = header; // Assign head if list is empty
	if (tail) tail->s.next = header;
	tail = header;

	pthread_mutex_unlock(&global_malloc_lock);
	return (void*)(header+1);
}

void free(void* block) {
	if (!block) return; // Block is already free, exit

	pthread_mutex_lock(&global_malloc_lock);
	
	void* heap_break = sbrk(0); // Get adress of heap break
	
	header_t *header = (void*)(block - 1); // Assign pointer to header by moving it one byte to the left

	if ((char*)block + header->s.size == heap_break) { // Check if given block is at the end of heap
		if (head == tail) { // List consists only of one element
			head = tail = NULL;
		} else { // Delete tail
			header_t *curr = head;
			while(curr->s.next != tail) {
				curr = curr->s.next;
			}
			curr->s.next = NULL;
			tail = curr;
		}

		sbrk(-sizeof(header_t*) - header->s.size); // Shrink heap
	} else {
		header->s.is_free = 1; // Do not alter heap, mark block as free
	}
	pthread_mutex_unlock(&global_malloc_lock);
}

// Allocate memory for an array of num elements of nsize bytes each
void* calloc(size_t num, size_t nsize) {
	if (!num || !nsize) return NULL;
	pthread_mutex_lock(&global_malloc_lock);
	
	size_t size = num * nsize;

	if (nsize != size / num) return NULL; // Check for overflow
	
	void *block = malloc(size);
	if (!block) return NULL;

	memset(block, 0, size); // Set memory to zero
	pthread_mutex_unlock(&global_malloc_lock);
	return block; // Return a pointer to the allocated memory
}

void* realloc(void* block, size_t size) {
	if (!block || !size) return NULL;
	pthread_mutex_lock(&global_malloc_lock);

	header_t *header = block - 1;

	if (header->s.size >= size) { // Block already has requested size
		pthread_mutex_unlock(&global_malloc_lock);
		return block;
	}

	void* ret = malloc(size);
	if (ret) {
		memcpy(ret, block, header->s.size); // Relocate contents to the new bigger block
		free(block);
	}
	return ret;
}

int main() {
	return 0;
}
