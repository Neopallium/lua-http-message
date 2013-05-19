/***************************************************************************
 * Copyright (C) 2007-2010 by Robert G. Jakabosky <bobby@neoawareness.com> *
 *                                                                         *
 ***************************************************************************/

#include "hm_buffer.h"
#include <stdlib.h>
#include <string.h>

HMBuffer *hm_buffer_resize(HMBuffer *buf, size_t capacity) {
	size_t new_cap;

	/* if new capacity is zero just free the buffer. */
	if(capacity == 0) {
		free(buf);
		return NULL;
	}

	new_cap = (sizeof(HMBuffer) + (sizeof(uint8_t) * capacity));
	/* resize old buffer or allocate new buffer. */
	buf = (HMBuffer *)realloc(buf, new_cap);
	if(buf != NULL) {
		buf->capacity = capacity;
	}
	return buf;
}

void hm_buffer_free(HMBuffer *buf) {
	if(buf) {
		buf->capacity = 0xDEADBEEF;
		free(buf);
	}
}

