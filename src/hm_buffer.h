/***************************************************************************
 * Copyright (C) 2007-2010 by Robert G. Jakabosky <bobby@neoawareness.com> *
 *                                                                         *
 ***************************************************************************/
#if !defined(__HM_BUFFER_H__)
#define __HM_BUFFER_H__

#include "lcommon.h"

typedef struct HMBuffer HMBuffer;

struct HMBuffer {
	size_t  capacity; /**< how many byte the `data` buffer can hold. */
	uint8_t data[];   /**< Memory for the buffer is allocated after the 'HMBuffer' structure. */
};

#define hm_buffer_data(buf) ((buf)->data)

#define hm_buffer_capacity(buf) ((buf)->capacity)

L_LIB_API HMBuffer *hm_buffer_resize(HMBuffer *buf, size_t capacity);

#define hm_buffer_new(capacity) hm_buffer_resize(NULL, capacity)

L_LIB_API void hm_buffer_free(HMBuffer *buf);

#endif /* __HM_BUFFER_H__ */
