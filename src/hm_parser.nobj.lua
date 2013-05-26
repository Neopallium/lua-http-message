-- Copyright (c) 2011 by Robert G. Jakabosky <bobby@sharedrealm.com>
--
-- Permission is hereby granted, free of charge, to any person obtaining a copy
-- of this software and associated documentation files (the "Software"), to deal
-- in the Software without restriction, including without limitation the rights
-- to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
-- copies of the Software, and to permit persons to whom the Software is
-- furnished to do so, subject to the following conditions:
--
-- The above copyright notice and this permission notice shall be included in
-- all copies or substantial portions of the Software.
--
-- THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
-- IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
-- FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
-- AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
-- LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
-- OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
-- THE SOFTWARE.

basetype "HMHeader *" "nil" "NULL"

object "HMParser" {
	include"hm_parser.h",
	ffi_cdef[[
typedef uint32_t hm_len_t;

typedef struct HMHeader {
	const char *name;
	const char *value;
	hm_len_t   name_len;
	hm_len_t   value_len;
	int        name_id;
} HMHeader;

]],
	destructor {
		c_method_call "void" "hm_parser_free" {},
	},

	-- buffer management.

	method "append" {
		c_method_call "size_t" "hm_parser_append_data" { "const char *", "data", "size_t", "#data" },
	},

	method "append_buffer" {
		var_in{"Buffer", "buf"},
		c_source[[
	${data_len} = ${buf}_if->get_size(${buf});
	${data} = (const char *)${buf}_if->const_data(${buf});
]],
		ffi_source[[
	${data_len} = ${buf}_if.get_size(${buf})
	${data} = ${buf}_if.const_data(${buf})
]],
		c_method_call "size_t" "hm_parser_append_data"
			{ "const char *", "(data)", "size_t", "(data_len)" },
	},

	method "eof" {
		c_method_call "void" "hm_parser_eof" {},
	},

	-- control parser.

	method "reset" {
		c_method_call "void" "hm_parser_reset" {},
	},

	method "next_message" {
		c_method_call "void" "hm_parser_next_message" {},
	},

	method "execute" {
		c_method_call "int" "hm_parser_execute" {},
	},

	-- get url/headers/body chunks

	method "count_headers" {
		c_method_call "uint32_t" "hm_parser_count_headers" {},
	},

	method "clear_headers" {
		c_method_call "void" "hm_parser_clear_headers" {},
	},

	method "get_url" {
		c_method_call { "const char *", "url", has_length = 1 } "hm_parser_get_url"
			{ "size_t", "&#url" },
	},

	method "get_header" {
		var_out { "uint32_t", "name_id" },
		var_out { "const char *", "name", has_length = 1 },
		var_out { "const char *", "value", has_length = 1 },
		c_method_call { "HMHeader *", "(header)" } "hm_parser_get_header" { "uint32_t", "idx" },
		c_source [[
	if(${header}) {
		${name_id} = ${header}->name_id;
		if(${name_id} <= 0) {
			${name} = ${header}->name;
			${name_len} = ${header}->name_len;
		}
		${value} = ${header}->value;
		${value_len} = ${header}->value_len;
	}
]],
		ffi_source [[
	if ${header} ~= nil then
		local name
		local id = ${header}.name_id
		if id <= 0 then
			name = ffi_string(${header}.name, ${header}.name_len)
		end
		return id, name,
			ffi_string(${header}.value, ${header}.value_len)
	end
]],
	},

	method "next_body" {
		c_method_call { "const char *", "body", has_length = 1 } "hm_parser_next_body"
			{ "size_t", "&#body" },
	},

	-- standard http-parser methods
	method "should_keep_alive" {
		c_method_call "bool" "hm_parser_should_keep_alive" {},
	},

	method "is_upgrade" {
		c_method_call "bool" "hm_parser_is_upgrade" {},
	},

	method "method" {
		c_method_call "int" "hm_parser_method" {},
	},

	method "method_str" {
		c_method_call "const char *" "hm_parser_method_str" {},
	},

	method "version" {
		c_method_call "int" "hm_parser_version" {},
	},

	method "status_code" {
		c_method_call "int" "hm_parser_status_code" {},
	},

	method "is_error" {
		c_method_call "bool" "hm_parser_is_error" {},
	},

	method "error" {
		c_method_call "int" "hm_parser_error" {},
	},

	method "error_name" {
		c_method_call "const char *" "hm_parser_error_name" {},
	},

	method "error_description" {
		c_method_call "const char *" "hm_parser_error_description" {},
	},
}

