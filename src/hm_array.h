/***************************************************************************
 * Copyright (C) 2007-2010 by Robert G. Jakabosky <bobby@neoawareness.com> *
 *                                                                         *
 ***************************************************************************/
#if !defined(__HM_ARRAY_H__)
#define __HM_ARRAY_H__

#include "lcommon.h"

typedef struct HMArray HMArray;

struct HMArray {
	uint16_t capacity;
} L_GNUC_PACKED;

#define hm_array_from_void(ary_p) (((HMArray *)(ary_p)) - 1)

#define hm_array_to_void(ary) ((void *)((ary) + 1))

L_LIB_API void *hm_array_resize_internal(void *ary_p, size_t elem_size, size_t capacity);

#define hm_array_capacity(ary_p) \
	(hm_array_from_void(ary_p)->capacity)

#define hm_array_resize(_type, ary_p, capacity) \
	(_type *)hm_array_resize_internal(ary_p, sizeof(_type), capacity)

#define hm_array_new(_type, capacity) \
	(_type *)hm_array_resize_internal(NULL, sizeof(_type), capacity)

L_LIB_API void hm_array_free(void *ary_p);

#endif /* __HM_ARRAY_H__ */
