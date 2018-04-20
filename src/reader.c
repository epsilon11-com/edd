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

#include "reader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <limits.h>

#include <errno.h>

#include <fcntl.h>
#include <sys/ioctl.h>

#include <unistd.h>

#include <linux/hdreg.h>

#include <scsi/sg.h>


#define ERR(...) \
	reader->error = 1; \
	snprintf(reader->error_msg, 4096, __VA_ARGS__);


int init_reader(reader_ctx* reader, const char* dev, unsigned char sensebuf_len, unsigned int buf_len)
{
	memset(reader, 0, sizeof(reader_ctx));

	reader->dev = (char*)malloc(strlen(dev) + 1);
	strcpy(reader->dev, dev);

	if (sensebuf_len != -1) {
		reader->sensebuf_len = sensebuf_len;
	} else {
		reader->sensebuf_len = 100;
	}

	if (buf_len != -1) {
		reader->buf_len = buf_len;
	} else {
		reader->buf_len = 65535;
	}

	reader->sensebuf = (char*)malloc(reader->sensebuf_len);
	reader->buf = (char*)malloc(reader->buf_len);


	reader->fd = open(reader->dev, O_RDONLY);

	if (reader->fd == -1) {
		ERR("Unable to open %s: %s\n", reader->dev, strerror(errno));

		return 1;
	}


	return 0;
}

void cleanup_reader(reader_ctx* reader)
{
	if (reader->dev != NULL) {
		free(reader->dev);

		reader->dev = NULL;
	}

	if (reader->sensebuf != NULL) {
		free(reader->sensebuf);

		reader->sensebuf = NULL;
	}

	if (reader->buf != NULL) {
		free(reader->buf);

		reader->buf = NULL;
	}

	if (reader->fd != -1) {
		close(reader->fd);

		reader->fd = -1;
	}
}



int get_capacity(reader_ctx* reader, uint32_t *max_lba, uint32_t *block_size)
{
	struct sg_io_hdr hdr;

	char cmd[9];

	cdb_read_capacity_10(cmd);

	memset(&hdr, 0, sizeof(sg_io_hdr_t));
	memset(reader->sensebuf, 0, reader->sensebuf_len);
	memset(reader->buf, 0, reader->buf_len);

	hdr.interface_id = 'S';
	hdr.dxfer_direction = SG_DXFER_FROM_DEV;
	hdr.cmd_len = sizeof(cmd);
	hdr.mx_sb_len = reader->sensebuf_len;
	hdr.iovec_count = 0;
	hdr.dxfer_len = reader->buf_len;
	hdr.dxferp = reader->buf;
	hdr.cmdp = cmd;
	hdr.sbp = reader->sensebuf;
	hdr.timeout = 5000;
//	hdr.flags = 0;
//	hdr.pack_id = 0;
//	hdr.usr_ptr = 0;

	int ret = 0;
	ret = ioctl(reader->fd, SG_IO, &hdr);

	if (ret == 0) {
		scsi_parse_sense(reader->sensebuf, hdr.sb_len_wr, &reader->senseinfo);

		if (reader->senseinfo.sense_key == 0) {
			parse_read_capacity_10(reader->buf, reader->buf_len, max_lba, block_size);
		}
	}

	return ret;
}

int read_blocks(reader_ctx* reader, uint64_t lba, uint16_t len) {

	struct sg_io_hdr hdr;

	char cmd[16];

	//cdb_read_10(cmd, true, lba, len);
	cdb_read_16(cmd, 0, 0, 0, lba, len);

	memset(&hdr, 0, sizeof(sg_io_hdr_t));
	memset(reader->sensebuf, 0, reader->sensebuf_len);
	memset(reader->buf, 0, reader->buf_len);

	hdr.interface_id = 'S';
	hdr.dxfer_direction = SG_DXFER_FROM_DEV;
	hdr.cmd_len = sizeof(cmd);
	hdr.mx_sb_len = reader->sensebuf_len;
	hdr.iovec_count = 0;
	hdr.dxfer_len = reader->buf_len;
	hdr.dxferp = reader->buf;
	hdr.cmdp = cmd;
	hdr.sbp = reader->sensebuf;
	hdr.timeout = 500000;
//	hdr.flags = 0;
//	hdr.pack_id = 0;
//	hdr.usr_ptr = 0;

	int ret = 0;
	ret = ioctl(reader->fd, SG_IO, &hdr);

	if (ret == 0) {
		scsi_parse_sense(reader->sensebuf, hdr.sb_len_wr, &reader->senseinfo);
	}

	return ret;

}

