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

#include "mapfile.h"

#include <unistd.h>

#include <errno.h>

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <fcntl.h>

#include <ctype.h>

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

#define LINE_ERR(...) \
	snprintf(dd->line_error_msg, 4096, __VA_ARGS__);

#define LINE_ERR_MSG dd->line_error_msg

#define ERR(...) \
	if (dd->error == 0) { \
		memset(dd->error_msg, 0, 4096); \
		dd->error = 1; \
	} \
	dd->error = 1; \
	snprintf(dd->error_msg + strlen(dd->error_msg), 4096 - strlen(dd->error_msg), __VA_ARGS__);

int _is_comment(const char* line)
{
	int i;

	for (i = 0; i < strlen(line); i++) {
		if (line[i] == '#') {
			return 1;
		}

		if (!isspace(line[i])) {
			return 0;
		}
	}

	return 0;
}

int _parse_header(dd_ctx* dd, const char* line)
{
	int i;

	int start_pos = -1;
	int param_num = 0;

	char param[3][21];
//	char** param;

//	printf("%x\n", param);
//
//	_get_params(line, &param, 3, 20);
//	param_num = 3;
//
//	printf("%s, %s, %s\n", param[0], param[1], param[2]);

	for (i = 0; i < strlen(line); i++) {
		if (isspace(line[i])) {
			if (start_pos != -1) {
				if (i - start_pos > 20) {
					// [TODO] Error, unexpectedly long parameter
				}

				if (param_num == 3) {
					// [TODO] Error, too many parameters
				}

				strncpy(param[param_num], line + start_pos, i - start_pos);

				param[param_num++][i - start_pos] = '\0';

				start_pos = -1;
			}
			continue;
		}

		if (start_pos == -1) {
			start_pos = i;
		}
	}

	// Validate parameters.

	// Require exactly 3 parameters be present.

	if (param_num != 3) {
		ERR("Invalid number of parameters\n");

		return 1;
	}

	// Build regex to test for hexadecimal strings.

	pcre2_code *re_hex;

	int errorcode;
	size_t erroroffset;

	re_hex = pcre2_compile("^0x[0-9a-f]+$", PCRE2_ZERO_TERMINATED, PCRE2_CASELESS, &errorcode, &erroroffset, NULL);

	pcre2_match_data* match_data;

	match_data = pcre2_match_data_create_from_pattern(re_hex, NULL);

	int match;

	// Require hexadecimal string for first parameter.

	match = pcre2_match(re_hex, param[0], strlen(param[0]), 0, 0, match_data, NULL);

	if (match <= 0) {
		LINE_ERR("First parameter '%s' not hexadecimal\n", param[0]);

		return 1;
	}

	// Require second parameter to contain exactly one character.

	if (strlen(param[1]) != 1) {
		LINE_ERR("Second parameter '%s' not exactly one character long\n", param[2]);

		return 1;
	}

	// Require third parameter to be a digit.

	if (strlen(param[2]) != 1 || !isdigit(param[2][0])) {
		LINE_ERR("Third parameter '%s' not single digit\n", param[2]);

		return 1;
	}

	// Clean up compiled regex.

	pcre2_code_free(re_hex);

	// Convert parameters to values.

	dd->current_pos = strtoll(param[0], NULL, 0);
	dd->current_status = param[1][0];
	dd->pass = strtoll(param[2], NULL, 0);

	return 0;
}

int _parse_line(dd_ctx* dd, const char* line, __uint64_t* pos, __uint64_t* size, char* status)
{
	int i;

	int start_pos = -1;
	int param_num = 0;

	char param[3][21];

	for (i = 0; i < strlen(line); i++) {
		if (isspace(line[i])) {
			if (start_pos != -1) {
				if (i - start_pos > 20) {
					// [TODO] Error, unexpectedly long parameter
				}

				if (param_num == 3) {
					// [TODO] Error, too many parameters
				}

				strncpy(param[param_num], line + start_pos, i - start_pos);

				param[param_num++][i - start_pos] = '\0';

				start_pos = -1;
			}
			continue;
		}

		if (start_pos == -1) {
			start_pos = i;
		}
	}

	// Validate parameters.

	// Require exactly 3 parameters be present.

	if (param_num != 3) {
		ERR("Invalid number of parameters\n");

		return 1;
	}

	// Build regex to test for hexadecimal strings.

	pcre2_code *re_hex;

	int errorcode;
	size_t erroroffset;

	re_hex = pcre2_compile("^0x[0-9a-f]+$", PCRE2_ZERO_TERMINATED, PCRE2_CASELESS, &errorcode, &erroroffset, NULL);

	pcre2_match_data* match_data;

	match_data = pcre2_match_data_create_from_pattern(re_hex, NULL);

	int match;

	// Require hexadecimal string for first parameter.

	match = pcre2_match(re_hex, param[0], strlen(param[0]), 0, 0, match_data, NULL);

	if (match <= 0) {
		LINE_ERR("First parameter '%s' not hexadecimal\n", param[0]);

		return 1;
	}

	// Require hexadecimal string for second parameter.

	match = pcre2_match(re_hex, param[1], strlen(param[1]), 0, 0, match_data, NULL);

	if (match <= 0) {
		LINE_ERR("Second parameter '%s' not hexadecimal\n", param[1]);

		return 1;
	}

	// Require third string to contain exactly one character.

	if (strlen(param[2]) != 1) {
		LINE_ERR("Third parameter '%s' not exactly one character long\n", param[2]);

		return 1;
	}

	// Clean up compiled regex.

	pcre2_code_free(re_hex);

	// Convert parameters to values and return them to caller.

	*pos = strtoll(param[0], NULL, 0);
	*size = strtoll(param[1], NULL, 0);
	*status = param[2][0];

	return 0;
}

int read_mapfile(dd_ctx* dd, const char* filename)
{
	FILE* fil;

	fil = fopen(filename, "r");

	if (fil == NULL) {
		ERR("Unable to open %s: %s\n", filename, strerror(errno));

		return 1;
	}

	char line[8192];

	int line_num = 0;

	int state = 0;

	while (fgets(line, 8192, fil) != NULL) {
		line_num++;

		if (_is_comment(line)) {
			continue;
		}

		__uint64_t pos;
		__uint64_t size;
		char status;

		if (state == 0) {
			if (_parse_header(dd, line)) {
				ERR("Parsing %s failed at line %d: %s\n", filename, line_num, LINE_ERR_MSG);

				fclose(fil);

				return 1;
			}

			state = 1;
		} else {
			if (_parse_line(dd, line, &pos, &size, &status)) {
				ERR("Parsing %s failed at line %d: %s\n", filename, line_num, LINE_ERR_MSG);

				fclose(fil);

				return 1;
			}

			if (status == '+') {
				ADD_SAFE_REGION(pos, size);
			}
		}
	}

	fclose(fil);

	return 0;
}

