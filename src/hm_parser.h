/***************************************************************************
 * Copyright (C) 2007-2010 by Robert G. Jakabosky <bobby@neoawareness.com> *
 *                                                                         *
 ***************************************************************************/
#ifndef __HM_PARSER_H__
#define __HM_PARSER_H__

#include <stddef.h>
#include <stdint.h>

#include "lcommon.h"
#define L_LIB_API extern
#define L_INLINE static inline

typedef struct HMParser HMParser;

/**
 * Create HTTP Response message.
 *
 * @return message pointer to new HMParser.
 * @public @memberof HMParser
 */
L_LIB_API HMParser *hm_parser_new_response();

/**
 * Create HTTP Request message.
 *
 * @return message pointer to new HMParser.
 * @public @memberof HMParser
 */
L_LIB_API HMParser *hm_parser_new_request();

/**
 * Free instance of HMParser.
 *
 * @param hm_parser pointer to HMParser instance to free
 * @public @memberof HMParser
 */
L_LIB_API void hm_parser_free(HMParser *hm_parser);

/**
 * Reset the parser & message state.
 *
 * @param hm_parser pointer to HMParser structure to be reset.
 * @public @memberof HMParser
 */
L_LIB_API void hm_parser_reset(HMParser *hm_parser);

/**
 * Append data to buffer.
 *
 * @param hm_parser pointer to HMParser structure.
 * @param data data to append.
 * @param len number of bytes to append.
 * @public @memberof HMParser
 */
L_LIB_API void hm_parser_append_data(HMParser *hm_parser, const char *data, size_t len);

/**
 * Prepare buffer for appending more data.
 *
 * @param hm_parser pointer to HMParser structure.
 * @param len number of bytes we would like to append.
 * @return available space in buffer for more data.
 * @public @memberof HMParser
 */
L_LIB_API size_t hm_parser_prepare_buffer(HMParser *hm_parser, size_t len);

/**
 * Get buffer to append more raw HTTP data from network.
 *
 * @param hm_parser pointer to HMParser structure.
 * @public @memberof HMParser
 */
L_LIB_API uint8_t *hm_parser_get_buffer(HMParser *hm_parser);

/**
 * Returns the amount of free space available in the buffer.
 *
 * @param hm_parser pointer to HMParser structure.
 * @public @memberof HMParser
 */
L_LIB_API size_t hm_parser_get_buffer_capacity(HMParser *hm_parser);

/**
 * Mark how many bytes have been written into the parse buffer.
 *
 * @param hm_parser pointer to HMParser structure.
 * @param len number of bytes that where written into the buffer.
 * @public @memberof HMParser
 */
L_LIB_API bool hm_parser_append_buffer_bytes(HMParser *hm_parser, size_t len);

/**
 * Parse new data that has been written into the buffer.
 *
 * @param hm_parser pointer to HMParser structure.
 * @param is_eof true if there is no more data.
 * @public @memberof HMParser
 */
L_LIB_API uint32_t hm_parser_parse(HMParser *hm_parser, bool is_eof);

/**
 * methods to access HTTP headers.
 */

/**
 * methods to access info from http_parser.
 */

L_LIB_API int hm_parser_should_keep_alive(HMParser *hm_parser);

L_LIB_API int hm_parser_is_upgrade(HMParser *hm_parser);

L_LIB_API int hm_parser_method(HMParser *hm_parser);

L_LIB_API const char *hm_parser_method_str(HMParser *hm_parser);

L_LIB_API int hm_parser_version(HMParser *hm_parser);

L_LIB_API int hm_parser_status_code(HMParser *hm_parser);

L_LIB_API int hm_parser_is_error(HMParser *hm_parser);

L_LIB_API int hm_parser_error(HMParser *hm_parser);

L_LIB_API const char *hm_parser_error_name(HMParser *hm_parser);

L_LIB_API const char *hm_parser_error_description(HMParser *hm_parser);

#endif /* __HM_PARSER_H__ */
