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

#include "edd.h"
#include "dd.h"

#include "badclusters.h"

#include "hexdump.h"

#include "overlay.h"

#include "elog.h"

#include <unistd.h>

#include <errno.h>

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <fcntl.h>

#include <ctype.h>

#include <memory.h>

#include <math.h>

#include <wchar.h>

#include <time.h>

#include <stdint.h>

#include <utime.h>

//#define TEMP_PARTITION_START 0x346500000

#define ERR(...) \
	if (dd->error == 0) { \
		memset(dd->error_msg, 0, 4096); \
		dd->error = 1; \
	} \
	dd->error = 1; \
	snprintf(dd->error_msg + strlen(dd->error_msg), 4096 - strlen(dd->error_msg), __VA_ARGS__);

int init_dd(dd_ctx* dd)
{
	memset(dd, 0, sizeof(dd_ctx));

	return 0;
}

int cleanup_dd(dd_ctx* dd)
{
	while (dd->safe_regions != NULL) {
		dd->safe_region_pos = dd->safe_regions;
		dd->safe_regions = dd->safe_regions->next;

		free(dd->safe_region_pos);
	}

	while (dd->failed_clusters != NULL) {
		dd->failed_cluster_pos = dd->failed_clusters;
		dd->failed_clusters = dd->failed_clusters->next;

		free(dd->failed_cluster_pos);
	}

	cleanup_bad_cluster_hashes(dd);

	if (dd->reader.dev != NULL) {
		cleanup_reader(&dd->reader);
	}
	return 0;
}

/**
 * Callback function for read_mft_record() that looks for MFT entry "$MFT"
 * and reads its data run into NTFS.mft_data_run in preparation for working
 * with the MFT.
 *
 * @param dd DD context struct
 * @param rh Record handler context struct (rh->param is not used.)
 */
static void mft_record_handler_read_mft(dd_ctx *dd, record_handler_ctx *rh)
{
	if (strcmp(rh->name, "$MFT") == 0) {
		// Extract MFT details.

		// [TODO] If extraction fails, or the bitmap wasn't able to be read,
		//        bail here.  Later figure out what type of "repair" options
		//        might be useful.

//		printf("Reading mft_data_run\n");

		memcpy(&NTFS.mft_data_run, &rh->data_run, sizeof(rh->data_run));

		NTFS.mft_data_run.entry = (data_run_entry*)malloc(sizeof(data_run_entry) * rh->data_run.entry_count);

		memcpy(NTFS.mft_data_run.entry, rh->data_run.entry, sizeof(data_run_entry) * rh->data_run.entry_count);

		memcpy(&(NTFS.mft_bitmap), &(rh->bitmap), sizeof(bitmap_st));

		NTFS.mft_bitmap.data = (char*)malloc(NTFS.mft_bitmap.length);

		memcpy(NTFS.mft_bitmap.data, rh->bitmap.data, NTFS.mft_bitmap.length);
	}
}

int open_ntfs(dd_ctx* dd, const char *filename, __uint64_t partition_offset) {

	NTFS.partition_offset = partition_offset;

	NTFS.disc = fopen(filename, "rb");

	if (NTFS.disc == NULL) {
		ERR("Unable to open %s; %s\n", filename, strerror(errno));

		return 1;
	}

	// Get file size.

	fseek(NTFS.disc, 0, SEEK_END);
	dd->disc_size = ftell(NTFS.disc);

	// Read volume header.

	char header[512];

	fseek(NTFS.disc, partition_offset, SEEK_SET);
	fread(header, 512, 1, NTFS.disc);

	// Verify header is NTFS.

	if (memcmp(header + 3, "NTFS", 4)) {
		ERR("Partition header does not appear to be NTFS.\n");

		fclose(NTFS.disc);

		return 2;
	}

	// Pull $MFT offset.

	memcpy(&NTFS_HEADER.bytes_per_sector, header + 11, 2);
	NTFS_HEADER.sectors_per_cluster = header[13];
	memcpy(&NTFS_HEADER.mft_cluster, header + 48, 4);
	memcpy(&NTFS_HEADER.mftmirror_cluster, header + 56, 8);
	NTFS_HEADER.mft_size = header[64];

	if ((unsigned char)header[64] < 128) {
		NTFS_HEADER.mft_size = header[64] * NTFS_CLUSTER_SIZE;
	} else {
		// [TODO] Bail if MFT value too large to hold.  Don't know that
		// this happens in practice yet; usually it's 1024 bytes.

		NTFS_HEADER.mft_size = (__uint64_t)powl(2, 256 - (unsigned char)header[64]);
	}

	// [TODO] Sanity check: cluster size should be evenly divisible by mft_size.

//	NTFS_CLUSTER = (char*)malloc(NTFS_CLUSTER_SIZE);


	memset(&(NTFS.mft_bitmap), 0, sizeof(bitmap_st));

	memset(&(NTFS.mft_data_run), 0, sizeof(data_run_st));

	read_mft_record(dd, NTFS_HEADER.mftmirror_cluster, &mft_record_handler_read_mft, NULL);

	if (NTFS.mft_data_run.entry_count == 0) {
		ERR("Unable to read $MFT entry\n");

		return 1;
	}




	return 0;
}

void close_ntfs(dd_ctx* dd)
{
	if (NTFS.disc != NULL) {
		fclose(NTFS.disc);

//		free(NTFS_CLUSTER);

		NTFS.disc = NULL;

		free(NTFS.mft_data_run.entry);
		memset(&NTFS.mft_data_run, 0, sizeof(data_run_st));
	}

	if (NTFS.mft_bitmap.length != 0) {
		free(NTFS.mft_bitmap.data);
	}
}

#define CLUSTER_TO_BYTE(c) \
	(__uint64_t)(NTFS.partition_offset + NTFS_CLUSTER_SIZE * (c))

/**
 * Test whether a given cluster position exists within the disc image
 * and is marked as properly read in the mapfile.
 *
 * @param dd DD context structure
 * @param cluster_pos Cluster position to test
 *
 * @return 1 if cluster exists and is "valid", 0 otherwise
 */
int _cluster_is_safe(dd_ctx *dd, __uint64_t cluster_pos) {
	if (dd->overlay.overlay_file != NULL) {
		if (overlay_has_cluster(dd, cluster_pos)) {
			return 1;
		}
	}

	dd->safe_region_pos = dd->safe_regions;

	while (dd->safe_region_pos != NULL) {
		if (dd->safe_region_pos->start <= CLUSTER_TO_BYTE(cluster_pos) &&
				dd->safe_region_pos->start + dd->safe_region_pos->length >= CLUSTER_TO_BYTE(cluster_pos + 1)) {

			return 1;
		}
		dd->safe_region_pos = dd->safe_region_pos->next;
	}

	return 0;
}

int read_cluster(dd_ctx *dd, unsigned char* cluster, __uint64_t cluster_pos)
{
	// Attempt to read cluster from overlay first.

	if (dd->overlay.overlay_file != NULL) {
		int result = read_cluster_from_overlay(dd, cluster, cluster_pos);

		if (result == 0) {
			// Return cluster if found in overlay.

			return 0;
		}

		if (result > 1) {
			// Pass through read error from read_cluster_from_overlay()

			// [TODO] It's worse if this occurs than the other errors from this
			// routine -- handle differently?

			return 2;
		}

		// Cluster not found in overlay; continue.
	}

	// Check that cluster exists within image filesize.

	if (NTFS.partition_offset + NTFS_CLUSTER_SIZE * (cluster_pos + 1) > dd->disc_size) {
		return 1;
	}

	// Perform read.

	fseek(NTFS.disc, NTFS.partition_offset + NTFS_CLUSTER_SIZE * cluster_pos, SEEK_SET);

	fread(cluster, NTFS_CLUSTER_SIZE, 1, NTFS.disc);

	// Check that cluster is in a "read" area of the dump.
	// (This is done after the read deliberately for now, but should be moved before the read after testing.)

	if (!_cluster_is_safe(dd, cluster_pos)) {
		return 1;
	}

	return 0;
}

// [TODO] Detect when MFT index out of bounds, return UINT64_MAX

__uint64_t get_mft_index(dd_ctx *dd, __uint64_t cluster, __uint8_t mft_rec) {
	__uint64_t cluster_count = 0;

	for (int i = 0; i < NTFS.mft_data_run.entry_count; i++) {
		if (cluster >= NTFS.mft_data_run.entry[i].cluster && cluster <= NTFS.mft_data_run.entry[i].cluster + NTFS.mft_data_run.entry[i].count) {
			cluster_count += cluster - NTFS.mft_data_run.entry[i].cluster;
			break;
		} else {
			cluster_count += NTFS.mft_data_run.entry[i].count;
		}
	}

	return cluster_count * (NTFS_CLUSTER_SIZE / NTFS.ntfs_header.mft_size) + mft_rec;
}

// [TODO] Test thoroughly, especially edge cases.

__uint64_t get_mft_cluster(dd_ctx *dd, __uint64_t mft_index) {
	__uint64_t total_count = 0;

	for (int i = 0; i < NTFS.mft_data_run.entry_count; i++) {
		if (NTFS.mft_data_run.entry[i].count * 4 - 1 < mft_index - total_count) {
//			printf("count %u, mft_index: %lu, total_count: %lu\n", mft_data_run.entry[i].count, mft_index, total_count);
			total_count += NTFS.mft_data_run.entry[i].count * 4;
		} else {
			return NTFS.mft_data_run.entry[i].cluster + ((mft_index - total_count) / 4);
		}
	}

	return UINT64_MAX;
}

int bitmap_is_set(dd_ctx *dd, bitmap_st *bitmap, int pos)
{
	if (bitmap->used == 0 || bitmap->valid == 0 || bitmap->length == 0) {
		// [TODO] Trip error?

		return 0;
	}

	int byte_pos = pos / 8;
	int bit_pos = pos % 8;

	if (byte_pos > bitmap->length) {
		// [TODO] Trip error?

		return 0;
	}

	return bitmap->data[byte_pos] & (1 << bit_pos);
}

typedef struct mft_attr_header {
	__uint32_t code;
	__uint32_t size;
	__uint8_t nonresident;
	__uint8_t name_size;
	__uint16_t name_offset;
	__uint16_t data_flags;
	__uint16_t id;

	char* name;

	// Resident attribute data

	__uint32_t r_data_size;
	__uint16_t r_data_offset;
	__uint8_t r_indexed_flag;

	// Nonresident attribute data

	__uint64_t nr_first_vcn;
	__uint64_t nr_last_vcn;
	__uint16_t nr_data_runs_offset;
	__uint16_t nr_compression_size;
	__uint64_t nr_allocated_data_size;
	__uint64_t nr_data_size;
	__uint64_t nr_valid_data_size;

} mft_attr_header;

void unwiden_string(char** output, const char* widestr, int length)
{
	*output = (char*)malloc(length + 1);

	for (int i = 0; i < length; i++) {
		(*output)[i] = widestr[i * 2];
	}

	(*output)[length] = '\0';
}

static __uint16_t _read_mft_attribute_header(dd_ctx *dd, unsigned char* cluster, mft_attr_header *attr_header, int attr_pos)
{
	memcpy(&attr_header->code, cluster + attr_pos, 4);
	memcpy(&attr_header->size, cluster + attr_pos + 4, 4);
	attr_header->name = NULL;

	if (attr_header->code == 0xffffffff) {
		return 0;
	}

	memcpy(&attr_header->nonresident, cluster + attr_pos + 8, 1);
	memcpy(&attr_header->name_size, cluster + attr_pos + 9, 1);
	memcpy(&attr_header->name_offset, cluster + attr_pos + 10, 2);
	memcpy(&attr_header->data_flags, cluster + attr_pos + 12, 2);
	memcpy(&attr_header->id, cluster + attr_pos + 14, 2);

	if (attr_header->name_size == 0) {
		attr_header->name = NULL;
	} else {
		attr_header->name = (char*)malloc(attr_header->name_size * 2 + 2);

		memset(attr_header->name, 0, attr_header->name_size * 2 + 2);
		memcpy(attr_header->name, cluster + attr_pos + attr_header->name_offset, attr_header->name_size * 2);
	}

	int data_pos = attr_pos + 16;

	if (attr_header->nonresident) {
		memcpy(&attr_header->nr_first_vcn, cluster + data_pos, 8);
		memcpy(&attr_header->nr_last_vcn, cluster + data_pos + 8, 8);
		memcpy(&attr_header->nr_data_runs_offset, cluster + data_pos + 16, 2);
		memcpy(&attr_header->nr_compression_size, cluster + data_pos + 18, 2);
		memcpy(&attr_header->nr_allocated_data_size, cluster + data_pos + 24, 8);
		memcpy(&attr_header->nr_data_size, cluster + data_pos + 32, 8);
		memcpy(&attr_header->nr_valid_data_size, cluster + data_pos + 40, 8);

		return attr_pos + attr_header->nr_data_runs_offset;
	}

	memcpy(&attr_header->r_data_size, cluster + data_pos, 4);
	memcpy(&attr_header->r_data_offset, cluster + data_pos + 4, 2);
	memcpy(&attr_header->r_indexed_flag, cluster + data_pos + 6, 1);

	return attr_pos + attr_header->r_data_offset;
}

/*
 * typedef struct ntfs_file_name_st {
	__uint32_t parent_mft_index;
	__uint16_t parent_sequence_num;
	__uint64_t date_created;
	__uint64_t date_modified;
	__uint64_t date_mft_modified;
	__uint64_t date_accessed;
	__uint64_t filesize;
	__uint32_t attributes;
	__uint32_t extended_data;
	__uint8_t name_size;
	__uint8_t name_namespace;
	char* name;
} ntfs_file_name_st;
 *
 */
static int _read_file_name(dd_ctx* dd, unsigned char* cluster, ntfs_file_name_st* file_name, int pos)
{
	memcpy(&file_name->parent_mft_index, cluster + pos, 4);
	memcpy(&file_name->parent_sequence_num, cluster + pos + 6, 2);

	memcpy(&file_name->date_created, cluster + pos + 8, 8);
	memcpy(&file_name->date_modified, cluster + pos + 16, 8);
	memcpy(&file_name->date_mft_modified, cluster + pos + 24, 8);
	memcpy(&file_name->date_accessed, cluster + pos + 32, 8);

	memcpy(&file_name->allocated_filesize, cluster + pos + 40, 8);
	memcpy(&file_name->filesize, cluster + pos + 48, 8);
	memcpy(&file_name->attributes, cluster + pos + 56, 4);
	memcpy(&file_name->extended_data, cluster + pos + 60, 4);

	memcpy(&file_name->name_size, cluster + pos + 64, 1);
	memcpy(&file_name->name_namespace, cluster + pos + 65, 1);

	file_name->name = (char*)malloc(file_name->name_size * 2);
	memcpy(file_name->name, cluster + pos + 66, file_name->name_size * 2);

	return 0;
}

static int _read_mft_data_run(dd_ctx* dd, unsigned char* cluster, data_run_st* data_run, int initial_data_run_pos, int data_run_size)
{
	// 0.0 4 bits (number of cluster blocks value size)
	// 0.4 4 bits (cluster block number value size)
	// 1 (size value size) (number of cluster blocks)
	// ... (cluster block number value size) (data run cluster block number) (last one is 0)

	// Go through data run to determine its size.

	int data_run_count = 0;

	memset(data_run, 0, sizeof(data_run));

	for (int data_run_step = 0; data_run_step < 2; data_run_step++) {
		__uint64_t data_run_cluster = 0;
		int data_run_pos = initial_data_run_pos;

		if (data_run_step == 1) {
			data_run->entry = (data_run_entry*)malloc(sizeof(data_run_entry) * data_run_count);
			data_run->entry_count = data_run_count;
			data_run->size = data_run_size;

			data_run_count = 0;
		}

		while (cluster[data_run_pos] != 0x00) {
			// Get header

			__uint8_t dr_header = cluster[data_run_pos];

			__uint8_t dr_count_len = dr_header & 0x0F;
			__uint8_t dr_offset_len = dr_header >> 4;

			__uint64_t dr_count = 0;

			if (dr_offset_len > 8) {
				// [TODO] Error, program is unable to handle offsets of this length.
				//        (this is really unlikely to occur)
			}

			if (dr_count_len > 8) {
				// [TODO] Error, program is unable to handle counts of this length.
				//        (this is really unlikely to occur)
			}

			memcpy(&dr_count, cluster + data_run_pos + 1, dr_count_len);

			__int64_t offset;

			if (cluster[data_run_pos + 1 + dr_count_len + dr_offset_len - 1] >> 4 >= 8) {
				memset(&offset, 0xff, 8);
			} else {
				memset(&offset, 0, 8);
			}

			memcpy(&offset, cluster + data_run_pos + 1 + dr_count_len, dr_offset_len);

			data_run_cluster += offset;

//								printf("Offset: ");
//								for (int i = 0; i < dr_offset_len; i++) {
//									printf("%02x ", cluster[data_run_pos + 1 + dr_count_len + i]);
//								}
//
//								printf("Count: ");
//								for (int i = 0; i < dr_count_len; i++) {
//									printf("%02x ", cluster[data_run_pos + 1 + i]);
//								}
//
//								printf("\n");
//
//								printf("Len: %hhu, count: %lu, cluster: %ld\n", dr_offset_len, dr_count, data_run_cluster);

			if (data_run_step == 1) {
				data_run->entry[data_run_count].sparse = 0;

				data_run->entry[data_run_count].cluster = data_run_cluster;
				data_run->entry[data_run_count].count = dr_count;
			}

			data_run_pos += 1 + dr_count_len + dr_offset_len;

			data_run_count++;
		}
	}
}

int read_mft_record(dd_ctx *dd, __uint64_t start_cluster, MFTRecordHandler handler, record_handler_ctx *rh)
{
	//printf("Starting to read MFT at %lu", partition_offset + cluster_size * start_cluster);

	unsigned char* cluster;

	cluster = (unsigned char*)malloc(NTFS_CLUSTER_SIZE);

	if (read_cluster(dd, cluster, start_cluster) == 1) {
		elog(LOG_READ_MFT_RECORD, "BAD CLUSTER %lu\n", start_cluster);

		// [TODO] Exit here if unable to read cluster?
//		return 1;
	}

	int mft_count = NTFS_CLUSTER_SIZE / NTFS_HEADER.mft_size;

	int sectors_per_mft = NTFS_HEADER.mft_size / NTFS_HEADER.bytes_per_sector;

	__uint16_t mft_offset = 0;

	for (int mft_rec = 0; mft_rec < mft_count; mft_rec++) {

		elog(LOG_READ_MFT_RECORD, "mft rec %d cluster %lu\n", mft_rec, start_cluster);
		if (LOG_READ_MFT_RECORD) {
//			hexdump(cluster + mft_offset, NTFS_HEADER.mft_size);
		}

		// Find and apply fix-ups.

		__uint16_t fix_up_offset;
		__uint16_t fix_up_count;
		__uint16_t fix_up_value;

		memcpy(&fix_up_offset, cluster + mft_offset + 4, 2);
		memcpy(&fix_up_count, cluster + mft_offset + 6, 2);
		memcpy(&fix_up_value, cluster + mft_offset + fix_up_offset, 2);

		if (fix_up_count < sectors_per_mft) {
			ERR("Failed to find expected number of fix up records at cluster %lu\n", start_cluster);

			return 1;
		}

		for (int mft_sector = 0; mft_sector < sectors_per_mft; mft_sector++) {
			if (memcmp(cluster + mft_offset + (512 * (mft_sector + 1)) - 2, &fix_up_value, 2)) {
				ERR("Cluster %lu contains invalid fix up placeholder (bad sector?)\n", start_cluster);

				return 1;
			}

			memcpy(cluster + mft_offset + (512 * (mft_sector + 1)) - 2,
					cluster + mft_offset + fix_up_offset + 2 + (mft_sector * 2),
					2);
		}

		__uint16_t mft_flags;

		memcpy(&mft_flags, cluster + mft_offset + 22, 2);

		// Fields for the callback.

		char* filename[4] = { NULL, NULL, NULL, NULL };
		__uint64_t si_date_created = 0;
		__uint64_t si_date_modified = 0;
		__uint64_t si_date_accessed = 0;
		__uint64_t date_created = 0;
		__uint64_t date_modified = 0;
		__uint64_t date_accessed = 0;
		__uint64_t filesize;
		data_run_st data_run;
		data_run_st dir_data_run;

		memset(&data_run, 0, sizeof(data_run));
		memset(&dir_data_run, 0, sizeof(dir_data_run));



		int allocated_rh = 0;

		if (rh == NULL) {
			rh = (record_handler_ctx*)malloc(sizeof(record_handler_ctx));
			allocated_rh = 1;
		}

		memset(&(rh->bitmap), 0, sizeof(bitmap_st));


//		printf("At offset %d\n", mft_rec);

		// Read MFT attributes.

		long num_attr = 0;

		__uint16_t attr_pos;
		__uint16_t attr_data_pos;
		memcpy(&attr_pos, cluster + mft_offset + 20, 2);

		mft_attr_header attr_header;
		attr_data_pos = _read_mft_attribute_header(dd, cluster, &attr_header, mft_offset + attr_pos);

		while (attr_header.code != 0xffffffff) {
//			printf("Has attr %08x\n", attr_header.code);

			num_attr++;

			// [TODO] Temporarily catching directory that isn't working
			if (attr_header.code == 0x00000000) {
				printf("***** Bad attr header\n");
//				hexdump(cluster, NTFS_CLUSTER_SIZE);
				exit(0);
			}

			if (num_attr > 20) {
				printf("In weeds at %lu index %d\n", start_cluster, mft_rec);
				return 1;
			}


			if (attr_header.code == 0x00000010) { // $STANDARD_INFORMATION
				memcpy(&si_date_created, cluster + attr_data_pos, 8);
				memcpy(&si_date_modified, cluster + attr_data_pos + 8, 8);
				memcpy(&si_date_accessed, cluster + attr_data_pos + 24, 8);

			} else if (attr_header.code == 0x00000030) { // $FILE_NAME

				// Store all namespaces into independent variables -- posix_name, ntfs_name, dos_name, both_name
				// -- per 0, 1, 2, or 3 being found as namespace.  Prefer returning to callback by ntfs_name,
				// both_name, posix_name, and both_name.  Downconvert to 8-bit from 16-bit characters.

				__uint8_t namespace = cluster[mft_offset + attr_pos + 16 + 8 + 65];

				// Get index of the attribute payload -- attribute header is
				// 16 bytes, resident MFT attribute header is 8 bytes.

				//__uint16_t attr_data_pos = attr_pos + 16 + 8;

				// Read name.

				if (namespace >= 0 && namespace <= 3) {
					__uint8_t name_size;

					//printf("cluster %lu record %d name size %d\n", start_cluster, mft_rec, name_size);

					name_size = cluster[attr_data_pos + 64];

					filename[namespace] = (char*)malloc(name_size + 1);
					filename[namespace][name_size] = '\0';

					for (int i = 0; i < name_size; i++) {
						filename[namespace][i] = cluster[attr_data_pos + 66 + (i * 2)];
					}
				}

				// Read modified date/time.

				memcpy(&date_created, cluster + attr_data_pos + 8, 8);
				memcpy(&date_modified, cluster + attr_data_pos + 24, 8);
				memcpy(&date_accessed, cluster + attr_data_pos + 32, 8);

				// Read file size.

				memcpy(&filesize, cluster + attr_data_pos + 48, 8);

			} else if (attr_header.code == 0x00000080) { // $DATA
				// The data stream may be resident or non-resident.

				if (attr_header.nonresident) {
//					printf("first %lu, last %lu\n", first_vcn, last_vcn);

					if (attr_header.nr_compression_size) {
//						printf("attribute is compressed; skipping\n");
					} else {

						_read_mft_data_run(dd, cluster, &data_run, attr_data_pos, attr_header.nr_data_size);

					}
				} else {
					// [TODO] Allocate and give the data directly to the callback?
				}

				// Multiple data attributes for the same data stream can be used
				// in the attribute list to define different parts of the data stream data.
				// The first data stream attribute will contain the size of the entire data stream data.
				// Other data stream attributes should have a size of 0.


			} else if (attr_header.code == 0x00000090) { // $INDEX_ROOT

				// [TODO] Get this index_entry_size to read_dir() somehow.

				__uint16_t index_entry_size;
				memcpy(&index_entry_size, cluster + attr_data_pos + 8, 2);

			} else if (attr_header.code == 0x000000a0) { // $INDEX_ALLOCATION
				// Read directory data run if this is a directory index
				// (name == "$I30")

				// Sanity checks: name length must be 4, name must be "$I30",
				// attribute must be nonresident, data run must terminate within
				// bounds of attribute length.

				char name_size = cluster[mft_offset + attr_pos + 9];

//				printf("MFT attribute header\n");
//				hexdump(cluster + mft_offset + attr_pos, 16);
//				printf("\nNonresident MFT attribute data\n");
//				hexdump(cluster + mft_offset + attr_pos + 16, 48);
//				printf("\nName\n");
//				hexdump(cluster + mft_offset + attr_pos + 16 + 48, name_size * 2);
//				printf("\n");
//				hexdump(cluster + mft_offset + attr_pos + 16 + 48 + name_size * 2, 8);

				if (attr_header.name_size != 4) {
					// Skip if name isn't four characters long.
				} else if (memcmp(attr_header.name, "$\000I\0003\0000\000", 8) != 0) {
					// Skip if name isn't $I30 (in wide characters).
				} else if (attr_header.nonresident != 1) {
					// Skip if MFT isn't non-resident.
				} else {
					// Read data run for directory.

//					printf("Read dir data run\n");

					_read_mft_data_run(dd, cluster, &dir_data_run, attr_data_pos, attr_header.nr_data_size);
				}
			} else if (attr_header.code == 0x000000b0) { // $BITMAP
				rh->bitmap.used = 1;

				if (attr_header.nonresident == 1) {
					data_run_st bitmap_data_run;

					_read_mft_data_run(dd, cluster, &bitmap_data_run, attr_data_pos, attr_header.nr_data_size);

					// Scan the data run, noting any clusters that are missing.

					int data_run_complete = 1;

					// [TODO] Test this routine when a bitmap isn't complete.

					for (int i = 0; i < bitmap_data_run.entry_count; i++) {
						for (__uint64_t j = 0; j < bitmap_data_run.entry[i].count; j++) {
							if (!_cluster_is_safe(dd, bitmap_data_run.entry[i].cluster + j)) {
								// Mark cluster missing, indicate bitmap cannot be fully read.
								int mft_index = get_mft_index(dd, start_cluster, mft_rec);

								add_bad_cluster(dd, mft_index, bitmap_data_run.entry[i].cluster + j);
								data_run_complete = 0;
							}
						}
					}

					// If no clusters are missing, read the bitmap.

					if (data_run_complete) {
						if (attr_header.nr_first_vcn != 0) {
							// [TODO] Don't know how to handle.  Handle?
							printf("Bailing: don't know how to handle Virtual Clusters\n");
							exit(0);
						}

						rh->bitmap.length = attr_header.nr_data_size;

						rh->bitmap.data = (char*)malloc(rh->bitmap.length);

						int bytes_left = rh->bitmap.length;
						int bitmap_pos = 0;

						unsigned char *bitmap_cluster = (unsigned char*)malloc(NTFS_CLUSTER_SIZE);

						for (int i = 0; i < bitmap_data_run.entry_count; i++) {
							for (__uint64_t j = 0; j < bitmap_data_run.entry[i].count; j++) {
								read_cluster(dd, bitmap_cluster, bitmap_data_run.entry[i].cluster + j);

								int bytes_to_copy = NTFS_CLUSTER_SIZE;

								if (rh->bitmap.length - bitmap_pos < NTFS_CLUSTER_SIZE) {
									bytes_to_copy = rh->bitmap.length - bitmap_pos;
								}

								memcpy(rh->bitmap.data + bitmap_pos, bitmap_cluster, bytes_to_copy);

								bitmap_pos += bytes_to_copy;
							}
						}

						free(bitmap_cluster);

						rh->bitmap.valid = 1;

					} else {
						rh->bitmap.valid = 0;
					}
				} else {
					// Bitmap is resident; read into rh->bitmap

					//cluster + attr_data_pos

					rh->bitmap.used = 1;
					rh->bitmap.valid = 1;

					rh->bitmap.length = attr_header.r_data_size;

					rh->bitmap.data = (char*)malloc(rh->bitmap.length);

					memcpy(rh->bitmap.data, cluster + attr_data_pos, rh->bitmap.length);
				}
			}

			attr_pos += attr_header.size;

//			// Ensure next attr_header.code is available for while() test
//			memcpy(&attr_header.code, cluster + mft_offset + attr_pos, 4);

			if (attr_header.name != NULL) {
				free(attr_header.name);
				attr_header.name = NULL;
			}

			attr_data_pos = _read_mft_attribute_header(dd, cluster, &attr_header, mft_offset + attr_pos);
		}

		if (attr_header.name != NULL) {
			free(attr_header.name);
			attr_header.name = NULL;
		}

		// Send MFT details to callback.

		char* filename_to_send = NULL;
		int namespace_priority[4] = { 1, 0, 2, 3 };

		for (int i = 0; i < 4; i++) {
			filename_to_send = filename[namespace_priority[i]];

			if (filename_to_send != NULL) {
				break;
			}
		}

		if (filename_to_send != NULL) {
//			if (mft_flags & 0x0001 == 0) {
//				printf("Unused entry for %s\n", filename_to_send);
//				continue;
//			}

			__uint64_t mft_index = get_mft_index(dd, start_cluster, mft_rec);

			rh->mft_index = mft_index;
			rh->name = filename_to_send;

			rh->date_created = si_date_created;
			rh->date_modified = si_date_modified;
			rh->date_accessed = si_date_accessed;

			rh->filesize = filesize;
			rh->data_run = data_run;

			rh->dir_data_run = dir_data_run;

			(*handler)(dd, rh);

			if (rh->bitmap.used == 1 && rh->bitmap.valid == 1) {
				free(rh->bitmap.data);
			}

			if (data_run.entry_count > 0) {
				free(data_run.entry);

				data_run.entry_count = 0;
			}

			if (dir_data_run.entry_count > 0) {
				free(dir_data_run.entry);

				dir_data_run.entry_count = 0;
			}
		} else {
			// [TODO] Error, expected filename and didn't find one.
		}

		for (int i = 0; i < 4; i++) {
			if (filename[i] != NULL) {
				free(filename[i]);
			}
		}

		if (allocated_rh > 0) {
			free(rh);

			rh = NULL;
		}

		mft_offset += NTFS_HEADER.mft_size;
	}

	return 0;
}

/**
 * Walk through MFT from start to finish, passing each record through the
 * callback in mft_record_handler.
 *
 * @param dd DD context struct
 * @param mft_record_handler Callback function (with MFTRecordHandler
 *        interface) that will receive details of every successfully read MFT
 *        record
 * @return 0 (return could probably be made void...)
 */
int read_mft(dd_ctx* dd, MFTRecordHandler mft_record_handler)
{
	printf("Read $MFT, entry count %u\n", NTFS.mft_data_run.entry_count);

	for (int i = 0; i < NTFS.mft_data_run.entry_count; i++) {
//		printf("Reading data run %d, cluster %lu, count %u\n", i, mft_data_run.entry[i].cluster, mft_data_run.entry[i].count);
		for (__uint64_t j = 0; j < NTFS.mft_data_run.entry[i].count; j++) {
			if (read_mft_record(dd, NTFS.mft_data_run.entry[i].cluster + j, mft_record_handler, NULL)) {
				MARK_FAILED_CLUSTER(NTFS.mft_data_run.entry[i].cluster + j);
				//return 1;
			}
		}
	}

	return 0;
}

typedef struct restore_ntfs_st {
	char* filename;
	//__uint64_t mft_index;
	file_name_st* fileinfo;
} restore_ntfs_st;

// [TODO] Return result in rh->result.
/**
 * Callback function for read_mft_record() that attempts to restore a file
 * from the NTFS image based on its MFT index.
 *
 * @param dd DD context struct
 * @param rh Record handler context struct (rh->param should be
 *           a __uint64_t* to the MFT index number of the file to restore)
 */
static void mft_record_handler_restore_ntfs(dd_ctx *dd, record_handler_ctx *rh)
{
//	printf("%lu | %lu\n", rh->mft_index, *(__uint64_t*)rh->param);

	unsigned char* cluster;

	cluster = (unsigned char*)malloc(NTFS_CLUSTER_SIZE);

	restore_ntfs_st *param = (restore_ntfs_st*)rh->param;

	if (rh->mft_index == param->fileinfo->id) {
//		printf("in restore\n");

		__uint64_t num_written = 0;

//		char out_filename[65535];
//
//		out_filename[0] = '\0';
//		strcat(out_filename, "restore/");
//		strcat(out_filename, rh->name);

		char* out_filename = param->filename;

		FILE* fil = fopen(out_filename, "wb");

		for (int i = 0; i < rh->data_run.entry_count; i++) {
			for (__uint64_t j = 0; j < rh->data_run.entry[i].count; j++) {
				if (read_cluster(dd, cluster, rh->data_run.entry[i].cluster + j)) {
					printf("BAD CLUSTER IN RESTORE\n");
					MARK_FAILED_CLUSTER(rh->data_run.entry[i].cluster + j);

					//free(cluster)
					//return 1;
				}

				__uint64_t size = NTFS_CLUSTER_SIZE;

				if (rh->data_run.size - num_written < NTFS_CLUSTER_SIZE) {
					size = rh->data_run.size - num_written;
				}

				fwrite(cluster, size, 1, fil);

				num_written += size;
			}
		}

		fclose(fil);

		struct utimbuf ut;

		ut.modtime = (param->fileinfo->date_modified - 116444736000000000) / 10000000;
		ut.actime = (param->fileinfo->date_accessed - 116444736000000000) / 10000000;

		utime(out_filename, &ut);
	}

	free(cluster);
}

int restore_ntfs(dd_ctx* dd, const char* path, file_name_st* file)
{
//	_restore_mft_index = mft_index;

	restore_ntfs_st param;

//	param.mft_index = file->id;
	param.fileinfo = file;
	param.filename = (char*)malloc(strlen(path) + strlen(file->ascii_name) + 1);

	strcpy(param.filename, path);
	strcat(param.filename, file->ascii_name);

//	printf("calling read_mft_record on cluster %lu\n", get_mft_cluster(dd, mft_index));

	record_handler_ctx rh;
	memset(&rh, 0, sizeof(record_handler_ctx));

	// Set parameter.

	rh.param = &param;

	// [TODO] Verify cluster exists from get_mft_cluster.

	read_mft_record(dd, get_mft_cluster(dd, file->id), &mft_record_handler_restore_ntfs, &rh);

	free(param.filename);
}

int read_dir_cluster(dd_ctx* dd, file_name_st** files, __uint64_t* dir_pos, bitmap_st* bitmap, __uint32_t mft_index, __uint64_t cluster_pos)
{
	// [TODO] Get size of index entry from index root.  Right now assuming cluster size, but it can
	// be smaller (like with MFT entries.)

	int index_record_size = NTFS_CLUSTER_SIZE;
	int sectors_per_entry = index_record_size / NTFS_HEADER.bytes_per_sector;

	unsigned char* cluster;

	cluster = (unsigned char*)malloc(NTFS_CLUSTER_SIZE);

	if (read_cluster(dd, cluster, cluster_pos)) {
		// Unable to read directory cluster

		add_bad_cluster(dd, mft_index, cluster_pos);

		free(cluster);
		return 1;
	}

	if (memcmp(cluster, "INDX", 4) != 0) {
		// [TODO] Failed to find index signature.

		free(cluster);
		return 1;
	}

	// Find and apply fix-ups.

	__uint16_t fix_up_offset;
	__uint16_t fix_up_count;
	__uint16_t fix_up_value;

	memcpy(&fix_up_offset, cluster + 4, 2);
	memcpy(&fix_up_count, cluster + 6, 2);
	memcpy(&fix_up_value, cluster + fix_up_offset, 2);

	if (fix_up_count < sectors_per_entry) {
		ERR("Failed to find expected number of fix up records at cluster %lu\n", cluster_pos);

		free(cluster);
		return 1;
	}

	for (int index_sector = 0; index_sector < sectors_per_entry; index_sector++) {
		if (memcmp(cluster + (512 * (index_sector + 1)) - 2, &fix_up_value, 2)) {
			ERR("Cluster %lu contains invalid fix up placeholder (bad sector?)\n", cluster_pos);

			free(cluster);
			return 1;
		}

		memcpy(cluster + (512 * (index_sector + 1)) - 2,
				cluster + fix_up_offset + 2 + (index_sector * 2),
				2);
	}

	__uint32_t index_values_offset;
	memcpy(&index_values_offset, cluster + 24, 4);

	// Add node header size to offset.

	index_values_offset += 24;

	file_name_st *current_file;

	do {
		__uint32_t file_mft_index;
		__uint16_t file_sequence_number;
		__uint16_t index_entry_size;
		__uint16_t index_value_size;
		__uint16_t index_entry_flags;

		memcpy(&file_mft_index, cluster + index_values_offset, 4);
		memcpy(&file_sequence_number, cluster + index_values_offset + 6, 2);
		memcpy(&index_entry_size, cluster + index_values_offset + 8, 2);
		memcpy(&index_value_size, cluster + index_values_offset + 10, 2);
		memcpy(&index_entry_flags, cluster + index_values_offset + 12, 2);

//		printf("File ref: %08x\n", file_mft_index);
//		printf("Index entry size: %d\n", index_entry_size);
//		printf("Index value size: %d\n", index_value_size);
//		printf("Index entry flags: %04x\n", index_entry_flags);

//		hexdump(cluster + index_values_offset + 16, index_entry_size - 16);
//
//		printf("\n");

//		index_entry_size += index_key_data_size +
//				(index_key_data_size % 8 > 0 ? 8 - index_key_data_size % 8 : 0);

		if (index_entry_flags & 0x0001) {
			// Has sub node
			// [TODO] This threw off the count... temporarily removing
			//index_entry_size += 8;
		}

		if (index_entry_flags & 0x0002) {
			// Last entry
			break;
		}

		// [TODO] Sanity check

		if (index_entry_size < 16) {
			// [TODO] Raise error, index corrupt
			break;
		}

		if (!(index_entry_flags & 0x00000002) && index_values_offset + index_entry_size + 16 > index_record_size) {
			// [TODO] Raise error, index corrupt
			break;
		}


		ntfs_file_name_st file_name;

		_read_file_name(dd, cluster, &file_name, index_values_offset + 16);


		// Search for file in hash table by MFT index.

		HASH_FIND_INT(*files, &file_mft_index, current_file);

		int new_hash_entry = 0;

		// Create record if not found in hash table.

		if (current_file == NULL) {
			current_file = (file_name_st*)malloc(sizeof(file_name_st));

			memset(current_file, 0, sizeof(file_name_st));

			current_file->id = file_mft_index;

			new_hash_entry = 1;
		}

		// Prefer long filenames for the ascii_name field.

		if (new_hash_entry || current_file->name_dos_len > 0) {
			free(current_file->ascii_name);

			unwiden_string(&current_file->ascii_name, file_name.name, file_name.name_size);
		}

		// Read filename into name_dos (short filename) or name (long filename).

		if (file_name.name_namespace == 2) {
			current_file->name_dos = (char*)malloc(file_name.name_size * 2);
			current_file->name_dos_len = file_name.name_size;

			current_file->name = NULL;
			current_file->name_len = 0;

			memcpy(current_file->name_dos, file_name.name, file_name.name_size * 2);
		} else {
			current_file->name_dos = NULL;
			current_file->name_dos_len = 0;

			current_file->name = (char*)malloc(file_name.name_size * 2);
			current_file->name_len = file_name.name_size;

			memcpy(current_file->name, file_name.name, file_name.name_size * 2);
		}

		// Copy other relevant fields from file name record.

		current_file->attributes = file_name.attributes;

		if (bitmap_is_set(dd, bitmap, *dir_pos)) {
			current_file->deleted = 0;
		} else {
			current_file->deleted = 1;
		}

		current_file->date_accessed = file_name.date_accessed;
		current_file->date_created = file_name.date_created;
		current_file->date_modified = file_name.date_modified;

		// Add record to hash table if new.

		if (new_hash_entry) {
			HASH_ADD_INT(*files, id, current_file);
		}

		free(file_name.name);

		index_values_offset += index_entry_size;

	} while (1);




	free(cluster);
}

/**
 * Callback function for read_mft_record() that attempts to read a directory
 * from the MFT.
 *
 * @param dd DD context struct
 * @param rh Record handler context struct (rh->param should be
 *           a __uint64_t* to the MFT index number of the directory to read)
 * @return Nothing (but rh->result set to hash table of directory)
 */
static void mft_record_handler_read_dir(dd_ctx *dd, record_handler_ctx *rh)
{
	file_name_st *files = NULL;

	if (rh->mft_index == *(__uint64_t*)rh->param) {

		// Scan the data run, noting any clusters that are missing.

		for (int i = 0; i < rh->dir_data_run.entry_count; i++) {
			for (__uint64_t j = 0; j < rh->dir_data_run.entry[i].count; j++) {
				if (!_cluster_is_safe(dd, j)) {
					// [TODO] Mark cluster missing, set flag to bail.

				}
			}
		}

		// If no clusters are missing, read the directory.

		__uint64_t dir_pos = 0;

		for (int i = 0; i < rh->dir_data_run.entry_count; i++) {
			for (__uint64_t j = 0; j < rh->dir_data_run.entry[i].count; j++) {
				read_dir_cluster(dd, &files, &dir_pos, &(rh->bitmap), rh->mft_index, rh->dir_data_run.entry[i].cluster + j);
			}
		}

		rh->result = files;
	}
}

NTFS_DIR* open_dir(dd_ctx* dd, __uint64_t mft_index)
{
	record_handler_ctx rh;
	memset(&rh, 0, sizeof(record_handler_ctx));

	// Set parameter to MFT index of directory to read.

	rh.param = &mft_index;

	read_mft_record(dd, get_mft_cluster(dd, mft_index), &mft_record_handler_read_dir, &rh);

	// [TODO] Return NULL if read failed, defining problems (probably missing clusters) in dd.

	NTFS_DIR* dir = (NTFS_DIR*)malloc(sizeof(NTFS_DIR));

	dir->files = rh.result;
	dir->current_file = dir->files;

	return dir;
}

NTFS_FILE* read_dir_file(dd_ctx* dd, NTFS_DIR* dir)
{
	if (dir == NULL || dir->current_file == NULL) {
		return NULL;
	}

//	printf("%d\n", dir->current_file->id);

	while (dir->current_file != NULL && !bitmap_is_set(dd, &(NTFS.mft_bitmap), dir->current_file->id)) {
		dir->current_file = dir->current_file->hh.next;
	}

	NTFS_FILE* current_file = dir->current_file;

	if (current_file == NULL) {
		return NULL;
	}

	dir->current_file = dir->current_file->hh.next;

	return current_file;
}

void rewind_dir(dd_ctx* dd, NTFS_DIR* dir)
{
	dir->current_file = dir->files;
}

int close_dir(dd_ctx* dd, NTFS_DIR** dir)
{
	if (*dir == NULL) {
		return 0;
	}

	// Clean up hash table

	file_name_st *files = (*dir)->files;
	file_name_st *current_file;
	file_name_st *tmp;

	HASH_ITER(hh, files, current_file, tmp) {
		HASH_DEL(files, current_file);

		free(current_file->name_dos);
		free(current_file->name);
		free(current_file->ascii_name);
		free(current_file);
	}

	free(*dir);
}


static void mft_record_handler_data_run_check(dd_ctx *dd, record_handler_ctx *rh)
{
	file_name_st *files = NULL;

	//

	if (rh->mft_index == *(__uint64_t*)rh->param) {

		// Scan the data run, noting any clusters that are missing.

		int complete = 1;

		for (int i = 0; i < rh->data_run.entry_count; i++) {
			for (__uint64_t j = 0; j < rh->data_run.entry[i].count; j++) {
				if (!_cluster_is_safe(dd, rh->data_run.entry[i].cluster + j)) {
					add_bad_cluster(dd, rh->mft_index, rh->data_run.entry[i].cluster + j);

					complete = 0;
				}
			}
		}

		*((int*)rh->result) = complete;

		return;
	}
}

int data_run_complete(dd_ctx* dd, __uint64_t mft_index)
{
	record_handler_ctx rh;
	memset(&rh, 0, sizeof(record_handler_ctx));

	// Set parameter to MFT index of directory to read.

	rh.param = &mft_index;

	int complete = 0;

	rh.result = &complete;

	__uint64_t cluster_pos = get_mft_cluster(dd, mft_index);

	if (cluster_pos == UINT64_MAX) {
		printf("Failed to find cluster for mft_index %lu!\n", mft_index);

		//exit(0);
		return 0;
	}

	if (_cluster_is_safe(dd, cluster_pos) == 0) {
		printf("BAD CLUSTER IN RESTORE\n");

		add_bad_cluster(dd, mft_index, cluster_pos);

		//printf("CLUSTER %lu UNSAFE\n", cluster_pos);
	}

	read_mft_record(dd, cluster_pos, &mft_record_handler_data_run_check, &rh);

	return complete;
}
