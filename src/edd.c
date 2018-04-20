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
#include "hexdump.h"
#include "mapfile.h"
#include "reader.h"
#include "overlay.h"
#include "badclusters.h"

#include <stdio.h>
#include <string.h>

#include <fcntl.h>
#include <sys/ioctl.h>

#include <stdlib.h>

#include <unistd.h>

#include <linux/hdreg.h>

#include <scsi/sg.h>

#include <scsicmd/scsicmd.h>


#include <time.h>
#include <utime.h>

#include <ctype.h>

#include <sys/stat.h>
#include <sys/types.h>

#define HDD "/dev/sdc"

struct sense_info_t g_si;

void bail_on_bad_status(const char* status, int ret, struct reader_ctx_struct* reader) {
	if (ret != 0) {
		printf("%s: Failed to do inquiry ioctl, rv: %d; exiting\n", status, ret);

		exit(2);
	}

	if (reader->senseinfo.sense_key != 0) {
		printf("%s: Sense %02x, %02x, %02x; exiting\n",
						 status, reader->senseinfo.sense_key, reader->senseinfo.asc, reader->senseinfo.ascq);

		exit(3);
	}
}

//int do_inquiry(int fd, char* dxfer, unsigned int dxfer_len) {
//
//	struct sg_io_hdr hdr;
//
//	char cmd[11];
//	cdb_inquiry(cmd, 1, 0xB0, 256);
//
//	memset(&hdr, 0, sizeof(sg_io_hdr_t));
//	memset(g_sb, 0, g_sb_len);
//	memset(dxfer, 69, dxfer_len);
//
//	hdr.interface_id = 'S';
//	hdr.dxfer_direction = SG_DXFER_FROM_DEV;
//	hdr.cmd_len = sizeof(cmd);
//	hdr.mx_sb_len = g_sb_len;
//	hdr.iovec_count = 0;
//	hdr.dxfer_len = dxfer_len;
//	hdr.dxferp = dxfer;
//	hdr.cmdp = cmd;
//	hdr.sbp = g_sb;
//	hdr.timeout = 5000;
//	hdr.flags = 0;
//	hdr.pack_id = 0;
//	hdr.usr_ptr = 0;
//
//	int ret = ioctl(fd, SG_IO, &hdr);
//
//	if (ret == 0) {
//		scsi_parse_sense(g_sb, hdr.sb_len_wr, &g_si);
//
//		for (int i = 0; i < 256; i++) {
//			printf("%02x\n", (unsigned char)dxfer[i]);
//		}
//	}
//
//	return ret;
//}

//time_t max_time = 0;

void mft_record_handler(dd_ctx *dd, record_handler_ctx *rh)
{
	// Convert modified date to Unix timestamp

	time_t date_created_unix = (rh->date_created - 116444736000000000) / 10000000;
	time_t date_modified_unix = (rh->date_modified - 116444736000000000) / 10000000;
	time_t date_accessed_unix = (rh->date_accessed - 116444736000000000) / 10000000;

//	printf("%lu\n", date_modified_unix);
//	printf("%lu\n", 2000000000lu);

//	if ((date_modified_unix < 2000000000lu) && (date_modified_unix > max_time)) {
//		printf("%lu\n", date_modified_unix);
//		max_time = date_modified_unix;
//	}

	if (rh->data_run.entry_count > 0) {
		rh->filesize = rh->data_run.size;
	}

	char* namecmp = (char*)malloc(strlen(rh->name)+1);

	for (int i = 0; i < strlen(rh->name); i++) {
		namecmp[i] = tolower(rh->name[i]);
	}

	namecmp[strlen(rh->name)] = '\0';

	char dt_created[80];
	char dt_modified[80];
	char dt_accessed[80];

	strftime(dt_created, 80, "%F", localtime(&date_created_unix));
	strftime(dt_modified, 80, "%F", localtime(&date_modified_unix));
	strftime(dt_accessed, 80, "%F", localtime(&date_accessed_unix));

	char *dot = strrchr(namecmp, '.');
	char *ext;

	if (dot != NULL) {
		dot++;

		ext = (char*)malloc(strlen(dot) + 1);
		strcpy(ext, dot);
	} else {
		ext = (char*)malloc(1);
		ext[0] = '\0';
	}

//	if (date_modified_unix > 1483232461) {
//		if (!strcmp(namecmp + strlen(namecmp) - 4, ".123")) {
//			printf("%s: %lu, %lu\n", name, date_modified_unix, filesize);
//		}
//		if (!strcmp(namecmp + strlen(namecmp) - 4, ".123")) {
			printf("|%s|%s|%s|%s|%s|%lu|%lu\n", rh->name, ext, dt_created, dt_modified, dt_accessed, rh->mft_index, rh->filesize);
//		}
//		if (strstr(namecmp, "country")) {
//			printf("%s: %s %lu\n", name, datetime, filesize);
//		}
//
//}

	free(ext);

//	if (date_modified_unix >= 1483232461) {
//		printf("%s: %lu, %lu\n", name, date_modified_unix, filesize);
//	}
}

void dump_failed_clusters(dd_ctx *dd)
{
	dd->failed_cluster_pos = dd->failed_clusters;

	if (dd->failed_cluster_pos == NULL) {
		return;
	}

	unsigned long first = 0xffffffff;
	unsigned long last;

	unsigned long cluster_size = dd->ntfs.ntfs_header.bytes_per_sector * dd->ntfs.ntfs_header.sectors_per_cluster;

	while (dd->failed_cluster_pos != NULL) {
		if (dd->failed_cluster_pos->cluster - last > 1) {
			if (first != 0xffffffff) {
				printf("%lX - %lX\n", 0x346500000 + (first * cluster_size), 0x346500000 + (last * cluster_size));
			}

			first = dd->failed_cluster_pos->cluster;
		}

		last = dd->failed_cluster_pos->cluster;

		dd->failed_cluster_pos = dd->failed_cluster_pos->next;
	}

	printf("%lX - %lX\n", 0x346500000 + (first * cluster_size), 0x346500000 + (last * cluster_size));
}

void walk_dir(dd_ctx* dd, __uint32_t mft_index, const char* path) {
	NTFS_DIR* dir = open_dir(dd, mft_index);
	NTFS_FILE* file;

	char is_dir[2];
	is_dir[1] = '\0';

//	mkdir(path, 0777);
//	printf("%s\n", path);

	while (file = read_dir_file(dd, dir)) {
		if (file->attributes & 0x10000000) {
			if (strcmp(file->ascii_name, ".") && strcmp(file->ascii_name, "..")) {
				//printf("GOING TO WALK %s\n", file->ascii_name);

				char* new_path = (char*)malloc(strlen(path) + strlen(file->ascii_name) + 2);
				strcpy(new_path, path);
				strcat(new_path, file->ascii_name);
				strcat(new_path, "/");


				struct utimbuf ut;

				ut.modtime = (file->date_modified - 116444736000000000) / 10000000;
				ut.actime = (file->date_accessed - 116444736000000000) / 10000000;

				utime(new_path, &ut);


				walk_dir(dd, file->id, new_path);

				free(new_path);
			}
			is_dir[0] = '*';
		} else {
			is_dir[0] = ' ';
		}

		if (!file->deleted) {
			printf("%s%10d | %s\n", is_dir, file->id, file->ascii_name);

			if (!(file->attributes & 0x10000000)) {
				restore_ntfs(dd, path, file);
			}
		}
		if (strcmp(file->ascii_name, "$BadClus") && !data_run_complete(dd, file->id)) {
//			printf("%s%10d | %s\n", is_dir, file->id, file->ascii_name);
		}
	}

	close_dir(dd, &dir);
}

int main() {
//	int tsize = 2;
//	unsigned char test[2] = { 0x00, 0x80 };
//
//	__int64_t ltest;
//
//	printf("%hhu\n", (test[tsize - 1] >> 4));
//
//	if (test[tsize - 1] >> 4 >= 8) {
//		memset(&ltest, 0xff, 8);
//	} else {
//		memset(&ltest, 0, 8);
//	}
//
//	memcpy(&ltest, test, tsize);
//
//	printf("%ld\n", ltest);
//
//	return 0;

	dd_ctx dd;

	if (init_dd(&dd)) {
		printf("%s", dd.error_msg);

		cleanup_dd(&dd);

		return 1;
	}

	if (read_mapfile(&dd, "/mnt/dump/dump.log")) {
		printf("%s", dd.error_msg);

		cleanup_dd(&dd);

		return 1;
	}

	open_ntfs(&dd, "/mnt/dump/disc", 0x346500000);

//	check_file_condition(&dd, 250201);

//	restore_ntfs(&dd, 258482);
//

	open_overlay(&dd, "../data/overlay");

	//recover_to_overlay(&dd, "/dev/sdc", 36874441, 1);

//	unsigned char cluster[4096];

//	printf("%d\n", read_cluster(&dd, cluster, 36874441));
//
//	printf("%s", dd.error_msg);
//	hexdump(cluster, 4096);

	walk_dir(&dd, 167, "/tmp/ernie/Users/Ernie/");
//
	dump_bad_clusters(&dd);

	// Attempt to rescue bad clusters

	bad_cluster_st *current_bad_cluster;
	bad_cluster_st *bad_cluster_tmp;

	dd.error_msg[0] = '\0';

	HASH_ITER(hh, dd.bad_clusters, current_bad_cluster, bad_cluster_tmp) {
		printf("Recovering %lu", current_bad_cluster->id);
//		if (current_bad_cluster->id * 4096 > 0x57D0000000) {
			if (recover_to_overlay(&dd, "/dev/sdc", current_bad_cluster->id, 1)) {
				printf("%s", dd.error_msg);
				save_index(&dd);
				close_overlay(&dd);
				close_ntfs(&dd);
				cleanup_dd(&dd);
				exit(0);
			}
//		}
	}

	save_index(&dd);

	close_overlay(&dd);

	close_ntfs(&dd);


	cleanup_dd(&dd);

	exit(0);




	if (read_mft(&dd, &mft_record_handler)) {
		printf("%s", dd.error_msg);

		close_ntfs(&dd);

		return 1;
	}

	close_ntfs(&dd);

	cleanup_dd(&dd);

	return 0;

	struct reader_ctx_struct reader;

	if (init_reader(&reader, "/dev/sdc", -1, -1)) {
		printf("%s", reader.error_msg);

		cleanup_reader(&reader);

		return 1;
	}

	uint32_t max_lba;
	uint32_t block_size;

	if (get_capacity(&reader, &max_lba, &block_size)) {
		printf("%s", reader.error_msg);

		cleanup_reader(&reader);

		return 1;
	}

	printf("Max LBA: %d, block size: %d\n", max_lba, block_size);


	int ret = read_blocks(&reader, 20000000, 100);

	bail_on_bad_status("read_blocks", ret, &reader);

	for (int i = 0; i < 512 * 100; i++) {
		printf("%c", reader.buf[i]);
	}

	return 0;
}
