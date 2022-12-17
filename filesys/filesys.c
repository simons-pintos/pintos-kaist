#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/fat.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "devices/disk.h"
#include "threads/thread.h"

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
	thread_current()->cwd = dir_open_root(); // 현재 thread의 cwd를 root로 설정
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
	/* struct disk_inode를 저장할 새로운 cluster 할당 */
	cluster_t inode_cluster = fat_create_chain(0);
	disk_sector_t inode_sector = cluster_to_sector(inode_cluster);
	char file_name[128];

	/* Root Directory open */
	struct dir *dir_path = parse_path (name, file_name);
	if (dir_path == NULL)
		return false;

	struct dir *dir = dir_reopen(dir_path);
	// struct dir *dir = dir_open_root();

	/* 할당 받은 cluster에 inode를 만들고 directory에 file 추가 */
	bool success = (dir != NULL && inode_create(inode_sector, initial_size, 0) && dir_add(dir, file_name, inode_sector));
	if (!success && inode_cluster != 0)
		fat_remove_chain(inode_cluster, 0);

	dir_close(dir);

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
	char file_name[128];
	struct dir *dir_path = parse_path (name, file_name);
	if (dir_path == NULL){
		return NULL;
		}
	struct dir *dir = dir_reopen(dir_path);
	// struct dir *dir = dir_open_root();

	struct inode *inode = NULL;
	if (dir != NULL){
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
	char file_name[128];
	struct dir *dir_path = parse_path (name, file_name);
	if (dir_path == NULL)
		return false;
	

	struct dir *dir = dir_reopen(dir_path);
	bool success = dir != NULL && dir_remove(dir, file_name);
	dir_close(dir);
	//dir_close(dir_path); 를 해주어야 할듯?(추후고민)

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
	
	/* Root Directory에 ., .. 추가 */
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

struct dir* parse_path (char *path_name, char *file_name){
	struct dir *dir = dir_open_root();
	char *token, *next_token, *save_ptr;
	char *path = malloc(strlen(path_name) + 1);
	strlcpy(path, path_name, strlen(path_name) + 1);

	// if (strstr(path, "/") == NULL){
	// 	strlcpy(file_name, path, strlen(path) + 1);
	// 	dir_close(dir);
	// 	dir = dir_reopen(thread_current()->cwd);

	// 	return dir;
	// }

	if(path[0] == '/'){
		//원래는 close후 다시 열어줌 (이유가 불분명해서 삭제)
	}
	else{
		dir_close(dir);
		dir = dir_reopen(thread_current()->cwd);
	}
	// if(path[1] == NULL){
	// 	dir_close(dir);
	// 	strlcpy(file_name, ".", 2);
	// 	return dir;
	// }

	token = strtok_r(path, "/", &save_ptr);
	next_token = strtok_r(NULL, "/", &save_ptr);

	
	while(next_token != NULL){
		struct inode *inode = NULL;
		if(!dir_lookup(dir, token, &inode)){
			dir_close(dir);
			return NULL;
		}

		if (inode_is_dir(inode)){
			dir_close(dir);
			dir = dir_open(inode);
		}
		else{
			dir_close(dir);
			return NULL;
		}

		token = next_token;
		next_token = strtok_r(NULL, "/", &save_ptr);
		/*walking done!*/
	}
	
	if (token == NULL){ //예외처리
		dir_close(dir);
		return NULL;
	}
	else{
		struct inode *inode = NULL;
		
		dir_lookup(dir, token, &inode);
		
		if (inode == NULL){
			strlcpy(file_name, token, strlen(token) + 1);
			return dir;
		}

		if (inode_is_dir(inode)){ //마지막이 디렉토리인 경우
			dir_close(dir);
			dir = dir_open(inode);
		}
		else{//마지막이 파일인 경우
			strlcpy(file_name, token, strlen(token) + 1);
			// dir_close(dir);
			// return NULL;
		}
	}
	free(path);
	return dir;
}

bool filesys_create_dir(const char *name){
	
	cluster_t inode_cluster = fat_create_chain(0);
	disk_sector_t inode_sector = cluster_to_sector(inode_cluster);
	char file_name[128];
	
	struct dir *dir_path = parse_path (name, file_name);
	if (dir_path == NULL)
		return false;

	struct dir *dir = dir_reopen(dir_path);

	/* 할당 받은 cluster에 inode를 만들고 directory에 file 추가 */
	
	bool success = (dir != NULL && inode_create(inode_sector, 0, 1) && dir_add(dir, file_name, inode_sector));
	if (!success && inode_cluster != 0)
		fat_remove_chain(inode_cluster, 0);

	/* directory에 .과 .. 추가 */
	if (success){
		struct inode *inode = NULL;
		dir_lookup(dir, file_name, &inode);
		struct dir *new_dir = dir_open(inode);
		dir_add(new_dir, ".", inode_sector);
		dir_add(new_dir, "..", inode_get_inumber(dir_get_inode(dir)));
		dir_close(new_dir);
	}

	dir_close(dir);
	return success;
}

