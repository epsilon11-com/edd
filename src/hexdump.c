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

#include "hexdump.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void hexdump(const void* input, int length)
{
	// address is 8 bytes

	// each line is 16 bytes

	// ASCII is 16 bytes

	int address_len = 8;
	int bytes_per_line = 16;

	// [TODO] Ensure bytes_per_line will fit within line_len
	int line_len = 79;

	int num_lines = length / bytes_per_line;

	if (length % bytes_per_line) {
		num_lines++;
	}

	char* address = (char*)malloc(address_len + 1);
	char* hex_bytes = (char*)malloc(bytes_per_line * 3 + 1);
	char* ascii_bytes = (char*)malloc(bytes_per_line + 1);

	int address_format_len = (address_len / 10 + 1) + 3;

	char* address_format = (char*)malloc(address_format_len + 1);

	snprintf(address_format, address_format_len + 1, "%%0%dX", address_len);

	char* line = (char*)malloc(line_len + 1);

	for (int line_num = 0; line_num < num_lines; line_num++) {

		int bytes_to_write = bytes_per_line;

		if ((line_num == num_lines - 1) && (length % bytes_per_line)) {
			bytes_to_write = length % bytes_per_line;
		}

		memset(line, ' ', line_len);
		line[line_len] = '\0';

		for (int byte_pos = 0; byte_pos < bytes_to_write; byte_pos++) {
			unsigned char ch = ((unsigned char*)input)[line_num * bytes_per_line + byte_pos];
			char byte_buf[3];

			snprintf(byte_buf, 3, "%02X", ch);
			memcpy(line + address_len + 2 + byte_pos * 3, byte_buf, 2);

			if (isprint(ch)) {
				line[address_len + 2 + bytes_per_line * 3 + 1 + byte_pos] = ch;
			} else {
				line[address_len + 2 + bytes_per_line * 3 + 1 + byte_pos] = '.';
			}
		}

		// If outputting the last line, and it isn't full, fill the remaining
		// space.

		if ((line_num == num_lines - 1) && (length % bytes_per_line)) {

		}

		snprintf(address, address_len + 1, address_format, line_num * bytes_per_line);
		memcpy(line, address, address_len);

		// Output line.

		printf("%s\n", line);
	}

	free(address_format);

	free(address);
	free(hex_bytes);
	free(ascii_bytes);

	free(line);
}
