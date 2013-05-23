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

#define HM_PIECE_INVALID (UINT16_MAX)

typedef struct HMPiece HMPiece;

struct HMPiece {
	hm_len_t    start;  /**< offset to start of piece in the buffer. */
	hm_len_t    end;    /**< offset to end of piece in the buffer. */
};

/**
 * HTTP message object.
 *
 * @ingroup Objects
 */
struct HMParser {
	http_parser parser;   /**< embedded http_parser. */
	HMPiece       *pieces;
	HMBuffer      *buf;         /**< buffer to hold raw http message. */
	uint32_t      state: 10;
	uint32_t      last_id: 4;
	uint32_t      is_eof: 1;
	hm_len_t      parsed_off;   /**< http parser offset. */
	hm_len_t      buf_len;      /**< number of bytes in buffer. */
	/* HTTP Message fields. */
	hm_idx_t      url_idx;
	hm_idx_t      headers_start;
	hm_idx_t      headers_end;
	hm_idx_t      body_start;
	/* tmp data */
	union {
		HMString str;
		HMHeader header;
	} tmp;
};

static void hm_parser_clear_message(HMParser *hm_parser) {
	/* clear parser state. */
	hm_parser->state = HM_PARSER_STATE_NONE;
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
	/* allocate piece array. */
	hm_array_new(hm_parser->pieces, INIT_PIECES);
	/* allocate buffer. */
	hm_parser->buf = hm_buffer_new(MIN_BUFFER_SPACE);

	/* initialize parser state. */
	hm_parser_reset(hm_parser);

	return hm_parser;
}

HMParser *hm_parser_new_response() {
	return hm_parser_new(0);
}

HMParser *hm_parser_new_request() {
	return hm_parser_new(1);
}

static void hm_parser_compact_buffer(HMParser *hm_parser, size_t offset) {
	size_t len = hm_parser->buf_len;

	/* Trim some data from the start of the buffer. */
	if(offset < len) {
		/* Compact the buffer. */
		char *data = (char *)hm_buffer_data(hm_parser->buf);
		len -= offset;
		memmove(data, data + offset, len);
		hm_parser->parsed_off -= offset;
		hm_parser->buf_len = len;
	} else {
		/* Trimmed the whole buffer. */
		hm_parser->parsed_off = 0;
		hm_parser->buf_len = 0;
	}
}

void hm_parser_next_message(HMParser *hm_parser) {
	/* remove previous message from buffer. */
	hm_parser_compact_buffer(hm_parser, hm_parser->parsed_off);

	hm_parser_clear_message(hm_parser);
}

void hm_parser_reset(HMParser* hm_parser) {
	http_parser* parser = &(hm_parser->parser);

	http_parser_init(parser, parser->type);
	parser->data = (char *)hm_buffer_data(hm_parser->buf);
	/* clear buffer state. */
	hm_parser->parsed_off = 0;
	hm_parser->buf_len = 0;
	hm_parser->is_eof = false;

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
	size_t count = hm_array_count(ary); \
	size_t cap = hm_array_capacity(ary); \
	(_idx) = count; \
	count++; \
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
	hm_array_set_count(ary, count); \
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
		idx = hm_array_count(hm_parser->pieces) - 1;
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
	if(hm_parser->state != HM_PARSER_STATE_NONE) {
		hm_parser_clear_message(hm_parser);
	}
	hm_parser->state = HM_PARSER_STATE_MESSAGE_BEGIN;
	hm_parser->last_id = hm_piece_none;
	return 0;
}

static int hm_parser_status_complete_cb(http_parser* parser) {
	HMParser *hm_parser = (HMParser*)parser;
	hm_parser->state = HM_PARSER_STATE_STATUS_COMPLETE;
	hm_parser->last_id = hm_piece_none;
	return 0;
}

static int hm_parser_url_cb(http_parser* parser, const char* data, size_t len) {
	HMParser *hm_parser = (HMParser*)parser;
	hm_parser->state = HM_PARSER_STATE_URL;
	if(hm_parser->url_idx == HM_PIECE_INVALID) {
		hm_parser->url_idx = hm_array_count(hm_parser->pieces);
	}
	return http_push_piece(parser, hm_piece_url, data, len);
}

static int hm_parser_header_field_cb(http_parser* parser, const char* data, size_t len) {
	HMParser *hm_parser = (HMParser*)parser;
	hm_parser->state = HM_PARSER_STATE_HEADERS;
	if(hm_parser->headers_start == HM_PIECE_INVALID) {
		hm_parser->headers_start = hm_array_count(hm_parser->pieces);
		hm_parser->headers_end = hm_parser->headers_start;
	}
	return http_push_piece(parser, hm_piece_header_field, data, len);
}

static int hm_parser_header_value_cb(http_parser* parser, const char* data, size_t len) {
	HMParser *hm_parser = (HMParser*)parser;
	hm_parser->state = HM_PARSER_STATE_HEADERS;
	return http_push_piece(parser, hm_piece_header_value, data, len);
}

static int hm_parser_headers_complete_cb(http_parser* parser) {
	HMParser *hm_parser = (HMParser*)parser;
	hm_parser->state = HM_PARSER_STATE_HEADERS_COMPLETE;
	hm_parser->last_id = hm_piece_none;
	/* mark end of headers. */
	if(hm_parser->headers_start != HM_PIECE_INVALID) {
		hm_parser->headers_end = hm_array_count(hm_parser->pieces);
	}
	http_parser_pause(parser, 1);
	return 0;
}

static int hm_parser_body_cb(http_parser* parser, const char* data, size_t len) {
	HMParser *hm_parser = (HMParser*)parser;
	hm_parser->state = HM_PARSER_STATE_BODY;
	if(len == 0) return 0;
	/* mark start of body pieces. */
	if(hm_parser->body_start == HM_PIECE_INVALID) {
		hm_parser->body_start = hm_array_count(hm_parser->pieces);
		hm_parser->last_id = hm_piece_none;
	}
	return http_push_piece(parser, hm_piece_body, data, len);
}

static int hm_parser_message_complete_cb(http_parser* parser) {
	HMParser *hm_parser = (HMParser*)parser;
	hm_parser->state = HM_PARSER_STATE_MESSAGE_COMPLETE;
	hm_parser->last_id = hm_piece_none;
	http_parser_pause(parser, 1);
	return 0;
}

static int hm_parser_resume_parse(HMParser *hm_parser, char *data, size_t data_len) {
	http_parser*  parser = &(hm_parser->parser);

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

	/* resume http parser. */
	size_t nparsed = http_parser_execute(parser, &settings, data, data_len);
	if(nparsed > 0) {
		hm_parser->parsed_off += nparsed;
	}
	/* check for error. */
	if(parser->http_errno != HPE_OK) {
		/* check if parser was paused. */
		if(parser->http_errno == HPE_PAUSED) {
			/* resume parser. */
			http_parser_pause(parser, 0);
		} else {
			/* return -1 to indicate a parser error. */
			hm_parser->state |= HM_PARSER_STATE_ERROR;
		}
		return -1;
	}

	return nparsed;
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

size_t hm_parser_append_data(HMParser *hm_parser, const char *data, size_t len) {
	size_t space = hm_parser_prepare_buffer(hm_parser, len);
	if(space < len) {
		len = space;
	}
	memcpy(hm_parser_get_buffer(hm_parser), data, len);
	hm_parser->buf_len += len;
	hm_parser->state &= ~HM_PARSER_STATE_NEEDS_INPUT;

	return len;
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
	hm_parser->state &= ~HM_PARSER_STATE_NEEDS_INPUT;
	return true;
}

void hm_parser_eof(HMParser *hm_parser) {
	hm_parser->is_eof = true;
	/* remove the NEEDS_INPUT flag to allow parser to resume. */
	hm_parser->state &= ~HM_PARSER_STATE_NEEDS_INPUT;
}

int hm_parser_execute(HMParser* hm_parser) {
	char *data = (char *)hm_buffer_data(hm_parser->buf);
	size_t data_len = hm_parser->buf_len;
	size_t parsed_off = hm_parser->parsed_off;
	int rc = 0;

	/* check if parser needs input or is already in an error state. */
	if(hm_parser->state & (HM_PARSER_STATE_NEEDS_INPUT|HM_PARSER_STATE_ERROR)) {
		return hm_parser->state;
	}

	/* Calculate how much data is unparsed. */
	data_len -= parsed_off;
	data += parsed_off;

	if(data_len > 0) {
		/* have data to parse. */
		rc = hm_parser_resume_parse(hm_parser, data, data_len);
		if(rc < 0) {
			/* paused or error. */
			return hm_parser->state;
		}
		data_len -= rc;
		if(data_len == 0 && hm_parser->is_eof) {
			data += rc;
			/* finished parsing buffer.  Now signal EOF. */
			hm_parser_resume_parse(hm_parser, data, 0);
			hm_parser->state |= HM_PARSER_STATE_NEEDS_INPUT;
		}
	} else {
		if(hm_parser->is_eof) {
			/* signal EOF. */
			hm_parser_resume_parse(hm_parser, data, 0);
			hm_parser->state |= HM_PARSER_STATE_NEEDS_INPUT;
		} else {
			/* need data. */
			hm_parser->state |= HM_PARSER_STATE_NEEDS_INPUT;
		}
	}

	return hm_parser->state;
}

HMString *hm_parser_get_url(HMParser *hm_parser) {
	HMString *str = NULL;
	hm_idx_t idx = hm_parser->url_idx;
	if(idx != HM_PIECE_INVALID) {
		const char *data = (const char *)hm_buffer_data(hm_parser->buf);
		HMPiece *url = hm_parser->pieces + idx;
		str = &(hm_parser->tmp.str);
		str->str = data + url->start;
		str->len = url->end - url->start;
	}
	return str;
}

uint32_t hm_parser_count_headers(HMParser *hm_parser) {
	uint32_t headers_start = hm_parser->headers_start;
	uint32_t headers_end = hm_parser->headers_end;
	assert(headers_start <= headers_end);
	if(headers_start == HM_PIECE_INVALID) {
		return 0;
	}
	return (headers_end - headers_start) >> 1;
}

HMHeader *hm_parser_get_header(HMParser *hm_parser, uint32_t idx) {
	HMHeader *head = NULL;
	uint32_t headers_start = hm_parser->headers_start;
	uint32_t headers_end = hm_parser->headers_end;
	uint32_t count = headers_end - headers_start;
	const char *data;
	HMPiece  *name;
	HMPiece  *value;

	/* check for headers. */
	if(headers_start == HM_PIECE_INVALID) {
		/* no headers. */
		return head;
	}
	/* validate 'idx'. */
	idx *= 2; /* each header has two pieces. */
	if(idx >= count) {
		/* idx out of bounds. */
		return head;
	}
	idx += headers_start;

	/* get name & value pieces. */
	data = (const char *)hm_buffer_data(hm_parser->buf);
	name = hm_parser->pieces + idx;
	value = name + 1;

	/* fill tmp. HMHeader. */
	head = &(hm_parser->tmp.header);
	head->name_id = 0;
	head->name = data + name->start;
	head->name_len = name->end - name->start;
	head->value = data + value->start;
	head->value_len = value->end - value->start;

	return head;
}

HMString *hm_parser_next_body(HMParser *hm_parser) {
	HMString *str = NULL;
	hm_idx_t idx = hm_parser->body_start;
	if(idx != HM_PIECE_INVALID) {
		const char *data = (const char *)hm_buffer_data(hm_parser->buf);
		HMPiece *piece = hm_parser->pieces + idx;
		str = &(hm_parser->tmp.str);
		str->str = data + piece->start;
		str->len = piece->end - piece->start;
		/* consume piece. */
		idx++;
		if(idx >= hm_array_count(hm_parser->pieces)) {
			idx = HM_PIECE_INVALID; /* no more pieces. */
		}
		hm_parser->body_start = idx;
	}
	return str;
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

