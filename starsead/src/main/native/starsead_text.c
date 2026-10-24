// Copyright 2026, Asterisk4Magisk contributors
// SPDX-License-Identifier: GPL-3.0

#include "starsead.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Text writer for rendered child documents, bounded by STARSEAD_MAX_JSON_SIZE. */

static int text_writer_reserve(struct starsead_text_writer *writer, size_t additional) {
    if (writer->result != 0) return writer->result;
    if (additional > STARSEAD_MAX_JSON_SIZE ||
        writer->length > STARSEAD_MAX_JSON_SIZE - additional) {
        writer->result = STARSEAD_CONFIG_INVALID;
        return writer->result;
    }
    size_t needed = writer->length + additional + 1U;
    if (needed <= writer->capacity) return 0;
    size_t capacity = writer->capacity == 0U ? 256U : writer->capacity;
    while (capacity < needed) {
        if (capacity > (STARSEAD_MAX_JSON_SIZE + 1U) / 2U) {
            capacity = STARSEAD_MAX_JSON_SIZE + 1U;
            break;
        }
        capacity *= 2U;
    }
    unsigned char *bytes = realloc(writer->bytes, capacity);
    if (bytes == NULL) {
        writer->result = STARSEAD_CONFIG_NO_MEMORY;
        return writer->result;
    }
    writer->bytes = bytes;
    writer->capacity = capacity;
    return 0;
}

void starsead_text_writer_bytes(
    struct starsead_text_writer *writer, const char *bytes, size_t length) {
    if (bytes == NULL || text_writer_reserve(writer, length) != 0) return;
    memcpy(writer->bytes + writer->length, bytes, length);
    writer->length += length;
    writer->bytes[writer->length] = '\0';
}

void starsead_text_writer_literal(struct starsead_text_writer *writer, const char *literal) {
    starsead_text_writer_bytes(writer, literal, strlen(literal));
}

void starsead_text_writer_format(struct starsead_text_writer *writer, const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    va_list copy;
    va_copy(copy, arguments);
    int count = vsnprintf(NULL, 0U, format, copy);
    va_end(copy);
    if (count < 0 || text_writer_reserve(writer, (size_t)count) != 0) {
        va_end(arguments);
        if (count < 0) writer->result = STARSEAD_CONFIG_INVALID;
        return;
    }
    int written = vsnprintf((char *)writer->bytes + writer->length,
        writer->capacity - writer->length, format, arguments);
    va_end(arguments);
    if (written != count) {
        writer->result = STARSEAD_CONFIG_INVALID;
        return;
    }
    writer->length += (size_t)count;
}

void starsead_text_writer_json_string(struct starsead_text_writer *writer, const char *value) {
    starsead_text_writer_literal(writer, "\"");
    const unsigned char *cursor = (const unsigned char *)value;
    while (*cursor != '\0' && writer->result == 0) {
        unsigned char byte = *cursor++;
        switch (byte) {
            case '\"': starsead_text_writer_literal(writer, "\\\""); break;
            case '\\': starsead_text_writer_literal(writer, "\\\\"); break;
            case '\b': starsead_text_writer_literal(writer, "\\b"); break;
            case '\f': starsead_text_writer_literal(writer, "\\f"); break;
            case '\n': starsead_text_writer_literal(writer, "\\n"); break;
            case '\r': starsead_text_writer_literal(writer, "\\r"); break;
            case '\t': starsead_text_writer_literal(writer, "\\t"); break;
            default:
                if (byte < 0x20U) starsead_text_writer_format(writer, "\\u%04x", (unsigned)byte);
                else starsead_text_writer_bytes(writer, (const char *)&byte, 1U);
                break;
        }
    }
    starsead_text_writer_literal(writer, "\"");
}

void starsead_text_writer_yaml_string(struct starsead_text_writer *writer, const char *value) {
    starsead_text_writer_literal(writer, "'");
    for (const char *cursor = value; *cursor != '\0'; ++cursor) {
        starsead_text_writer_bytes(writer, cursor, 1U);
        if (*cursor == '\'') starsead_text_writer_literal(writer, "'");
    }
    starsead_text_writer_literal(writer, "'");
}

int starsead_text_writer_take(
    struct starsead_text_writer *writer, struct starsead_anonymous_document *document) {
    if (writer->result != 0) {
        int result = writer->result;
        free(writer->bytes);
        memset(writer, 0, sizeof(*writer));
        return result;
    }
    if (writer->bytes == NULL) {
        writer->bytes = calloc(1U, 1U);
        if (writer->bytes == NULL) return STARSEAD_CONFIG_NO_MEMORY;
    }
    document->bytes = writer->bytes;
    document->length = writer->length;
    memset(writer, 0, sizeof(*writer));
    return 0;
}

/* Growable byte buffer for rule batch scripts, bounded by STARSEAD_BYTE_BUFFER_MAX_BYTES. */

void starsead_byte_buffer_destroy(struct starsead_byte_buffer *buffer) {
    if (buffer == NULL) return;
    free(buffer->bytes);
    memset(buffer, 0, sizeof(*buffer));
}

int starsead_byte_buffer_append(
    struct starsead_byte_buffer *buffer, const void *bytes, size_t length) {
    if (buffer == NULL || (length != 0U && bytes == NULL) ||
        length > STARSEAD_BYTE_BUFFER_MAX_BYTES - buffer->length) return -1;
    size_t required = buffer->length + length;
    if (required > buffer->capacity) {
        size_t capacity = buffer->capacity == 0U ? 4096U : buffer->capacity;
        while (capacity < required) {
            if (capacity > STARSEAD_BYTE_BUFFER_MAX_BYTES / 2U) {
                capacity = STARSEAD_BYTE_BUFFER_MAX_BYTES;
                break;
            }
            capacity *= 2U;
        }
        unsigned char *resized = realloc(buffer->bytes, capacity);
        if (resized == NULL) return -1;
        buffer->bytes = resized;
        buffer->capacity = capacity;
    }
    if (length != 0U) memcpy(buffer->bytes + buffer->length, bytes, length);
    buffer->length = required;
    return 0;
}
