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

#pragma once

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <uchar.h>

#include <uthash-master/uthash.h>
#include <uthash-master/utarray.h>

#include "reader.h"


typedef struct data_run_entry {
	__uint64_t cluster;
	__uint32_t count;
	__uint8_t sparse;
} data_run_entry;

typedef struct data_run_st {
	data_run_entry* entry; // Data run records.
	__uint32_t entry_count; // Number of elements in "entry" array.
	__uint64_t size; // Size of data represented by data run, in bytes.  Use to truncate data read from final cluster if necessary.

	__uint64_t mft_cluster; // Cluster of MFT record data run is associated with
	__uint8_t mft_rec; // Index of MFT record data run is associated with
} data_run_st;

typedef struct bitmap_st {
	int used;
	int valid;

	int length;

	char* data;
} bitmap_st;

typedef struct ntfs_file_name_st {
	__uint32_t parent_mft_index;
	__uint16_t parent_sequence_num;
	__uint64_t date_created;
	__uint64_t date_modified;
	__uint64_t date_mft_modified;
	__uint64_t date_accessed;
	__uint64_t allocated_filesize;
	__uint64_t filesize;
	__uint32_t attributes;
	__uint32_t extended_data;
	__uint8_t name_size;
	__uint8_t name_namespace;
	char* name;
} ntfs_file_name_st;

typedef struct ntfs_header {
	__uint16_t bytes_per_sector;
	__uint8_t sectors_per_cluster;
	__uint64_t mft_cluster;
	__uint64_t mftmirror_cluster;
	__uint64_t mft_size;
} ntfs_header;

typedef struct ntfs_struct {
	FILE* disc;
	__uint64_t partition_offset;

	ntfs_header ntfs_header;

//	unsigned char* cluster;

	data_run_st mft_data_run;

	bitmap_st mft_bitmap;
} ntfs_st;

typedef struct safe_region_st {
	__uint64_t start;
	__uint64_t length;
	struct safe_region_st *next;
} safe_region_st;

// Old bad cluster tracker

typedef struct failed_cluster_st {
	__uint64_t cluster;
	struct failed_cluster_st *next;
} failed_cluster_st;


// New bad cluster tracker

typedef struct bad_cluster_st {
	__uint64_t id; // cluster

	UT_hash_handle hh;
} bad_cluster_st;

typedef struct bad_cluster_by_mft_index_st {
	__uint32_t id; // mft index
	bad_cluster_st* bad_clusters;

	UT_hash_handle hh;
} bad_cluster_by_mft_index_st;



// Overlay

typedef struct cluster_index_st {
	__uint64_t id; // cluster number
	__uint64_t file_pos; // position of cluster in overlay

	UT_hash_handle hh;
} cluster_index_st;

typedef struct overlay_ctx_st {
	FILE* overlay_file;

	char* overlay_filename;
	char* index_filename;
	char* index_tmp_filename;

	cluster_index_st *index;
} overlay_ctx;


// DD context

typedef struct dd_ctx {
	int error;
	char error_msg[4096];
	char line_error_msg[4096];

	__uint64_t current_pos;
	char current_status;
	int pass;

	FILE* disc;
	long disc_size;
	ntfs_st ntfs;

	failed_cluster_st *failed_clusters;
	failed_cluster_st *failed_cluster_pos;

	bad_cluster_by_mft_index_st* bad_clusters_by_mft_index;
	bad_cluster_st* bad_clusters;

	safe_region_st *safe_regions;
	safe_region_st *safe_region_pos;

	overlay_ctx overlay;
	reader_ctx reader;
} dd_ctx;



#define NTFS dd->ntfs
#define NTFS_HEADER dd->ntfs.ntfs_header
#define NTFS_CLUSTER_SIZE (NTFS_HEADER.bytes_per_sector * NTFS_HEADER.sectors_per_cluster)
//#define NTFS_CLUSTER NTFS.cluster

#define ADD_SAFE_REGION(regionart, region_length) \
	if (dd->safe_regions == NULL) { \
		dd->safe_regions = (safe_region_st*)malloc(sizeof(safe_region_st)); \
		dd->safe_region_pos = dd->safe_regions; \
	} else { \
		dd->safe_region_pos->next = (safe_region_st*)malloc(sizeof(safe_region_st)); \
		dd->safe_region_pos = dd->safe_region_pos->next; \
	} \
	dd->safe_region_pos->start = regionart; \
	dd->safe_region_pos->length = region_length; \
	dd->safe_region_pos->next = NULL;

#define MARK_FAILED_CLUSTER(cluster_fail) \
	if (dd->failed_clusters == NULL) { \
		dd->failed_clusters = (failed_cluster_st*)malloc(sizeof(failed_cluster_st)); \
		dd->failed_cluster_pos = dd->failed_clusters; \
	} else { \
		dd->failed_cluster_pos->next = (failed_cluster_st*)malloc(sizeof(failed_cluster_st)); \
		dd->failed_cluster_pos = dd->failed_cluster_pos->next; \
	} \
	dd->failed_cluster_pos->cluster = cluster_fail; \
	dd->failed_cluster_pos->next = NULL;

typedef struct record_handler_ctx {
	__uint64_t mft_index;
	const char* name;

	__uint64_t date_created;
	__uint64_t date_modified;
	__uint64_t date_accessed;

	__uint64_t filesize;
	data_run_st data_run;

	data_run_st dir_data_run;

	bitmap_st bitmap;

	void* param;
	void* result;
} record_handler_ctx;

typedef void (*MFTRecordHandler)(dd_ctx *dd, record_handler_ctx *rh);

/* Hash table stuff for handling directories */

typedef struct file_name_st {
	int id;

	char* name;
	int name_len;

	char* name_dos;
	int name_dos_len;

	char* ascii_name;

	__uint64_t date_accessed;
	__uint64_t date_created;
	__uint64_t date_modified;

	int deleted;

	__uint32_t attributes;

	UT_hash_handle hh;
} file_name_st;

typedef struct ntfs_dir_st {
	file_name_st *files;

	file_name_st *current_file;
} NTFS_DIR;

typedef struct file_name_st NTFS_FILE;

/* Interface */

int init_dd(dd_ctx* dd);
int cleanup_dd(dd_ctx* dd);




int open_ntfs(dd_ctx* dd, const char *filename, __uint64_t partition_offset);
int read_mft_record(dd_ctx *dd, __uint64_t start_cluster, MFTRecordHandler handler, record_handler_ctx *rh);
int read_mft(dd_ctx* dd, MFTRecordHandler record_handler);
int restore_ntfs(dd_ctx* dd, const char* path, file_name_st* file);

NTFS_DIR* open_dir(dd_ctx* dd, __uint64_t mft_index);
NTFS_FILE* read_dir_file(dd_ctx* dd, NTFS_DIR* dir);
void rewind_dir(dd_ctx* dd, NTFS_DIR* dir);
int close_dir(dd_ctx* dd, NTFS_DIR** dir);

void close_ntfs(dd_ctx* dd);

int read_cluster(dd_ctx *dd, unsigned char* cluster, __uint64_t cluster_pos);

int data_run_complete(dd_ctx* dd, __uint64_t mft_index);

