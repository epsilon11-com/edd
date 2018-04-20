/*
Copyright (c) 2018, Eric Adolfson
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#define _FILE_OFFSET_BITS 64

#include "overlay.h"
#include "dd.h"
#include "reader.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>

#define ERR(...) \
	if (dd->error == 0) { \
		memset(dd->error_msg, 0, 4096); \
		dd->error = 1; \
	} \
	dd->error = 1; \
	snprintf(dd->error_msg + strlen(dd->error_msg), 4096 - strlen(dd->error_msg), __VA_ARGS__);

void _allocate_filenames(overlay_ctx* overlay, const char* base_filename)
{
	overlay->overlay_filename = (char*)malloc(strlen(base_filename) + 5);
	overlay->index_filename = (char*)malloc(strlen(base_filename) + 5);
	overlay->index_tmp_filename = (char*)malloc(strlen(base_filename) + 5);

	strcpy(overlay->overlay_filename, base_filename);
	strcat(overlay->overlay_filename, ".dat");

	strcpy(overlay->index_filename, base_filename);
	strcat(overlay->index_filename, ".idx");

	strcpy(overlay->index_tmp_filename, base_filename);
	strcat(overlay->index_tmp_filename, ".~dx");
}

void _cleanup_overlay(overlay_ctx* overlay)
{
	if (overlay->overlay_filename != NULL) {
		free(overlay->overlay_filename);
		free(overlay->index_filename);
		free(overlay->index_tmp_filename);

		overlay->overlay_filename = NULL;
	}

	if (overlay->overlay_file != NULL) {
		fclose(overlay->overlay_file);
	}
}

int open_overlay(dd_ctx* dd, const char* base_filename)
{
	overlay_ctx* overlay = &(dd->overlay);

	// Build filenames from base_filename.

	_allocate_filenames(overlay, base_filename);

	// Test if index file backup exists.  Exit here if so, so the user
	// can decide whether to keep it or remove it.

	if (access(overlay->index_tmp_filename, F_OK) == 0) {
		ERR("Overlay index backup file %s found; rename to overlay index file %s or remove to continue\n", overlay->index_filename, overlay->index_filename);

		_cleanup_overlay(overlay);

		return 1;
	}

	// Initialize hash table of overlay index entries.

	overlay->index = NULL;

	// Open or create overlay index file.

	FILE *index_file = fopen(overlay->index_filename, "rb+");

	if (index_file == NULL) {
		index_file = fopen(overlay->index_filename, "wb+");

		if (index_file == NULL) {
			ERR("Unable to open overlay index %s; %s\n", overlay->index_filename, strerror(errno));

			_cleanup_overlay(overlay);

			return 1;
		}
	}

	// Open or create overlay file.

	overlay->overlay_file = fopen(overlay->overlay_filename, "rb+");

	if (overlay->overlay_file == NULL) {
		overlay->overlay_file = fopen(overlay->overlay_filename, "wb+");

		if (overlay->overlay_file == NULL) {
			ERR("Unable to open overlay %s; %s\n", overlay->overlay_filename, strerror(errno));

			fclose(index_file);

			_cleanup_overlay(overlay);

			return 2;
		}
	}

	// [TODO] Verify index file is multiple of 16 bytes (record size), and
	// overlay file is multiple of cluster size.

	// Read index file to hash.

	char record_bytes[16];

	cluster_index_st *cluster_index;

	while (fread(record_bytes, 16, 1, index_file) == 1) {
		cluster_index = (cluster_index_st*)malloc(sizeof(cluster_index_st));

		memcpy(&cluster_index->id, record_bytes, 8);
		memcpy(&cluster_index->file_pos, record_bytes + 8, 8);

		HASH_ADD(hh, overlay->index, id, sizeof(__uint64_t), cluster_index);
	}

	// Close index file.

	fclose(index_file);

	return 0;
}


int sort_index_by_id(cluster_index_st *a, cluster_index_st *b)
{
	return (a->id - b->id);
}


int save_index(dd_ctx* dd)
{
	overlay_ctx* overlay = &(dd->overlay);

	struct stat statbuf;

	// Sort index hash.

	HASH_SORT(overlay->index, sort_index_by_id);

	// Plan to backup the index file if it exists and contains data.

	int backup = 0;

	if (stat(overlay->index_filename, &statbuf) == -1) {

		// Exit here if unable to access the file information, unless that's
		// because the file doesn't exist.

		if (errno != ENOENT) {
			ERR("Unable to stat() %s; %s\n", overlay->index_filename, strerror(errno));

			return 1;
		}
	} else {
		if (statbuf.st_size > 0) {
			backup = 1;
		}
	}


	// Rename current index file to backup if necessary.

	if (backup) {
		if (rename(overlay->index_filename, overlay->index_tmp_filename)) {
			ERR("Unable to rename overlay index %s to %s: %s\n", overlay->index_filename, overlay->index_tmp_filename, strerror(errno));

			return 2;
		}
	}


	// Save hash to index file.

	int failed_save_index = 0;

	FILE *index_file = fopen(overlay->index_filename, "wb+");

	if (index_file == NULL) {
		ERR("Unable to open overlay index %s; %s\n", overlay->index_filename, strerror(errno));

		failed_save_index = 1;
	}

	cluster_index_st *current_cluster_index;
	cluster_index_st *cluster_index_tmp;

	char record_bytes[16];

	HASH_ITER(hh, overlay->index, current_cluster_index, cluster_index_tmp) {
		memcpy(record_bytes, &current_cluster_index->id, 8);
		memcpy(record_bytes + 8, &current_cluster_index->file_pos, 8);

		if (fwrite(record_bytes, 16, 1, index_file) != 1) {
			ERR("Write to overlay index %s failed: %s\n", overlay->index_filename, strerror(errno));

			fclose(index_file);

			index_file = NULL;

			unlink(overlay->index_filename);

			failed_save_index = 1;
		}
	}

	if (index_file != NULL) {
		fclose(index_file);
	}


	// If fail, restore backup to index file.

	if (failed_save_index) {
		unlink(overlay->index_filename);

		if (rename(overlay->index_tmp_filename, overlay->index_filename)) {
			ERR("Unable to rename overlay index backup %s to %s: %s\n", overlay->index_tmp_filename, overlay->index_filename, strerror(errno));

			return 3;
		}
	}


	// Otherwise, remove backup.

	unlink(overlay->index_tmp_filename);

	return 0;
}

void close_overlay(dd_ctx* dd)
{
	_cleanup_overlay(&(dd->overlay));
}

// [TODO]
// Cleanup.  Assign the device name to dd and remove from this call.  Make the call to initialize
// reader deliberate so the program only throws an error regarding the device if the device needs
// to be accessed as part of the program's operation.

int recover_to_overlay(dd_ctx* dd, const char* device, __uint64_t start_cluster_pos, int num_clusters)
{
	overlay_ctx* overlay = &(dd->overlay);

	if (dd->reader.dev == NULL) {
		if (init_reader(&(dd->reader), device, -1, -1)) {
			ERR("%s", dd->reader.error_msg);

			return 3;
		}

//		uint32_t max_lba;
//		uint32_t block_size;
//
//		if (get_capacity(&(dd->reader), &max_lba, &block_size)) {
//			printf("%s", dd->reader.error_msg);
//
//			return 1;
//		}
//
//		printf("Max LBA: %d, block size: %d\n", max_lba, block_size);
	}

	__uint64_t cluster_pos;

	for (cluster_pos = start_cluster_pos; cluster_pos < start_cluster_pos + num_clusters; cluster_pos++) {

		// [TODO] Retrieve cluster from device.
		// [TODO] Write this better!!

		unsigned char* cluster = (unsigned char*)malloc(NTFS_CLUSTER_SIZE);

//		printf("read block %lu\n", (NTFS.partition_offset + NTFS_CLUSTER_SIZE * cluster_pos) / 512);

//		printf("%lu\n", NTFS.partition_offset / 512);
//		exit(0);

//		printf("read: %d\n", read_blocks(&dd->reader, (NTFS_CLUSTER_SIZE * cluster_pos) / 512, 8));
//		printf("read: %d\n", read_blocks(&dd->reader, (NTFS.partition_offset + (NTFS_CLUSTER_SIZE * cluster_pos)) / 512, 8));
//		printf("read: %d\n", read_blocks(&dd->reader, 1, 8));


//		if (lseek64(dd->reader.fd, NTFS.partition_offset + (NTFS_CLUSTER_SIZE * cluster_pos), SEEK_SET) == -1) {
//		if (lseek64(dd->reader.fd, NTFS.partition_offset, SEEK_SET) == -1) {
		FILE* fil = fopen(dd->reader.dev, "rb");

		printf("%lu\n", sizeof(off_t));

		if (fseeko(fil, NTFS.partition_offset + (NTFS_CLUSTER_SIZE * cluster_pos), SEEK_SET)) {
			ERR("Seek failed on %s: %s\n", dd->reader.dev, strerror(errno));

			fclose(fil);
			return 5;
		}

		if (fread(cluster, NTFS_CLUSTER_SIZE, 1, fil) != 1) {
			ERR("Read failed on %s: %s\n", dd->reader.dev, strerror(errno));

			fclose(fil);
			return 5;
		}

		fclose(fil);

//		if (dd->reader.senseinfo.sense_key != 0) {
//			printf("%s: Sense %02x, %02x, %02x; exiting\n",
//							 dd->reader.error_msg, dd->reader.senseinfo.sense_key, dd->reader.senseinfo.asc, dd->reader.senseinfo.ascq);
//		}
//
//		memcpy(cluster, dd->reader.buf, 4096);

		// Look up cluster_pos in index.

		cluster_index_st *cluster_index;

		HASH_FIND(hh, overlay->index, &cluster_pos, sizeof(__uint64_t), cluster_index);

		if (cluster_index != NULL) {

			// If the cluster already exists in the overlay, overwrite it.

			if (fseek(overlay->overlay_file, cluster_index->file_pos, SEEK_SET)) {
				ERR("fseek to %lu failed on %s: %s\n", cluster_index->file_pos, overlay->overlay_filename, strerror(errno));

				return 1;
			}

			if (fwrite(cluster, NTFS_CLUSTER_SIZE, 1, overlay->overlay_file) != 1) {
				ERR("Write to overlay %s failed: %s\n", overlay->overlay_filename, strerror(errno));

				return 2;
			}
		} else {

			// Prepare to add the cluster to the end of the overlay.

			if (fseek(overlay->overlay_file, 0, SEEK_END)) {
				ERR("fseek to EOF failed on %s: %s\n", overlay->overlay_filename, strerror(errno));

				return 1;
			}

			__uint64_t file_pos = ftell(overlay->overlay_file);

			if (fwrite(cluster, NTFS_CLUSTER_SIZE, 1, overlay->overlay_file) != 1) {
				ERR("Write to overlay %s failed: %s\n", overlay->overlay_filename, strerror(errno));

				return 2;
			}

			cluster_index = (cluster_index_st*)malloc(sizeof(cluster_index_st));

			cluster_index->id = cluster_pos;
			cluster_index->file_pos = file_pos;

			HASH_ADD(hh, overlay->index, id, sizeof(__uint64_t), cluster_index);
		}
	}

	return 0;
}

int overlay_has_cluster(dd_ctx* dd, __uint64_t cluster_pos)
{
	overlay_ctx* overlay = &(dd->overlay);

	cluster_index_st *cluster_index;

	HASH_FIND(hh, overlay->index, &cluster_pos, sizeof(__uint64_t), cluster_index);

	if (cluster_index == NULL) {
		return 0;
	}

	return 1;
}

int read_cluster_from_overlay(dd_ctx* dd, unsigned char* cluster, __uint64_t cluster_pos)
{
	overlay_ctx* overlay = &(dd->overlay);

	cluster_index_st *cluster_index;

	HASH_FIND(hh, overlay->index, &cluster_pos, sizeof(__uint64_t), cluster_index);

	if (cluster_index == NULL) {
		return -1;
	}

	if (fseek(overlay->overlay_file, cluster_index->file_pos, SEEK_SET)) {
		ERR("fseek to %lu failed on %s: %s\n", cluster_index->file_pos, overlay->overlay_filename, strerror(errno));

		return 1;
	}

	if (fread(cluster, NTFS_CLUSTER_SIZE, 1, overlay->overlay_file) != 1) {
		ERR("Read from overlay %s failed: %s\n", overlay->overlay_filename, strerror(errno));

		return 2;
	}

	return 0;
}

