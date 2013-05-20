/***************************************************************************
 * Copyright (C) 2007-2010 by Robert G. Jakabosky <bobby@neoawareness.com> *
 *                                                                         *
 ***************************************************************************/
#if !defined(__HM_ARRAY_H__)
#define __HM_ARRAY_H__

#include "lcommon.h"

typedef struct HMArray HMArray;

struct HMArray {
	uint16_t count;
	uint16_t capacity;
} L_GNUC_PACKED;

#define hm_array_from_void(ary_p) (((HMArray *)(ary_p)) - 1)

#define hm_array_to_void(ary) ((void *)((ary) + 1))

L_LIB_API void *hm_array_resize_internal(void *ary_p, size_t elem_size, size_t capacity);

L_LIB_API void hm_array_free_internal(void *ary_p);

#define hm_array_capacity(ary_p) \
	(hm_array_from_void(ary_p)->capacity)

#define hm_array_count(ary_p) \
	(hm_array_from_void(ary_p)->count)

#define hm_array_set_count(ary_p, _count) \
	(hm_array_from_void(ary_p)->count) = (_count)

#define hm_array_resize(ary_p, capacity) \
	(ary_p) = (typeof(ary_p)) hm_array_resize_internal(ary_p, sizeof(ary_p[0]), capacity)

#define hm_array_new(ary_p, capacity) \
	(ary_p) = (typeof(ary_p)) hm_array_resize_internal(NULL, sizeof(ary_p[0]), capacity)

#define hm_array_free(ary_p) do { \
	hm_array_free_internal(ary_p); (ary_p) = NULL; \
} while(0)

#endif /* __HM_ARRAY_H__ */
