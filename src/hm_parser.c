#include <assert.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "hm_parser.h"

#include "hm_buffer.h"

#include "hm_array.h"

#include "http-parser/http_parser.h"

#define MIN_BUFFER_SPACE 1024

#define MAX_HEADERS 512
#define INIT_HEADERS 8
#define GROW_HEADERS 8

#define MAX_CHUNKS 512
#define INIT_CHUNKS 16
#define GROW_CHUNKS 16

#define MAX_PIECES  4096
#define INIT_PIECES ((INIT_HEADERS * 2) + INIT_CHUNKS)
#define GROW_PIECES 128

typedef enum {
	hm_piece_url = 0,
	hm_piece_header_field,
	hm_piece_header_value,
	hm_piece_body,
	hm_piece_none,
} hm_piece_t;

typedef uint32_t httpoff_t;
typedef uint32_t httplen_t;

#define HM_PIECE_INVALID (UINT32_MAX)

typedef uint32_t hm_len_t;

typedef struct HMPiece HMPiece;
typedef struct HMHeader HMHeader;

struct HMPiece {
	httpoff_t   start;  /**< offset to start of piece in the buffer. */
	httpoff_t   end;    /**< offset to end of piece in the buffer. */
};

struct HMHeader {
	uint16_t idx;    /**< offset to header's pieces. (name at idx, value at idx+1) */
};

/**
 * HTTP message object.
 *
 * @ingroup Objects
 */
struct HMParser {
	http_parser parser;   /**< embedded http_parser. */
	HMPiece       *pieces;
	hm_piece_t    last_id;
	int is_eof;
	hm_parser_state_t state;
	hm_len_t      parsed_off;   /**< http parser offset. */
	hm_len_t      buf_len;      /**< number of bytes in buffer. */
	HMBuffer      *buf;         /**< buffer to hold raw http message. */
	/* HTTP Message fields. */
	int           url_idx;
	int           headers_start;
	int           headers_end;
	int           body_start;
};

static void hm_parser_clear_message(HMParser *hm_parser) {
	/* clear parser state. */
	hm_parser->state = hm_parser_state_none;
	hm_parser->last_id = hm_piece_none;
	/* clear HTTP Message fields */
	hm_array_set_count(hm_parser->pieces, 0);
	hm_parser->url_idx = HM_PIECE_INVALID;
	hm_parser->headers_start = HM_PIECE_INVALID;
	hm_parser->headers_end = HM_PIECE_INVALID;
	hm_parser->body_start = HM_PIECE_INVALID;
}

static HMParser *hm_parser_new(int is_request) {
	HMParser* hm_parser;
	http_parser* parser;

	hm_parser = (HMParser *)malloc(sizeof(HMParser));
	/* init. http parser. */
	parser = &(hm_parser->parser);
	if(is_request) {
		parser->type = HTTP_REQUEST;
	} else {
		parser->type = HTTP_RESPONSE;
	}
	http_parser_init(parser, parser->type);
	/* allocate piece array. */
	hm_array_new(hm_parser->pieces, INIT_PIECES);
	/* allocate buffer. */
	hm_parser->buf = hm_buffer_new(MIN_BUFFER_SPACE);
	parser->data = (char *)hm_buffer_data(hm_parser->buf);
	/* clear buffer state. */
	hm_parser->parsed_off = 0;
	hm_parser->buf_len = 0;

	hm_parser_clear_message(hm_parser);

	return hm_parser;
}

HMParser *hm_parser_new_response() {
	return hm_parser_new(0);
}

HMParser *hm_parser_new_request() {
	return hm_parser_new(1);
}

void hm_parser_next_message(HMParser *hm_parser) {
	size_t offset = hm_parser->parsed_off;
	size_t len = hm_parser->buf_len;

	/* remove previous message from buffer. */
	if(offset < len) {
		/* The buffer has some data for the next message.
		 * Compact the buffer.
		 */
		char *data = (char *)hm_buffer_data(hm_parser->buf);
		len -= offset;
		memmove(data, data + offset, len);
	} else {
		len = 0;
	}
	hm_parser->parsed_off = 0;
	hm_parser->buf_len = len;

	hm_parser_clear_message(hm_parser);
}

void hm_parser_reset(HMParser* hm_parser) {
	http_parser* parser = &(hm_parser->parser);

	http_parser_init(parser, parser->type);
	parser->data = (char *)hm_buffer_data(hm_parser->buf);
	/* clear buffer state. */
	hm_parser->parsed_off = 0;
	hm_parser->buf_len = 0;
	hm_parser->is_eof = true;

	hm_parser_clear_message(hm_parser);
}

void hm_parser_free(HMParser* hm_parser) {
	hm_buffer_free(hm_parser->buf);
	hm_parser->buf = NULL;
	hm_array_free(hm_parser->pieces);
	hm_parser->pieces = NULL;
	free(hm_parser);
}

#define HM_PARSER_ARY_GROW_CHECK(hm_parser, _ary, _idx, _grow, _max) do { \
	typeof((hm_parser)->_ary) ary = (hm_parser)->_ary; \
	size_t count = hm_array_count(ary) + 1; \
	size_t cap = hm_array_capacity(ary); \
	if(count >= cap) { \
		if(cap == (_max)) { \
			return -1; \
		} else { \
			cap += (_grow); \
			if(cap > (_max)) cap = (_max); \
			hm_array_resize(ary, cap); \
			if(ary == NULL) return -1; \
			(hm_parser)->_ary = ary; \
		} \
	} \
	(_idx) = count; \
} while(0)

#define HM_PARSER_PIECES_GROW_CHECK(hm_parser, _idx) \
	HM_PARSER_ARY_GROW_CHECK(hm_parser, pieces, _idx, GROW_PIECES, MAX_PIECES)

/* push piece. */
static int http_push_piece(http_parser* parser, hm_piece_t piece_id, const char *data, size_t len) {
	HMParser *hm_parser = (HMParser*)parser;
	const char *data_start = (const char *)parser->data;
	size_t start = data - data_start;
	uint32_t idx;
	HMPiece *piece;
	bool append_to_last = false;

	/* check if we need to append data to last piece. */
	if(hm_parser->last_id == piece_id) {
		append_to_last = true;
		/* don't merge large body pieces. */
		if(piece_id == hm_piece_body && len > 100) {
			append_to_last = false;
		}
	}

	if(append_to_last) {
		size_t end;
		/* append data to last piece. */
		idx = hm_array_count(hm_parser->pieces);
		piece = hm_parser->pieces + idx;
		/* check for buffer gaps. */
		end = piece->end;
		if(end != start) {
			char *end_ptr = ((char *)parser->data) + end;
			/* close gap for this piece. */
			memmove(end_ptr, data, len);
		}
		piece->end = end + len;
		return 0;
	}

	/* start new piece. */
	HM_PARSER_PIECES_GROW_CHECK(hm_parser, idx);

	hm_parser->last_id = piece_id;

	/* initialize new piece. */
	piece = hm_parser->pieces + idx;
	piece->start = start;
	piece->end = start + len;
	return 0;
}

static int hm_parser_message_begin_cb(http_parser* parser) {
	HMParser *hm_parser = (HMParser*)parser;
	hm_parser->state = hm_parser_state_message_begin;
	hm_parser->last_id = hm_piece_none;
	return 0;
}

static int hm_parser_status_complete_cb(http_parser* parser) {
	HMParser *hm_parser = (HMParser*)parser;
	hm_parser->state = hm_parser_state_status_complete;
	hm_parser->last_id = hm_piece_none;
	return 0;
}

static int hm_parser_url_cb(http_parser* parser, const char* data, size_t len) {
	HMParser *hm_parser = (HMParser*)parser;
	hm_parser->state = hm_parser_state_url;
	if(hm_parser->url_idx < 0) {
		hm_parser->url_idx = hm_array_count(hm_parser->pieces);
	}
	return http_push_piece(parser, hm_piece_url, data, len);
}

static int hm_parser_header_field_cb(http_parser* parser, const char* data, size_t len) {
	HMParser *hm_parser = (HMParser*)parser;
	hm_parser->state = hm_parser_state_headers;
	if(hm_parser->headers_start < 0) {
		hm_parser->headers_start = hm_array_count(hm_parser->pieces);
		hm_parser->headers_end = hm_parser->headers_start;
	}
	return http_push_piece(parser, hm_piece_header_field, data, len);
}

static int hm_parser_header_value_cb(http_parser* parser, const char* data, size_t len) {
	HMParser *hm_parser = (HMParser*)parser;
	hm_parser->state = hm_parser_state_headers;
	return http_push_piece(parser, hm_piece_header_value, data, len);
}

static int hm_parser_headers_complete_cb(http_parser* parser) {
	HMParser *hm_parser = (HMParser*)parser;
	hm_parser->state = hm_parser_state_headers_complete;
	hm_parser->last_id = hm_piece_none;
	/* mark end of headers. */
	hm_parser->headers_end = hm_array_count(hm_parser->pieces);
	http_parser_pause(parser, 1);
	return 0;
}

static int hm_parser_body_cb(http_parser* parser, const char* data, size_t len) {
	HMParser *hm_parser = (HMParser*)parser;
	hm_parser->state = hm_parser_state_body;
	if(len == 0) return 0;
	return http_push_piece(parser, hm_piece_body, data, len);
}

static int hm_parser_message_complete_cb(http_parser* parser) {
	HMParser *hm_parser = (HMParser*)parser;
	hm_parser->state = hm_parser_state_message_complete;
	hm_parser->last_id = hm_piece_none;
	http_parser_pause(parser, 1);
	return 0;
}

static uint32_t hm_parser_resume_parse(HMParser *hm_parser) {
	http_parser*  parser = &(hm_parser->parser);
	char *data = (char *)hm_buffer_data(hm_parser->buf);
	size_t data_len = hm_parser->buf_len;
	size_t parsed_off = hm_parser->parsed_off;

	static const http_parser_settings settings = {
		.on_message_begin    = hm_parser_message_begin_cb,
		.on_url              = hm_parser_url_cb,
		.on_status_complete  = hm_parser_status_complete_cb,
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
		/* parse data into pieces. */
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

