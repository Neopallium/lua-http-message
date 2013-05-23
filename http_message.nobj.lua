
-- make generated variable nicer.
set_variable_format "%s"

c_module "http_message" {

-- enable FFI bindings support.
luajit_ffi = true,
luajit_ffi_load_cmodule = true,

export_definitions "states" {
NONE             = "HM_PARSER_STATE_NONE",
MESSAGE_BEGIN    = "HM_PARSER_STATE_MESSAGE_BEGIN",
STATUS_COMPLETE  = "HM_PARSER_STATE_STATUS_COMPLETE",
URL              = "HM_PARSER_STATE_URL",
HEADERS          = "HM_PARSER_STATE_HEADERS",
HEADERS_COMPLETE = "HM_PARSER_STATE_HEADERS_COMPLETE",
BODY             = "HM_PARSER_STATE_BODY",
MESSAGE_COMPLETE = "HM_PARSER_STATE_MESSAGE_COMPLETE",
NEEDS_INPUT      = "HM_PARSER_STATE_NEEDS_INPUT",
ERROR            = "HM_PARSER_STATE_ERROR",
},

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

