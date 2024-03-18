#include "userfs.h"
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

enum {
	BLOCK_SIZE = 512,
	MAX_FILE_SIZE = 1024 * 1024 * 100,
};

/** Global error code. Set from any function on any error. */
static enum ufs_error_code ufs_error_code = UFS_ERR_NO_ERR;

struct block {
	/** Block memory. */
	char *memory;
	/** How many bytes are occupied. */
	int occupied;
	/** Next block in the file. */
	struct block *next;
	/** Previous block in the file. */
	struct block *prev;

	/* PUT HERE OTHER MEMBERS */
};

struct file {
	/** Double-linked list of file blocks. */
	struct block *block_list;
	/**
	 * Last block in the list above for fast access to the end
	 * of file.
	 */
	struct block *last_block;
	/** How many file descriptors are opened on the file. */
	int refs;
	/** File name. */
	char *name;
	/** Files are stored in a double-linked list. */
	struct file *next;
	struct file *prev;

	/* PUT HERE OTHER MEMBERS */
};

/** List of all files. */
static struct file *file_list = NULL;

struct filedesc {
	struct file *file;

	/* PUT HERE OTHER MEMBERS */
};

/**
 * An array of file descriptors. When a file descriptor is
 * created, its pointer drops here. When a file descriptor is
 * closed, its place in this array is set to NULL and can be
 * taken by next ufs_open() call.
 */
static struct filedesc **file_descriptors = NULL;
static int file_descriptor_count = 0;
static int file_descriptor_capacity = 0;

enum ufs_error_code ufs_errno() {
	return ufs_error_code;
}

struct file *find_file(const char *filename) {
    struct file *curr = file_list;
    while (curr != NULL) {
        if (strcmp(curr->name, filename) == 0) {
            return curr;
        }
        curr = curr->next;
    }
    return NULL;
}

struct file *create_file(const char *filename) {
    struct file *new_file = (struct file*)malloc(sizeof(struct file));
    if (new_file == NULL) {
        return NULL;
    }

    new_file->name = strdup(filename);
    if (new_file->name == NULL) {
        free(new_file);
        return NULL;
    }
    new_file->block_list = NULL;
    new_file->last_block = NULL;
    new_file->refs = 0;
    new_file->next = file_list;
    new_file->prev = NULL;

    if (file_list != NULL) {
        file_list->prev = new_file;
    }
    file_list = new_file;

    return new_file;
}

struct filedesc *allocate_filedesc() {
    if (file_descriptor_count == file_descriptor_capacity) {
        int new_capacity = file_descriptor_capacity * 2 + 1;
        struct filedesc **new_descriptors = (struct filedesc **) realloc(file_descriptors, new_capacity * sizeof(struct filedesc *));
        if (new_descriptors == NULL) {
            return NULL;
        }
        file_descriptors = new_descriptors;
        file_descriptor_capacity = new_capacity;
    }

    struct filedesc *filedesc = (struct filedesc *) malloc(sizeof(struct filedesc));
    if (filedesc == NULL) {
        return NULL;
    }

    file_descriptors[file_descriptor_count++] = filedesc;

    return filedesc;
}

struct block *create_block() {
    struct block *new_block = (struct block *)malloc(sizeof(struct block));
    if (new_block == NULL) {
        return NULL;
    }

    new_block->memory = (char *)malloc(BLOCK_SIZE);
    if (new_block->memory == NULL) {
        free(new_block);
        return NULL;
    }
    new_block->occupied = 0;
    new_block->next = NULL;
    new_block->prev = NULL;
    return new_block;
}

int ufs_open(const char *filename, int flags) {
    struct file *file = find_file(filename);
    if (file == NULL) {
        if (flags & UFS_CREATE) {
            file = create_file(filename);
            if (file == NULL) {
                ufs_error_code = UFS_ERR_NO_MEM;
                return -1;
            }
        } else {
            ufs_error_code = UFS_ERR_NO_FILE;
            return -1;
        }
    }

    struct filedesc *filedesc = allocate_filedesc();
    if (filedesc == NULL) {
        ufs_error_code = UFS_ERR_NO_MEM;
        return -1;
    }

    filedesc->file = file;
    if (!(flags & UFS_CREATE)) {
        filedesc->file->last_block = NULL;
    }

    return file_descriptor_count - 1;
}


ssize_t ufs_write(int fd, const char *buf, size_t size) {
  if (fd < 0 || fd >= file_descriptor_count || file_descriptors[fd] == NULL) {
    ufs_error_code = UFS_ERR_NO_FILE;
    return -1;
  }
  struct filedesc *filedesc = file_descriptors[fd];
  struct file *file = filedesc->file;

  struct block *cur_block;
  if (file->last_block == NULL) {
    cur_block = create_block();
    file->block_list = cur_block;
    file->last_block = cur_block;
  } else {
    cur_block = file->last_block;
  }

  size_t bytes_written = 0;
  size_t offset = 0;

  while (bytes_written < size) {
    size_t remaining_space = BLOCK_SIZE - cur_block->occupied;
    size_t bytes_to_write;

    if (remaining_space > (size - bytes_written)) {
      bytes_to_write = size - bytes_written;
    } else {
      bytes_to_write = remaining_space;
    }

    memcpy(cur_block->memory + cur_block->occupied, buf + offset, bytes_to_write);
    cur_block->occupied += bytes_to_write;
    bytes_written += bytes_to_write;
    offset += bytes_to_write;

    if (bytes_written < size) {
      struct block *new_block = create_block();
      cur_block->next = new_block;
      new_block->prev = cur_block;
      cur_block = new_block;
      file->last_block = cur_block;
    }
  }

  return bytes_written;
}

ssize_t ufs_read(int fd, char *buf, size_t size) {
  if (fd < 0 || fd >= file_descriptor_count || file_descriptors[fd] == NULL) {
    ufs_error_code = UFS_ERR_NO_FILE;
    return -1;
  }
  struct filedesc *filedesc = file_descriptors[fd];
  struct file *file = filedesc->file;

  size_t bytes_read = 0;
  struct block *cur_block = file->block_list;

  while (bytes_read < size && cur_block != NULL) {
    size_t bytes_to_read = cur_block->occupied;
    if (bytes_to_read > size - bytes_read) {
      bytes_to_read = size - bytes_read;
    }
    memcpy(buf + bytes_read, cur_block->memory, bytes_to_read);
    bytes_read += bytes_to_read;
    cur_block = cur_block->next;
  }

  if (bytes_read == 0) {
    ufs_error_code = UFS_ERR_NO_FILE;
  } else {
    ufs_error_code = UFS_ERR_NO_ERR;
  }

  return bytes_read;
}


void remove_block_from_file(struct block *block, struct file *file) {
    if (block->prev == NULL) {
        file->block_list = block->next;
    } else {
        block->prev->next = block->next;
    }

    if (block->next == NULL) {
        file->last_block = block->prev;
    } else {
        block->next->prev = block->prev;
    }

    free(block->memory);
    free(block);
}

void delete_file(struct file *file) {
    struct block *current_block = file->block_list;
    while (current_block != NULL) {
        struct block *next_block = current_block->next;
        remove_block_from_file(current_block, file);
        current_block = next_block;
    }

    if (file->prev == NULL) {
        file_list = file->next;
    } else {
        file->prev->next = file->next;
    }

    if (file->next != NULL) {
        file->next->prev = file->prev;
    }

    free(file->name);
    free(file);
}

int ufs_close(int fd) {
    if (fd < 0 || fd >= file_descriptor_count || file_descriptors[fd] == NULL) {
        ufs_error_code = UFS_ERR_NO_FILE;
        return -1;
    }

    struct filedesc *filedesc = file_descriptors[fd];
    struct file *file = filedesc->file;
    filedesc->file = NULL;

    file->refs--;
    if (file->refs == 0) {
        delete_file(file);
    }

    free(filedesc);
    file_descriptors[fd] = NULL;

    return 0;
}

int ufs_delete(const char *filename) {
    struct file *file = find_file(filename);
    if (file == NULL) {
        ufs_error_code = UFS_ERR_NO_FILE;
        return -1;
    }

    if (file->refs > 0) {
        return -1;
    }

    delete_file(file);

    return 0;
}


void ufs_destroy(void) {
  struct file *cur_file = file_list;
  while (cur_file != NULL) {
    struct block *cur_block = cur_file->block_list;
    while (cur_block != NULL) {
      struct block *next_block = cur_block->next;
      free(cur_block->memory);
      free(cur_block);
      cur_block = next_block;
    }

    struct file *next_file = cur_file->next;
    free(cur_file->name);
    free(cur_file);
    cur_file = next_file;
  }

  free(file_descriptors);
}


