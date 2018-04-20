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

#include "badclusters.h"
#include "dd.h"

void cleanup_bad_cluster_hashes(dd_ctx* dd)
{
	bad_cluster_st *current_bad_clusters;
	bad_cluster_st *bad_clusters_tmp;

	bad_cluster_by_mft_index_st *current_bad_clusters_by_mft_index;
	bad_cluster_by_mft_index_st *bad_clusters_by_mft_index_tmp;

	HASH_ITER(hh, dd->bad_clusters, current_bad_clusters, bad_clusters_tmp) {
		HASH_DEL(dd->bad_clusters, current_bad_clusters);
	}

	HASH_ITER(hh, dd->bad_clusters_by_mft_index, current_bad_clusters_by_mft_index, bad_clusters_by_mft_index_tmp) {
		HASH_ITER(hh, current_bad_clusters_by_mft_index->bad_clusters, current_bad_clusters, bad_clusters_tmp) {
			HASH_DEL(current_bad_clusters_by_mft_index->bad_clusters, current_bad_clusters);
		}
	}
}

void add_bad_cluster(dd_ctx* dd, __uint32_t mft_index, __uint64_t cluster)
{

	bad_cluster_st *bad_cluster_list;

	HASH_FIND(hh, dd->bad_clusters, &cluster, sizeof(__uint64_t), bad_cluster_list);

	if (bad_cluster_list == NULL) {
		bad_cluster_list = (bad_cluster_st*)malloc(sizeof(bad_cluster_st));

		bad_cluster_list->id = cluster;

		HASH_ADD(hh, dd->bad_clusters, id, sizeof(__uint64_t), bad_cluster_list);
	}

	bad_cluster_by_mft_index_st *bad_clusters_by_mft_index;

	HASH_FIND(hh, dd->bad_clusters_by_mft_index, &mft_index, sizeof(__uint32_t), bad_clusters_by_mft_index);

	if (bad_clusters_by_mft_index == NULL) {
		bad_clusters_by_mft_index = (bad_cluster_by_mft_index_st*)malloc(sizeof(bad_cluster_by_mft_index_st));
		bad_clusters_by_mft_index->id = mft_index;
		bad_clusters_by_mft_index->bad_clusters = NULL;

		bad_cluster_list = (bad_cluster_st*)malloc(sizeof(bad_cluster_st));
		bad_cluster_list->id = cluster;

		HASH_ADD(hh, bad_clusters_by_mft_index->bad_clusters, id, sizeof(__uint64_t), bad_cluster_list);

		HASH_ADD(hh, dd->bad_clusters_by_mft_index, id, sizeof(__uint32_t), bad_clusters_by_mft_index);
	} else {
		HASH_FIND(hh, bad_clusters_by_mft_index->bad_clusters, &cluster, sizeof(__uint64_t), bad_cluster_list);

		if (bad_cluster_list == NULL) {
			bad_cluster_list = (bad_cluster_st*)malloc(sizeof(bad_cluster_st));

			bad_cluster_list->id = cluster;

			HASH_ADD(hh, bad_clusters_by_mft_index->bad_clusters, id, sizeof(__uint64_t), bad_cluster_list);
		}
	}
}


/**
 * Taking a sorted list of bad clusters, print a list of byte regions
 * that can be used with ddrescue.
 *
 * @param bad_clusters uthash list of bad clusters
 */
void print_byte_regions_from_clusters(dd_ctx* dd, bad_cluster_st *bad_clusters)
{
	bad_cluster_st *current_bad_cluster;
	bad_cluster_st *bad_cluster_tmp;

	__uint64_t start_region = UINT64_MAX;
	__uint64_t end_region = UINT64_MAX;

	HASH_ITER(hh, bad_clusters, current_bad_cluster, bad_cluster_tmp) {
		if (start_region == UINT64_MAX) {
			start_region = current_bad_cluster->id;
			end_region = current_bad_cluster->id;
		} else {
			if (current_bad_cluster->id > end_region + 1) {
				// Emit start_region -> end_region as byte offset and length.

				__uint64_t byte_pos = NTFS.partition_offset + NTFS_CLUSTER_SIZE * start_region;
				__uint64_t byte_len = (end_region - start_region + 1) * NTFS_CLUSTER_SIZE;

				printf("%lX %lX\n", byte_pos, byte_len);

				// Begin new region.

				start_region = current_bad_cluster->id;
			}

			end_region = current_bad_cluster->id;
		}
	}

	// Emit last region (if bad cluster list wasn't empty to begin with.)

	if (start_region != UINT64_MAX) {
		__uint64_t byte_pos = NTFS.partition_offset + NTFS_CLUSTER_SIZE * start_region;
		__uint64_t byte_len = (end_region - start_region + 1) * NTFS_CLUSTER_SIZE;

		printf("%lX %lX\n", byte_pos, byte_len);
	}
}


int sort_bad_clusters_by_id(bad_cluster_st *a, bad_cluster_st *b)
{
	return (a->id - b->id);
}

int sort_bad_clusters_by_mft_index_by_id(bad_cluster_by_mft_index_st *a, bad_cluster_by_mft_index_st *b)
{
	return (a->id - b->id);
}

void dump_bad_clusters(dd_ctx* dd)
{
	bad_cluster_st *current_bad_cluster;
	bad_cluster_st *bad_cluster_tmp;

	bad_cluster_by_mft_index_st *current_bad_clusters_by_mft_index;
	bad_cluster_by_mft_index_st *bad_clusters_by_mft_index_tmp;

	HASH_SORT(dd->bad_clusters, sort_bad_clusters_by_id);
	HASH_SORT(dd->bad_clusters_by_mft_index, sort_bad_clusters_by_mft_index_by_id);

	printf("Global bad clusters:\n");

	HASH_ITER(hh, dd->bad_clusters, current_bad_cluster, bad_cluster_tmp) {
		printf("%lu\n", current_bad_cluster->id);
	}

	printf("\nBad clusters by file/dir:\n");

	HASH_ITER(hh, dd->bad_clusters_by_mft_index, current_bad_clusters_by_mft_index, bad_clusters_by_mft_index_tmp) {
		printf("%10u | ", current_bad_clusters_by_mft_index->id);

		HASH_SORT(current_bad_clusters_by_mft_index->bad_clusters, sort_bad_clusters_by_id);

		HASH_ITER(hh, current_bad_clusters_by_mft_index->bad_clusters, current_bad_cluster, bad_cluster_tmp) {
			printf("%lu ", current_bad_cluster->id);
		}

		printf("\n");
	}

	printf("\n");

	print_byte_regions_from_clusters(dd, dd->bad_clusters);
}
