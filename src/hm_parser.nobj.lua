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

object "HMParser" {
	include"hm_parser.h",
	destructor {
		c_method_call "void" "hm_parser_free" {},
	},

	method "reset" {
		c_method_call "void" "hm_parser_reset" {},
	},

	method "next_message" {
		c_method_call "void" "hm_parser_next_message" {},
	},

	method "parse" {
		c_method_call "uint32_t" "hm_parser_parse" { "bool", "is_eof" },
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

