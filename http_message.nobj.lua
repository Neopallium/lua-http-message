
-- make generated variable nicer.
set_variable_format "%s"

c_module "http_message" {

-- enable FFI bindings support.
--luajit_ffi = true,
--luajit_ffi_load_cmodule = true,

subfiles {
"src/hm_parser.nobj.lua",
},

c_function "request" {
	c_call "!HMParser *" "hm_parser_new_request" {},
},
c_function "response" {
	c_call "!HMParser *" "hm_parser_new_response" {},
},
}

