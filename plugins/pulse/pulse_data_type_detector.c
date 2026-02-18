/*
Copyright (c) 2024 Roger Light <roger@atchoo.org>

All rights reserved. This program and the accompanying materials
are made available under the terms of the Eclipse Public License 2.0
and Eclipse Distribution License v1.0 which accompany this distribution.

The Eclipse Public License is available at
   https://www.eclipse.org/legal/epl-2.0/
and the Eclipse Distribution License is available at
  http://www.eclipse.org/org/documents/edl-v10.php.

SPDX-License-Identifier: EPL-2.0 OR BSD-3-Clause
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <errno.h>
#include <limits.h>

#include <cjson/cJSON.h>

#include "pulse_data_type_detector.h"

/* Helper to add a suggestion to the list */
static void add_suggestion(pulse_dts_list_t *list, const char *data_type_id, 
                          const char *format_id, char *deserialized_data)
{
	if (list->count >= 16) {
		free(deserialized_data);
		return;
	}
	
	list->items[list->count].data_type_id = data_type_id;
	list->items[list->count].format_id = format_id;
	list->items[list->count].deserialized_data = deserialized_data;
	list->count++;
}

/* Binary decoding functions (little-endian) */

static int8_t decode_int8(const uint8_t *p)
{
	return (int8_t)p[0];
}

static int16_t decode_int16_le(const uint8_t *p)
{
	return (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static int32_t decode_int32_le(const uint8_t *p)
{
	return (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) |
	                 ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
}

static int64_t decode_int64_le(const uint8_t *p)
{
	return (int64_t)((uint64_t)p[0] | ((uint64_t)p[1] << 8) |
	                 ((uint64_t)p[2] << 16) | ((uint64_t)p[3] << 24) |
	                 ((uint64_t)p[4] << 32) | ((uint64_t)p[5] << 40) |
	                 ((uint64_t)p[6] << 48) | ((uint64_t)p[7] << 56));
}

static float decode_float_le(const uint8_t *p)
{
	uint32_t bits = (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
	                ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
	float val;
	memcpy(&val, &bits, sizeof(float));
	return val;
}

static double decode_double_le(const uint8_t *p)
{
	uint64_t bits = (uint64_t)p[0] | ((uint64_t)p[1] << 8) |
	                ((uint64_t)p[2] << 16) | ((uint64_t)p[3] << 24) |
	                ((uint64_t)p[4] << 32) | ((uint64_t)p[5] << 40) |
	                ((uint64_t)p[6] << 48) | ((uint64_t)p[7] << 56);
	double val;
	memcpy(&val, &bits, sizeof(double));
	return val;
}

/* Plausibility checks */

static bool is_plausible_string(const uint8_t *data, size_t len)
{
	if (len == 0) return false;
	
	bool has_alnum_or_space = false;
	
	for (size_t i = 0; i < len; i++) {
		uint8_t c = data[i];
		
		/* Check for control characters (except tab, newline, CR) */
		if (c < 0x20 && c != '\t' && c != '\n' && c != '\r') {
			return false;
		}
		
		/* Check for alphanumeric, space, or high bytes (potential UTF-8) */
		if (isalnum(c) || c == ' ' || c >= 0x80) {
			has_alnum_or_space = true;
		}
	}
	
	return has_alnum_or_space;
}

static bool is_plausible_float(const uint8_t *data)
{
	float value = decode_float_le(data);
	
	if (isnan(value) || isinf(value)) {
		return false;
	}
	
	if (value == 0.0f) {
		return true;
	}
	
	double abs_val = fabs((double)value);
	if (abs_val > 1e6) {
		return false;
	}
	
	int exponent = (int)floor(log10(abs_val));
	return abs(exponent) < 9;
}

static bool is_plausible_double(const uint8_t *data)
{
	double value = decode_double_le(data);
	
	if (isnan(value) || isinf(value)) {
		return false;
	}
	
	if (value == 0.0) {
		return true;
	}
	
	double abs_val = fabs(value);
	if (abs_val > 1e12) {
		return false;
	}
	
	int exponent = (int)floor(log10(abs_val));
	return abs(exponent) < 12;
}

/* String classification helpers */

static bool is_integer_string(const char *str)
{
	if (!str || !*str) return false;
	
	char *endptr;
	errno = 0;
	(void)strtoll(str, &endptr, 10);
	
	/* Check if entire string was consumed and no overflow */
	return errno == 0 && *endptr == '\0' && endptr != str;
}

static bool is_decimal_string(const char *str)
{
	if (!str || !*str) return false;
	
	char *endptr;
	errno = 0;
	double val = strtod(str, &endptr);
	
	/* Check if entire string was consumed, no overflow, and not NaN/Inf */
	return errno == 0 && *endptr == '\0' && endptr != str && 
	       !isnan(val) && !isinf(val);
}

/* JSON detection */

static bool try_parse_json(const uint8_t *data, size_t len, bool *is_array)
{
	if (len == 0) return false;
	
	/* Quick check: find first non-whitespace character */
	for (size_t i = 0; i < len; i++) {
		uint8_t c = data[i];
		if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
			continue;
		}
		
		/* Must start with { or [ */
		if (c != '{' && c != '[') {
			return false;
		}
		
		/* Try to parse as JSON */
		cJSON *json = cJSON_ParseWithLength((const char *)data, len);
		if (json) {
			if (is_array) {
				*is_array = cJSON_IsArray(json);
			}
			cJSON_Delete(json);
			return true;
		}
		
		return false;
	}
	
	return false;
}

/* String suggestion helpers */

static void append_string_suggestions(pulse_dts_list_t *result, 
                                      const uint8_t *payload, size_t len,
                                      bool include_data)
{
	/* Create a null-terminated copy for string operations */
	char *str = (char *)malloc(len + 1);
	if (!str) return;
	
	memcpy(str, payload, len);
	str[len] = '\0';
	
	/* Check if it's an integer string */
	if (is_integer_string(str)) {
		add_suggestion(result, PULSE_DT_INTEGER, PULSE_FMT_UTF8, 
		              include_data ? strdup(str) : NULL);
	}
	/* Check if it's a decimal string */
	else if (is_decimal_string(str)) {
		add_suggestion(result, PULSE_DT_DECIMAL, PULSE_FMT_UTF8,
		              include_data ? strdup(str) : NULL);
	}
	/* Otherwise it's a generic string */
	else {
		add_suggestion(result, PULSE_DT_STRING, PULSE_FMT_UTF8,
		              include_data ? strdup(str) : NULL);
	}
	
	/* Always add ISO-8859-1 variant */
	add_suggestion(result, PULSE_DT_STRING, PULSE_FMT_ISO88591,
	              include_data ? strdup(str) : NULL);
	
	free(str);
}

static void append_json_suggestion(pulse_dts_list_t *result,
                                  const uint8_t *payload, size_t len,
                                  bool include_data, bool is_array)
{
	const char *format = is_array ? PULSE_FMT_JSON_ARR : PULSE_FMT_JSON_OBJ;
	
	char *data = NULL;
	if (include_data) {
		data = (char *)malloc(len + 1);
		if (data) {
			memcpy(data, payload, len);
			data[len] = '\0';
		}
	}
	
	add_suggestion(result, PULSE_DT_OBJECT, format, data);
}

static char *format_byte_array(const uint8_t *payload, size_t len, bool include_data)
{
	if (!include_data || len == 0) return NULL;
	
	/* Format as hex string: "0x01 0x02 0x03" */
	/* Max display: first 32 bytes */
	size_t display_len = len > 32 ? 32 : len;
	size_t buf_size = display_len * 5 + 10; /* "0xXX " per byte + "..." */
	
	char *buf = (char *)malloc(buf_size);
	if (!buf) return NULL;
	
	char *p = buf;
	for (size_t i = 0; i < display_len; i++) {
		p += sprintf(p, "0x%02x", payload[i]);
		if (i < display_len - 1) {
			*p++ = ' ';
		}
	}
	
	if (len > display_len) {
		strcpy(p, " ...");
	}
	
	return buf;
}

/* Main detection function */

pulse_dts_list_t pulse_detect_data_type(const uint8_t *payload, size_t len, bool include_data)
{
	pulse_dts_list_t result;
	memset(&result, 0, sizeof(result));
	
	if (!payload) {
		/* Empty payload - only BYTES */
		add_suggestion(&result, PULSE_DT_BYTES, PULSE_FMT_BYTES, NULL);
		return result;
	}
	
	switch (len) {
	case 0:
		/* Empty payload - only BYTES */
		add_suggestion(&result, PULSE_DT_BYTES, PULSE_FMT_BYTES, NULL);
		break;
		
	case 1:
		/* 1 byte: string check, then INT8 */
		if (is_plausible_string(payload, len)) {
			append_string_suggestions(&result, payload, len, include_data);
		}
		
		{
			int8_t val = decode_int8(payload);
			char *data = NULL;
			if (include_data) {
				data = (char *)malloc(16);
				if (data) sprintf(data, "%d", val);
			}
			add_suggestion(&result, PULSE_DT_INTEGER, PULSE_FMT_INT8, data);
		}
		break;
		
	case 2:
		/* 2 bytes: string check, then INT16 */
		if (is_plausible_string(payload, len)) {
			append_string_suggestions(&result, payload, len, include_data);
		}
		
		{
			int16_t val = decode_int16_le(payload);
			char *data = NULL;
			if (include_data) {
				data = (char *)malloc(16);
				if (data) sprintf(data, "%d", val);
			}
			add_suggestion(&result, PULSE_DT_INTEGER, PULSE_FMT_INT16, data);
		}
		break;
		
	case 4:
		/* 4 bytes: check "true", then string, then INT32, then FLOAT */
		if (is_plausible_string(payload, len)) {
			if (memcmp(payload, "true", 4) == 0) {
				add_suggestion(&result, PULSE_DT_BOOLEAN, PULSE_FMT_UTF8,
				              include_data ? strdup("true") : NULL);
			} else {
				append_string_suggestions(&result, payload, len, include_data);
			}
		}
		
		{
			int32_t val = decode_int32_le(payload);
			char *data = NULL;
			if (include_data) {
				data = (char *)malloc(32);
				if (data) sprintf(data, "%d", val);
			}
			add_suggestion(&result, PULSE_DT_INTEGER, PULSE_FMT_INT32, data);
		}
		
		if (is_plausible_float(payload)) {
			float val = decode_float_le(payload);
			char *data = NULL;
			if (include_data) {
				data = (char *)malloc(32);
				if (data) sprintf(data, "%g", val);
			}
			add_suggestion(&result, PULSE_DT_DECIMAL, PULSE_FMT_FLOAT, data);
		}
		break;
		
	case 5:
		/* 5 bytes: check "false", then JSON, then string */
		if (is_plausible_string(payload, len)) {
			if (memcmp(payload, "false", 5) == 0) {
				add_suggestion(&result, PULSE_DT_BOOLEAN, PULSE_FMT_UTF8,
				              include_data ? strdup("false") : NULL);
			} else {
				append_string_suggestions(&result, payload, len, include_data);
			}
		}
		
		{
			bool is_array = false;
			if (try_parse_json(payload, len, &is_array)) {
				append_json_suggestion(&result, payload, len, include_data, is_array);
			}
		}
		break;
		
	case 8:
		/* 8 bytes: string, JSON, INT64, DOUBLE */
		if (is_plausible_string(payload, len)) {
			append_string_suggestions(&result, payload, len, include_data);
		}
		
		{
			bool is_array = false;
			if (try_parse_json(payload, len, &is_array)) {
				append_json_suggestion(&result, payload, len, include_data, is_array);
			}
		}
		
		{
			int64_t val = decode_int64_le(payload);
			char *data = NULL;
			if (include_data) {
				data = (char *)malloc(32);
				if (data) sprintf(data, "%lld", (long long)val);
			}
			add_suggestion(&result, PULSE_DT_INTEGER, PULSE_FMT_INT64, data);
		}
		
		if (is_plausible_double(payload)) {
			double val = decode_double_le(payload);
			char *data = NULL;
			if (include_data) {
				data = (char *)malloc(32);
				if (data) sprintf(data, "%g", val);
			}
			add_suggestion(&result, PULSE_DT_DECIMAL, PULSE_FMT_DOUBLE, data);
		}
		break;
		
	default:
		/* Other lengths: string check, then JSON check */
		if (is_plausible_string(payload, len)) {
			append_string_suggestions(&result, payload, len, include_data);
		}
		
		{
			bool is_array = false;
			if (try_parse_json(payload, len, &is_array)) {
				append_json_suggestion(&result, payload, len, include_data, is_array);
			}
		}
		break;
	}
	
	/* Always append BYTES as final fallback */
	add_suggestion(&result, PULSE_DT_BYTES, PULSE_FMT_BYTES,
	              format_byte_array(payload, len, include_data));
	
	return result;
}

void pulse_free_dts_list(pulse_dts_list_t *list)
{
	if (!list) return;
	
	for (int i = 0; i < list->count; i++) {
		free(list->items[i].deserialized_data);
		list->items[i].deserialized_data = NULL;
	}
	
	list->count = 0;
}
