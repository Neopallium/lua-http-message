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

local setmetatable = setmetatable

local hm = require"http_message"

local parser_mt = {}
local meths = {}
parser_mt.__index = meths


function meths:is_upgrade()
	return self.hm_parser:is_upgrade()
end

function meths:should_keep_alive()
	return self.hm_parser:should_keep_alive()
end

function meths:method()
	return self.hm_parser:method_str()
end

function meths:version()
	local version = self.hm_parser:version()
	return (version / 65536), (version % 65536)
end

function meths:status_code()
	return self.hm_parser:status_code()
end

function meths:is_error()
	return self.hm_parser:is_error()
end

function meths:error()
	return self.hm_parser:error(), self.hm_parser:error_name(), self.hm_parser:error_description()
end

function meths:on_init()
end

function meths:on_reset()
end

function meths:on_error()
	local err, name, desp = self:error()
	print('default: on_error:', err, name, desp)
end

function meths:on_message_begin()
	print('default: on_message_begin')
end

function meths:on_headers_complete()
	print('default: on_headers_complete')
end

function meths:on_body()
	print('default: on_body')
end

function meths:on_message_complete()
	print('default: on_message_complete')
end

local states = hm.states
local NONE = states.NONE
local MESSAGE_BEGIN = states.MESSAGE_BEGIN
local STATUS_COMPLETE = states.STATUS_COMPLETE
local URL = states.URL
local HEADERS = states.HEADERS
local HEADERS_COMPLETE = states.HEADERS_COMPLETE
local BODY = states.BODY
local MESSAGE_COMPLETE = states.MESSAGE_COMPLETE
local NEEDS_INPUT = states.NEEDS_INPUT
local ERROR = states.ERROR

local function hm_MESSAGE_COMPLETE(self)
	-- Send on_body(nil) message to comply with LTN12
	self:on_body()
	self:on_message_complete()
	-- Prepare parser for next message.
	self.hm_parser:next_message()
	self.last_state = NONE
end

local function hm_BODY(self)
	local on_body = self.on_body
	local hm_parser = self.hm_parser
	repeat
		local data = hm_parser:next_body()
		if not data then break end
		on_body(self, data)
	until false
	self.last_state = HEADERS_COMPLETE
end

local header_ids = hm.header_ids

local function hm_HEADERS_COMPLETE(self)
	local hm_parser = self.hm_parser
	local req = self.req
	req.url = hm_parser:get_url()
	local count = hm_parser:count_headers()
	local headers = req.headers
	for i=0,count-1 do
		local id, name, value = hm_parser:get_header(i)
		if id > 0 then
			name = header_ids[id]
		end
		headers[name] = value
	end
	self:on_headers_complete()
end

local function hm_MESSAGE_BEGIN(self)
	self.req = self:on_message_begin()
end

local function null_state(self)
end

local parser_states = {
	[NONE] = null_state,
	[MESSAGE_BEGIN] = hm_MESSAGE_BEGIN,
	[STATUS_COMPLETE] = null_state,
	[URL] = null_state,
	[HEADERS] = null_state,
	[HEADERS_COMPLETE] = hm_HEADERS_COMPLETE,
	[BODY] = hm_BODY,
	[MESSAGE_COMPLETE] = hm_MESSAGE_COMPLETE,
}

local function parser_execute(self)
	local hm_parser = self.hm_parser
	local needs_input = false

	local rc = hm_parser:execute()
	if rc >= NEEDS_INPUT then
		if rc >= ERROR then
			-- parser error
			return self:on_error()
		end
		-- needs input
		needs_input = true
		rc = rc - NEEDS_INPUT
	end
	local last = self.last_state
	if rc ~= last then
		self.last_state = rc
		-- flush all states from last to new.
		for s=last+1,rc do
			parser_states[s](self)
		end
	end

	if needs_input then
		return true
	end

	return parser_execute(self)
end

function meths:execute(data)
	if data then
		self.hm_parser:append(data)
	end
	return parser_execute(self)
end

function meths:execute_buffer(buf)
	if buf then
		self.hm_parser:append_buffer(buf)
	end
	return parser_execute(self)
end

function meths:reset()
	self.last_state = NONE
	self.hm_parser:reset()
	return self:on_reset()
end

local function create_parser(hm_parser)
	local self = {
		hm_parser = hm_parser,
		last_state = NONE,
	}
	return setmetatable(self, parser_mt)
end

module(...)

function request()
	return create_parser(hm.request())
end

function response()
	return create_parser(hm.response())
end

