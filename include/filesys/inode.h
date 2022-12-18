#ifndef FILESYS_INODE_H
#define FILESYS_INODE_H

#include <stdbool.h>
#include "filesys/off_t.h"
#include "devices/disk.h"

#define INODE_FILE 0
#define INODE_DIR 1

struct bitmap;

void inode_init(void);
bool inode_create(disk_sector_t, off_t, uint32_t is_dir);
bool inode_create_link(disk_sector_t sector, char *path_name);

struct inode *inode_open(disk_sector_t);
struct inode *inode_reopen(struct inode *);
disk_sector_t inode_get_inumber(const struct inode *);
void inode_close(struct inode *);
void inode_remove(struct inode *);
off_t inode_read_at(struct inode *, void *, off_t size, off_t offset);
off_t inode_write_at(struct inode *, const void *, off_t size, off_t offset);
void inode_deny_write(struct inode *);
void inode_allow_write(struct inode *);
off_t inode_length(const struct inode *);
uint32_t inode_is_dir(const struct inode *inode);
bool inode_is_removed(const struct inode *inode);
bool inode_is_link(const struct inode *inode);
char *inode_get_link_name(const struct inode *inode);

#endif /* filesys/inode.h */
