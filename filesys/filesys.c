#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "threads/thread.h"
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/fat.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "filesys/fsutil.h"
#include "devices/disk.h"

#define EFILESYS
/* The disk that contains the file system. */
struct disk *filesys_disk;

static void do_format(void);

/* Initializes the file system module.
 * If FORMAT is true, reformats the file system. */
void filesys_init(bool format)
{
	filesys_disk = disk_get(0, 1);
	if (filesys_disk == NULL)
		PANIC("hd0:1 (hdb) not present, file system initialization failed");

	inode_init();

#ifdef EFILESYS
	fat_init();

	if (format)
		do_format();

	fat_open();

	thread_current()->curr_dir = dir_open_root();
#else
	/* Original FS */
	free_map_init();

	if (format)
		do_format();

	free_map_open();
#endif
}

/* Shuts down the file system module, writing any unwritten data
 * to disk. */
void filesys_done(void)
{
	/* Original FS */
#ifdef EFILESYS
	fat_close();
#else
	free_map_close();
#endif
}

/* Creates a file named NAME with the given INITIAL_SIZE.
 * Returns true if successful, false otherwise.
 * Fails if a file named NAME already exists,
 * or if internal memory allocation fails. */
bool filesys_create(const char *name, off_t initial_size)
{
	struct thread *curr = thread_current();

	char *copy_name = (char *)malloc(strlen(name) + 1);
	strlcpy(copy_name, name, strlen(name) + 1);

	/* Root Directory open */
	char file_name[NAME_MAX + 1];
	struct dir *dir = parse_path(copy_name, file_name);

	/* struct disk_inode를 저장할 새로운 cluster 할당 */
	cluster_t inode_cluster = fat_create_chain(0);
	disk_sector_t inode_sector = cluster_to_sector(inode_cluster);

	/* 할당 받은 cluster에 inode를 만들고 directory에 file 추가 */
	bool success = (dir != NULL && inode_create(inode_sector, initial_size, INODE_FILE) && dir_add(dir, file_name, inode_sector));
	if (!success && inode_cluster != 0)
		fat_remove_chain(inode_cluster, 0);

	dir_close(dir);
	free(copy_name);

	return success;
}

/* Opens the file with the given NAME.
 * Returns the new file if successful or a null pointer
 * otherwise.
 * Fails if no file named NAME exists,
 * or if an internal memory allocation fails. */
struct file *
filesys_open(const char *name)
{
	struct thread *curr = thread_current();
	struct inode *inode = NULL;

	char copy_name[512];
	strlcpy(copy_name, name, strlen(name) + 1);

	char file_name[NAME_MAX + 1];
	char *link_path;

	struct dir *dir = parse_path(copy_name, file_name);

	if (dir != NULL)
		dir_lookup(dir, file_name, &inode);

	while (inode != NULL && inode_is_link(inode))
	{
		link_path = inode_get_link_name(inode);
		strlcpy(copy_name, link_path, strlen(link_path) + 1);

		dir_close(dir);
		dir = parse_path(copy_name, file_name);

		if (dir != NULL)
			dir_lookup(dir, file_name, &inode);
	}

	dir_close(dir);

	return file_open(inode);
}

/* Deletes the file named NAME.
 * Returns true if successful, false on failure.
 * Fails if no file named NAME exists,
 * or if an internal memory allocation fails. */
bool filesys_remove(const char *name)
{
	struct thread *curr = thread_current();
	bool success = false;

	char *copy_name = (char *)malloc(strlen(name) + 1);
	strlcpy(copy_name, name, strlen(name) + 1);

	char file_name[NAME_MAX + 1];
	struct dir *dir = parse_path(copy_name, file_name);

	struct inode *inode;
	dir_lookup(dir, file_name, &inode);

	if (inode_is_dir(inode) == INODE_DIR)
	{
		struct dir *target_dir = dir_open(inode);
		char temp_name[NAME_MAX + 1];

		if (!dir_readdir(target_dir, temp_name))
			success = dir_remove(dir, file_name);

		dir_close(target_dir);
	}
	else
	{
		inode_close(inode);
		success = dir_remove(dir, file_name);
	}

	dir_close(dir);
	free(copy_name);

	return success;
}

/* Formats the file system. */
static void
do_format(void)
{
	printf("Formatting file system...");

#ifdef EFILESYS
	/* Create FAT and save it to the disk. */
	fat_create();

	/* Root Directory 생성 */
	disk_sector_t root = cluster_to_sector(ROOT_DIR_CLUSTER);
	if (!dir_create(root, 16))
		PANIC("root directory creation failed");

	struct dir *root_dir = dir_open_root();
	dir_add(root_dir, ".", root);
	dir_add(root_dir, "..", root);
	dir_close(root_dir);

	fat_close();
#else
	free_map_create();
	if (!dir_create(ROOT_DIR_SECTOR, 16))
		PANIC("root directory creation failed");

	free_map_close();
#endif

	printf("done.\n");
}

struct dir *parse_path(char *path_name, char *file_name)
{
	// printf("[DEBUG][parse_path]path_name: %s\n", path_name);

	struct thread *curr = thread_current();
	struct dir *dir;

	char path[512];
	strlcpy(path, path_name, strlen(path_name) + 1);

	if (path == NULL || file_name == NULL)
		return NULL;

	if (strlen(path) == 0)
		return NULL;

	if (path[0] == '/')
		dir = dir_open_root();
	else
		dir = dir_reopen(curr->curr_dir);

	char *token, *next_token, *save_ptr;
	token = strtok_r(path, "/", &save_ptr);
	next_token = strtok_r(NULL, "/", &save_ptr);

	struct inode *inode = NULL;
	while (token != NULL && next_token != NULL)
	{
		if (!dir_lookup(dir, token, &inode))
			goto fail;

		if (inode_is_link(inode))
		{
			char *link_name = inode_get_link_name(inode);
			strlcpy(path, link_name, strlen(link_name) + 1);

			strlcat(path, "/", strlen(path) + 2);
			strlcat(path, next_token, strlen(path) + strlen(next_token) + 1);
			strlcat(path, save_ptr, strlen(path) + strlen(save_ptr) + 1);

			dir_close(dir);

			if (path[0] == '/')
				dir = dir_open_root();
			else
				dir = dir_reopen(curr->curr_dir);

			token = strtok_r(path, "/", &save_ptr);
			next_token = strtok_r(NULL, "/", &save_ptr);

			continue;
		}

		if (inode_is_dir(inode) == INODE_FILE)
			goto fail;

		dir_close(dir);

		dir = dir_open(inode);

		token = next_token;
		next_token = strtok_r(NULL, "/", &save_ptr);
	}

	if (token == NULL)
		strlcpy(file_name, ".", 2);
	else
	{
		if (strlen(token) > NAME_MAX)
			goto fail;

		strlcpy(file_name, token, strlen(token) + 1);
	}

	return dir;

fail:
	dir_close(dir);
	return NULL;
}

bool filesys_create_dir(const char *name)
{
	bool success = false;

	char file_name[NAME_MAX + 1];
	struct dir *dir = parse_path(name, file_name);
	if (dir == NULL)
		return false;

	cluster_t inode_cluster = fat_create_chain(0);
	disk_sector_t inode_sector = cluster_to_sector(inode_cluster);

	struct inode *sub_dir_inode;
	struct dir *sub_dir = NULL;

	bool succ_create = dir_create(inode_sector, 16);
	if (dir_add(dir, file_name, inode_sector))
	{
		bool succ_lookup = dir_lookup(dir, file_name, &sub_dir_inode);
		bool succ_create_curr_dir = dir_add(sub_dir = dir_open(sub_dir_inode), ".", inode_sector);
		bool succ_create_prev_dir = dir_add(sub_dir, "..", inode_get_inumber(dir_get_inode(dir)));
		success = (succ_lookup && succ_create_curr_dir && succ_create_prev_dir);
	}

	if (!success && inode_cluster != 0)
		fat_remove_chain(inode_cluster, 0);

	dir_close(sub_dir);
	dir_close(dir);

	return success;
}

int filesys_create_link(const char *target, const char *linkpath)
{
	struct thread *curr = thread_current();

	/* Root Directory open */
	char file_name[NAME_MAX + 1];
	struct dir *dir = parse_path(linkpath, file_name);
	if (dir == NULL)
		return -1;

	/* struct disk_inode를 저장할 새로운 cluster 할당 */
	cluster_t inode_cluster = fat_create_chain(0);
	disk_sector_t inode_sector = cluster_to_sector(inode_cluster);

	bool succ_create = inode_create_link(inode_sector, target);
	bool succ_dir_add = dir_add(dir, file_name, inode_sector);

	/* 할당 받은 cluster에 inode를 만들고 directory에 file 추가 */
	bool success = (succ_create && succ_dir_add);
	if (!success && inode_cluster != 0)
		fat_remove_chain(inode_cluster, 0);

	dir_close(dir);

	return success - 1;
}