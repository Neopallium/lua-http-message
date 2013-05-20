#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "hm_parser.h"

#include "hm_buffer.h"

#include "hm_array.h"

#include "http-parser/http_parser.h"

#define HTTP_TOKENIZER_MAX_TOKENS		4096

#define HTTP_TOKEN_MESSAGE_BEGIN      0
#define HTTP_TOKEN_URL                1
#define HTTP_TOKEN_HEADER_FIELD       2
#define HTTP_TOKEN_HEADER_VALUE       3
#define HTTP_TOKEN_HEADERS_COMPLETE   4
#define HTTP_TOKEN_BODY               5
#define HTTP_TOKEN_MESSAGE_COMPLETE   6

typedef uint32_t httpoff_t;
typedef uint32_t httplen_t;

typedef uint32_t hm_len_t;

#define HTTP_TOKENIZER_MAX_CHUNK_LENGTH (2 ^ (sizeof(httplen_t) * 8))

typedef struct http_token http_token;
struct http_token {
	uint32_t    id;   /**< token id. */
	httpoff_t   off;  /**< token offset. */
	httplen_t   len;  /**< token length. */
};

#define MIN_BUFFER_SPACE 1024

#define INIT_TOKENS 32
#define GROW_TOKENS 128

/**
 * HTTP message object.
 *
 * @ingroup Objects
 */
struct HMParser {
	http_parser parser;   /**< embedded http_parser. */
	http_token  *tokens;  /**< array of parsed tokens. */
	uint16_t    count;    /**< number of parsed tokens. */
	uint16_t    len;      /**< length of tokens array. */
	int is_eof;
	hm_len_t      parsed_off;   /**< http parser offset. */
	hm_len_t      buf_len;      /**< number of bytes in buffer. */
	HMBuffer      *buf;         /**< buffer to hold raw http message. */
};

static HMParser *hm_parser_new(int is_request) {
	HMParser* hm_parser;
	http_parser* parser;
	uint32_t len = INIT_TOKENS;

	hm_parser = (HMParser *)malloc(sizeof(HMParser));
	/* init. http parser. */
	parser = &(hm_parser->parser);
	if(is_request) {
		parser->type = HTTP_REQUEST;
	} else {
		parser->type = HTTP_RESPONSE;
	}
	http_parser_init(parser, parser->type);
	/* allocate token array. */
	hm_parser->tokens = (http_token *)calloc(len, sizeof(http_token));
	hm_parser->len = len;
	hm_parser->count = 0;
	/* allocate buffer. */
	hm_parser->buf = hm_buffer_new(MIN_BUFFER_SPACE);
	parser->data = (char *)hm_buffer_data(hm_parser->buf);
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
	http_parser* parser = &(hm_parser->parser);

	hm_parser->count = 0;
	http_parser_init(parser, parser->type);
	parser->data = (char *)hm_buffer_data(hm_parser->buf);
	/* clear buffer state. */
	hm_parser->parsed_off = 0;
	hm_parser->buf_len = 0;
	hm_parser->is_eof = true;
}

void hm_parser_free(HMParser* hm_parser) {
	hm_buffer_free(hm_parser->buf);
	hm_parser->buf = NULL;
	free(hm_parser->tokens);
	hm_parser->tokens = NULL;
	free(hm_parser);
}

static int hm_parser_grow(HMParser *hm_parser) {
	uint32_t    len = hm_parser->len + GROW_TOKENS;
	http_token  *tokens;

	if(len > HTTP_TOKENIZER_MAX_TOKENS) return -1;

	tokens = (http_token *)realloc(hm_parser->tokens, sizeof(http_token) * len);
	if(tokens == NULL) return -1;
	hm_parser->tokens = tokens;
	hm_parser->len = len;
	if((len + GROW_TOKENS) > HTTP_TOKENIZER_MAX_TOKENS) {
		/* can't hold any more tokens, pause parsing. */
		http_parser_pause(&(hm_parser->parser), 1);
	}
	return 0;
}

#define HTTP_TOKENIZER_GROW_CHECK(hm_parser) do { \
	if((hm_parser)->count >= (hm_parser)->len) { \
		if(hm_parser_grow(hm_parser) != 0) { \
			return -1; \
		} \
	} \
} while(0)

/* push token with no data. */
static int http_push_token(http_parser* parser, int token_id) {
	HMParser *hm_parser = (HMParser*)parser;
	uint32_t idx = hm_parser->count++;
	http_token *token;

	HTTP_TOKENIZER_GROW_CHECK(hm_parser);

	token = hm_parser->tokens + idx;
	token->id = token_id;
	token->off = 0;
	token->len = 0;

	return 0;
}

/* push token with data. */
static int http_push_data_token(http_parser* parser, int token_id, const char *data, size_t len) {
	HMParser *hm_parser = (HMParser*)parser;
	const char *data_start = (const char *)parser->data;
	uint32_t idx = hm_parser->count++;
	http_token *token;

	HTTP_TOKENIZER_GROW_CHECK(hm_parser);

	token = hm_parser->tokens + idx;
	token->id = token_id;
	token->off = (data - data_start);
	token->len = len;

	return 0;
}

static int hm_parser_message_begin_cb(http_parser* parser) {
	return http_push_token(parser, HTTP_TOKEN_MESSAGE_BEGIN);
}

static int hm_parser_url_cb(http_parser* parser, const char* data, size_t len) {
	return http_push_data_token(parser, HTTP_TOKEN_URL, data, len);
}

static int hm_parser_header_field_cb(http_parser* parser, const char* data, size_t len) {
	return http_push_data_token(parser, HTTP_TOKEN_HEADER_FIELD, data, len);
}

static int hm_parser_header_value_cb(http_parser* parser, const char* data, size_t len) {
	return http_push_data_token(parser, HTTP_TOKEN_HEADER_VALUE, data, len);
}

static int hm_parser_headers_complete_cb(http_parser* parser) {
	if(http_push_token(parser, HTTP_TOKEN_HEADERS_COMPLETE) < 0) return -1;
	http_parser_pause(parser, 1);
	return 0;
}

static int hm_parser_body_cb(http_parser* parser, const char* data, size_t len) {
	if(len == 0) return 0;
	return http_push_data_token(parser, HTTP_TOKEN_BODY, data, len);
}

static int hm_parser_message_complete_cb(http_parser* parser) {
	if(http_push_token(parser, HTTP_TOKEN_MESSAGE_COMPLETE) < 0) return -1;
	http_parser_pause(parser, 1);
	return 0;
}

void hm_parser_consume_tokens(HMParser *hm_parser, int count) {
	uint16_t new_count = 0;
	assert(count <= hm_parser->count);
	if(hm_parser->count > count) {
		new_count = hm_parser->count - count;
		/* slow patch consume some tokens.  Need to move other tokens to start. */
		memmove(hm_parser->tokens, &(hm_parser->tokens[count]), sizeof(http_token) * new_count);
	}
	/* fast path consumed all tokens. */
	hm_parser->count = new_count;
}

static uint32_t hm_parser_resume_parse(HMParser *hm_parser) {
	http_parser*  parser = &(hm_parser->parser);
	char *data = (char *)hm_buffer_data(hm_parser->buf);
	size_t data_len = hm_parser->buf_len;
	size_t parsed_off = hm_parser->parsed_off;

	static const http_parser_settings settings = {
		.on_message_begin    = hm_parser_message_begin_cb,
		.on_url              = hm_parser_url_cb,
		.on_header_field     = hm_parser_header_field_cb,
		.on_header_value     = hm_parser_header_value_cb,
		.on_headers_complete = hm_parser_headers_complete_cb,
		.on_body             = hm_parser_body_cb,
		.on_message_complete = hm_parser_message_complete_cb
	};

	/* resume http parser where it stopped before. */
	data_len -= parsed_off;
	data += parsed_off;
	if(data_len > 0) {
		/* parse data into tokens. */
		size_t nparsed = http_parser_execute(parser, &settings, data, data_len);
		if(nparsed > 0) {
			hm_parser->parsed_off += nparsed;
		}
		/* check for error or pause. */
		if(hm_parser_is_error(hm_parser)) {
		}
		if(parser->http_errno == HPE_PAUSED) {
			/* resume parser. */
			http_parser_pause(parser, 0);
		}
		return nparsed;
	}
	if(hm_parser->is_eof) {
		/* flush parser. */
		http_parser_execute(parser, &settings, data, 0);
	}

	return 0;
}

const http_token *hm_parser_get_tokens(HMParser *hm_parser) {
	return hm_parser->tokens;
}

uint32_t hm_parser_count_tokens(HMParser *hm_parser) {
	return hm_parser->count;
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
			hm_parser->parser.data = (char *)hm_buffer_data(buf);
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
	if(is_eof) {
		hm_parser->is_eof = true;
	}
	return hm_parser_resume_parse(hm_parser);
}

int hm_parser_should_keep_alive(HMParser *hm_parser) {
	return http_should_keep_alive(&hm_parser->parser);
}

int hm_parser_is_upgrade(HMParser *hm_parser) {
	return hm_parser->parser.upgrade;
}

int hm_parser_method(HMParser *hm_parser) {
	return hm_parser->parser.method;
}

const char *hm_parser_method_str(HMParser *hm_parser) {
	return http_method_str(hm_parser->parser.method);
}

int hm_parser_version(HMParser *hm_parser) {
	return ((hm_parser->parser.http_major) << 16) + (hm_parser->parser.http_minor);
}

int hm_parser_status_code(HMParser *hm_parser) {
	return hm_parser->parser.status_code;
}

int hm_parser_is_error(HMParser *hm_parser) {
	return (hm_parser->parser.http_errno != HPE_OK && hm_parser->parser.http_errno != HPE_PAUSED);
}

int hm_parser_error(HMParser *hm_parser) {
	return hm_parser->parser.http_errno;
}

const char *hm_parser_error_name(HMParser *hm_parser) {
	return http_errno_name(hm_parser->parser.http_errno);
}

const char *hm_parser_error_description(HMParser *hm_parser) {
	return http_errno_description(hm_parser->parser.http_errno);
}

