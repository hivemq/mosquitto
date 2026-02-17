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

#ifndef PULSE_DATA_TYPE_DETECTOR_H
#define PULSE_DATA_TYPE_DETECTOR_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Data type identifiers */
#define PULSE_DT_BYTES    "hivemq.datatypes.generic.bytes"
#define PULSE_DT_INTEGER  "hivemq.datatypes.generic.integer"
#define PULSE_DT_DECIMAL  "hivemq.datatypes.generic.decimal"
#define PULSE_DT_BOOLEAN  "hivemq.datatypes.generic.boolean"
#define PULSE_DT_STRING   "hivemq.datatypes.generic.string"
#define PULSE_DT_OBJECT   "hivemq.datatypes.generic.object"

/* Serialization format identifiers */
#define PULSE_FMT_BYTES       "hivemq.serializations.generic.bytes"
#define PULSE_FMT_JSON_OBJ    "hivemq.serializations.generic.json.object"
#define PULSE_FMT_JSON_ARR    "hivemq.serializations.generic.json.array"
#define PULSE_FMT_UTF8        "hivemq.serializations.generic.utf8"
#define PULSE_FMT_ISO88591    "hivemq.serializations.generic.iso88591"
#define PULSE_FMT_FLOAT       "hivemq.serializations.generic.float"
#define PULSE_FMT_DOUBLE      "hivemq.serializations.generic.double"
#define PULSE_FMT_INT8        "hivemq.serializations.generic.int8"
#define PULSE_FMT_INT16       "hivemq.serializations.generic.int16"
#define PULSE_FMT_INT32       "hivemq.serializations.generic.int32"
#define PULSE_FMT_INT64       "hivemq.serializations.generic.int64"

/**
 * A single data type suggestion.
 */
typedef struct {
	const char *data_type_id;        /* Data type identifier (constant string) */
	const char *format_id;           /* Serialization format identifier (constant string) */
	char *deserialized_data;         /* String representation (heap-allocated, can be NULL) */
} pulse_dts_t;

/**
 * A list of data type suggestions.
 * Fixed-size array to avoid dynamic allocation.
 */
typedef struct {
	pulse_dts_t items[16];           /* Fixed array (typical max ~10 suggestions) */
	int count;                       /* Number of valid suggestions in items */
} pulse_dts_list_t;

/**
 * Detect data types from a payload.
 * 
 * This is a stateless function that analyzes a payload and returns
 * a list of possible data type interpretations, ranked by likelihood.
 * 
 * @param payload The payload data to analyze
 * @param len Length of the payload in bytes
 * @param include_data If true, include deserialized string representations in results
 * @return A list of data type suggestions (caller must free with pulse_free_dts_list)
 */
pulse_dts_list_t pulse_detect_data_type(const uint8_t *payload, size_t len, bool include_data);

/**
 * Free memory allocated for a data type suggestion list.
 * 
 * Frees all deserialized_data strings in the list.
 * 
 * @param list The list to free
 */
void pulse_free_dts_list(pulse_dts_list_t *list);

#ifdef __cplusplus
}
#endif

#endif /* PULSE_DATA_TYPE_DETECTOR_H */
