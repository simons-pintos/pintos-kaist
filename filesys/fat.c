#include "filesys/fat.h"
#include "devices/disk.h"
#include "filesys/filesys.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include <stdio.h>
#include <string.h>

/* Should be less than DISK_SECTOR_SIZE */
struct fat_boot
{
	unsigned int magic;				  // overflow 감지
	unsigned int sectors_per_cluster; // cluster 1개가 차지하는 sector 수 -> 1로 고정
	unsigned int total_sectors;		  // disk의 모든 sector 수
	unsigned int fat_start;			  // fat이 시작하는 sector number
	unsigned int fat_sectors;		  // fat이 차지하는 sector 수
	unsigned int root_dir_cluster;	  // root directory의 cluster number
};

/* FAT FS */
struct fat_fs
{
	struct fat_boot bs;		  // filesystem 정보
	unsigned int *fat;		  // fat
	unsigned int fat_length;  // fat의 길이 -> entry 개수
	disk_sector_t data_start; // data block이 시작되는 sector number
	cluster_t last_clst;	  // 마지막 clust?
	struct lock write_lock;	  // 쓰기 lock
};

static struct fat_fs *fat_fs;

void fat_boot_create(void);
void fat_fs_init(void);

void fat_init(void)
{
	fat_fs = calloc(1, sizeof(struct fat_fs));
	if (fat_fs == NULL)
		PANIC("FAT init failed");

	// Read boot sector from the disk
	unsigned int *bounce = malloc(DISK_SECTOR_SIZE);
	if (bounce == NULL)
		PANIC("FAT init failed");
	disk_read(filesys_disk, FAT_BOOT_SECTOR, bounce);
	memcpy(&fat_fs->bs, bounce, sizeof(fat_fs->bs));
	free(bounce);

	// Extract FAT info
	if (fat_fs->bs.magic != FAT_MAGIC)
		fat_boot_create();
	fat_fs_init();
}

void fat_open(void)
{
	fat_fs->fat = calloc(fat_fs->fat_length, sizeof(cluster_t));
	if (fat_fs->fat == NULL)
		PANIC("FAT load failed");

	// Load FAT directly from the disk
	uint8_t *buffer = (uint8_t *)fat_fs->fat;
	off_t bytes_read = 0;
	off_t bytes_left = sizeof(fat_fs->fat);
	const off_t fat_size_in_bytes = fat_fs->fat_length * sizeof(cluster_t);
	for (unsigned i = 0; i < fat_fs->bs.fat_sectors; i++)
	{
		bytes_left = fat_size_in_bytes - bytes_read;
		if (bytes_left >= DISK_SECTOR_SIZE)
		{
			disk_read(filesys_disk, fat_fs->bs.fat_start + i,
					  buffer + bytes_read);
			bytes_read += DISK_SECTOR_SIZE;
		}
		else
		{
			uint8_t *bounce = malloc(DISK_SECTOR_SIZE);
			if (bounce == NULL)
				PANIC("FAT load failed");
			disk_read(filesys_disk, fat_fs->bs.fat_start + i, bounce);
			memcpy(buffer + bytes_read, bounce, bytes_left);
			bytes_read += bytes_left;
			free(bounce);
		}
	}
}

void fat_close(void)
{
	// Write FAT boot sector
	uint8_t *bounce = calloc(1, DISK_SECTOR_SIZE);
	if (bounce == NULL)
		PANIC("FAT close failed");
	memcpy(bounce, &fat_fs->bs, sizeof(fat_fs->bs));
	disk_write(filesys_disk, FAT_BOOT_SECTOR, bounce);
	free(bounce);

	// Write FAT directly to the disk
	uint8_t *buffer = (uint8_t *)fat_fs->fat;
	off_t bytes_wrote = 0;
	off_t bytes_left = sizeof(fat_fs->fat);
	const off_t fat_size_in_bytes = fat_fs->fat_length * sizeof(cluster_t);
	for (unsigned i = 0; i < fat_fs->bs.fat_sectors; i++)
	{
		bytes_left = fat_size_in_bytes - bytes_wrote;
		if (bytes_left >= DISK_SECTOR_SIZE)
		{
			disk_write(filesys_disk, fat_fs->bs.fat_start + i,
					   buffer + bytes_wrote);
			bytes_wrote += DISK_SECTOR_SIZE;
		}
		else
		{
			bounce = calloc(1, DISK_SECTOR_SIZE);
			if (bounce == NULL)
				PANIC("FAT close failed");
			memcpy(bounce, buffer + bytes_wrote, bytes_left);
			disk_write(filesys_disk, fat_fs->bs.fat_start + i, bounce);
			bytes_wrote += bytes_left;
			free(bounce);
		}
	}
}

void fat_create(void)
{
	// Create FAT boot
	fat_boot_create();
	fat_fs_init();

	// Create FAT table
	fat_fs->fat = calloc(fat_fs->fat_length, sizeof(cluster_t));
	if (fat_fs->fat == NULL)
		PANIC("FAT creation failed");

	// Set up ROOT_DIR_CLST
	fat_put(ROOT_DIR_CLUSTER, EOChain);

	// Fill up ROOT_DIR_CLUSTER region with 0
	uint8_t *buf = calloc(1, DISK_SECTOR_SIZE);
	if (buf == NULL)
		PANIC("FAT create failed due to OOM");
	disk_write(filesys_disk, cluster_to_sector(ROOT_DIR_CLUSTER), buf);
	free(buf);
}

void fat_boot_create(void)
{
	unsigned int fat_sectors =
		(disk_size(filesys_disk) - 1) / (DISK_SECTOR_SIZE / sizeof(cluster_t) * SECTORS_PER_CLUSTER + 1) + 1;
	fat_fs->bs = (struct fat_boot){
		.magic = FAT_MAGIC,
		.sectors_per_cluster = SECTORS_PER_CLUSTER,
		.total_sectors = disk_size(filesys_disk),
		.fat_start = 1,
		.fat_sectors = fat_sectors,
		.root_dir_cluster = ROOT_DIR_CLUSTER,
	};
}

void fat_fs_init(void)
{
	fat_fs->fat_length = disk_size(filesys_disk) - 1 - fat_fs->bs.fat_sectors;
	fat_fs->data_start = fat_fs->bs.fat_start + fat_fs->bs.fat_sectors;
}

/*----------------------------------------------------------------------------*/
/* FAT handling                                                               */
/*----------------------------------------------------------------------------*/

/* Add a cluster to the chain.
 * If CLST is 0, start a new chain.
 * Returns 0 if fails to allocate a new cluster. */
cluster_t
fat_create_chain(cluster_t clst)
{
	/* FAT에서 empty cluster 탐색 */
	int i;
	for (i = 2; i < fat_fs->fat_length && fat_get(i) > 0; i++)
		;

	/* empty cluster가 없으면 */
	if (i >= fat_fs->fat_length)
		return 0;

	/* empty cluster에 새로운 cluster 생성 */
	fat_put(i, EOChain);

	/* 새로운 cluster chain일 때 */
	if (clst == 0)
		return i;

	/* 기존 cluster chain의 마지막에 추가할 때 */
	cluster_t temp_c;
	for (temp_c = clst; fat_get(temp_c) != EOChain; temp_c = fat_get(temp_c))
		;
	fat_put(temp_c, i);

	return i;
}

/* Remove the chain of clusters starting from CLST.
 * If PCLST is 0, assume CLST as the start of the chain. */
void fat_remove_chain(cluster_t clst, cluster_t pclst)
{
	/* pcluster가 입력됬으면 pcluster를 chain으로 끝으로 만듬 */
	if (pclst)
		fat_put(pclst, EOChain);

	/* clst부터 순회하면서 FAT에서 할당 해제 */
	cluster_t temp_c = clst;
	cluster_t next_c;
	for (; fat_get(temp_c) != EOChain; temp_c = next_c)
	{
		next_c = fat_get(temp_c);
		fat_put(temp_c, 0);
	}

	fat_put(temp_c, 0);
}

/* Update a value in the FAT table. */
void fat_put(cluster_t clst, cluster_t val)
{
	if (cluster_to_sector(clst - 1) >= disk_size(filesys_disk))
		return;

	fat_fs->fat[clst - 1] = val;
}

/* Fetch a value in the FAT table. */
cluster_t
fat_get(cluster_t clst)
{
	return fat_fs->fat[clst - 1];
}

/* Covert a cluster # to a sector number. */
disk_sector_t
cluster_to_sector(cluster_t clst)
{
	return clst + fat_fs->data_start;
}

cluster_t
sector_to_cluster(disk_sector_t sector)
{
	cluster_t clst = sector - fat_fs->data_start;

	if (clst < 2)
		return 0;

	return clst;
}

void print_fat(int start, int end)
{
	printf("\n=========================FAT====================================================================================\n");
	for (int i = start; i < end; i++)
	{
		if (fat_fs->fat[i] == EOChain)
			printf(" [%3d|EOC] ", i + 1);
		else
			printf(" [%3d|%3d] ", i + 1, fat_fs->fat[i]);
		if (i % 5 == 4)
			printf("\n");
	}
	printf("\n================================================================================================================\n");
}