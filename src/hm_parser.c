#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "hm_parser.h"

#include "hm_buffer.h"

#include "hm_array.h"

#include "http_tokenizer.h"

typedef uint32_t hm_len_t;

/**
 * HTTP message object.
 *
 * @ingroup Objects
 */
struct HMParser {
	http_tokenizer *tokenizer;
	hm_len_t      parsed_off;   /**< http parser offset. */
	hm_len_t      buf_len;      /**< number of bytes in buffer. */
	HMBuffer      *buf;         /**< buffer to hold raw http message. */
};

#define MIN_BUFFER_SPACE 1024

static void hm_parser_reset_internal(HMParser* hm_parser) {
	/* clear buffer state. */
	hm_parser->parsed_off = 0;
	hm_parser->buf_len = 0;
	/* reset http tokenizer */
	http_tokenizer_reset(hm_parser->tokenizer);
}

static HMParser *hm_parser_new(int is_request) {
	HMParser* hm_parser;

	hm_parser = (HMParser *)malloc(sizeof(HMParser));
	hm_parser->buf = hm_buffer_new(MIN_BUFFER_SPACE);
	/* create http tokenizer. */
	if(is_request) {
		hm_parser->tokenizer = http_tokenizer_new_request();
	} else {
		hm_parser->tokenizer = http_tokenizer_new_response();
	}
	/* clear buffer state. */
	hm_parser->parsed_off = 0;
	hm_parser->buf_len = 0;

	return hm_parser;
}

HMParser *hm_parser_new_response() {
	return hm_parser_new(0);
}

HMParser *hm_parser_new_request() {
	return hm_parser_new(1);
}

void hm_parser_reset(HMParser* hm_parser) {
	hm_parser_reset_internal(hm_parser);
}

void hm_parser_free(HMParser* hm_parser) {
	hm_parser_reset_internal(hm_parser);
	free(hm_parser);
}

size_t hm_parser_prepare_buffer(HMParser *hm_parser, size_t len) {
	HMBuffer *buf = hm_parser->buf;
	size_t cap = hm_buffer_capacity(buf);
	size_t buf_len = hm_parser->buf_len;
	size_t available;
	/* buffer length should never be larger then the current capacity. */
	assert(cap >= buf_len);
	available = cap - buf_len;
	/* check if we should try to grow the buffer. */
	if(available < len) {
		size_t new_cap = buf_len + len;
		/* check for overflow. */
		if(new_cap <= buf_len) {
			/* ignore bad request to grow buffer. */
			return 0; /* return zero to signal bad value. */
		}
		buf = hm_buffer_resize(buf, new_cap);
		if(buf) {
			/* update parser's buffer. */
			hm_parser->buf = buf;
			cap = hm_buffer_capacity(buf);
			available = cap - buf_len;
		} else {
			/* failed to grow buffer. */
			available = 0;
		}
	}
	return available;
}

uint8_t *hm_parser_get_buffer(HMParser *hm_parser) {
	uint8_t *data = hm_buffer_data(hm_parser->buf);
	data += hm_parser->buf_len;
	return data;
}

size_t hm_parser_get_buffer_capacity(HMParser *hm_parser) {
	size_t cap = hm_buffer_capacity(hm_parser->buf);
	cap -= hm_parser->buf_len;
	return cap;
}

void hm_parser_append_data(HMParser *hm_parser, const char *data, size_t len) {
	size_t space = hm_parser_prepare_buffer(hm_parser, len);
	assert(space < len);
	memcpy(hm_parser_get_buffer(hm_parser), data, len);
	hm_parser->buf_len += len;
}

bool hm_parser_append_buffer_bytes(HMParser *hm_parser, size_t len) {
	size_t cap = hm_buffer_capacity(hm_parser->buf);
	size_t buf_len = hm_parser->buf_len;
	size_t new_len = buf_len + len;
	/* check for integer/capacity overflow. */
	if(new_len < buf_len || new_len > cap) {
		/* invalid `len` value. */
		return false;
	}
	hm_parser->buf_len = new_len;
	return true;
}

uint32_t hm_parser_parse(HMParser* hm_parser, bool is_eof) {
	char *data = (char *)hm_buffer_data(hm_parser->buf);
	size_t data_len = hm_parser->buf_len;
	size_t parsed_off = hm_parser->parsed_off;

	/* resume http parser where it stopped before. */
	data_len -= parsed_off;
	if(data_len > 0) {
		/* parse data into tokens. */
		size_t nparsed = http_tokenizer_execute(hm_parser->tokenizer, data, parsed_off, data_len);
		if(nparsed > 0) {
			hm_parser->parsed_off += nparsed;
		}
		/* check for error or pause. */
		if(http_tokenizer_is_error(hm_parser->tokenizer)) {
		}
		return nparsed;
	}
	if(is_eof) {
		/* flush parser. */
		http_tokenizer_execute(hm_parser->tokenizer, data, parsed_off, 0);
	}
	return 0;
}

int hm_parser_should_keep_alive(HMParser* hm_parser) {
	return http_tokenizer_should_keep_alive(hm_parser->tokenizer);
}

int hm_parser_is_upgrade(HMParser* hm_parser) {
	return http_tokenizer_is_upgrade(hm_parser->tokenizer);
}

int hm_parser_method(HMParser* hm_parser) {
	return http_tokenizer_method(hm_parser->tokenizer);
}

const char *hm_parser_method_str(HMParser* hm_parser) {
	return http_tokenizer_method_str(hm_parser->tokenizer);
}

int hm_parser_version(HMParser* hm_parser) {
	return http_tokenizer_version(hm_parser->tokenizer);
}

int hm_parser_status_code(HMParser* hm_parser) {
	return http_tokenizer_status_code(hm_parser->tokenizer);
}

int hm_parser_is_error(HMParser* hm_parser) {
	return http_tokenizer_is_error(hm_parser->tokenizer);
}

int hm_parser_error(HMParser* hm_parser) {
	return http_tokenizer_error(hm_parser->tokenizer);
}

const char *hm_parser_error_name(HMParser* hm_parser) {
	return http_tokenizer_error_name(hm_parser->tokenizer);
}

const char *hm_parser_error_description(HMParser* hm_parser) {
	return http_tokenizer_error_description(hm_parser->tokenizer);
}

