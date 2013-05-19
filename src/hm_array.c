/***************************************************************************
 * Copyright (C) 2007-2010 by Robert G. Jakabosky <bobby@neoawareness.com> *
 *                                                                         *
 ***************************************************************************/

#include "hm_array.h"
#include <stdlib.h>
#include <string.h>

void *hm_array_resize_internal(void *ary_p, size_t elem_size, size_t capacity) {
	HMArray *ary = NULL;

	if(ary_p) {
		ary = hm_array_from_void(ary_p);
	}
	/* if new capacity is zero just free the array. */
	if(capacity == 0) {
		free(ary);
		return NULL;
	}

	/* resize old array or allocate new array. */
	ary = (HMArray *)realloc(ary, (sizeof(HMArray) + (elem_size * capacity)));
	if(ary != NULL) {
		ary->capacity = capacity;
		return hm_array_to_void(ary);
	}
	/* failed to resize/allocate array. */
	return NULL;
}

void hm_array_free(void *ary_p) {
	HMArray *ary;
	if(ary_p) {
		ary = hm_array_from_void(ary_p);
		ary->capacity = 0xDEAD;
		free(ary);
	}
}

