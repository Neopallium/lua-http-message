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
local meths = {
on_message_begin = function()
end,
on_url = function(data)
end,
on_header = function(k,v)
end,
on_headers_complete = function()
end,
on_body = function(data)
end,
on_message_complete = function()
end,
}
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
	self.on_body()
	self.on_message_complete()
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
		on_body(data)
	until false
	self.last_state = HEADERS_COMPLETE
end

local function flush_url(self)
	if not self.url then
		local url = self.hm_parser:get_url()
		self.url = url
		return self.on_url(url)
	end
end

local header_ids = hm.header_ids

local function hm_HEADERS_COMPLETE(self)
	local hm_parser = self.hm_parser
	flush_url(self)
	local count = hm_parser:count_headers()
	local on_header = self.on_header
	for i=0,count-1 do
		local id, name, value = hm_parser:get_header(i)
		if id > 0 then
			name = header_ids[id]
		end
		on_header(name, value)
	end
	self.on_headers_complete()
end

local function hm_HEADERS(self)
	flush_url(self)
end

local function hm_MESSAGE_BEGIN(self)
	self.on_message_begin()
	self.url = nil
end

local function null_state(self)
end

local parser_states = {
	[NONE] = null_state,
	[MESSAGE_BEGIN] = hm_MESSAGE_BEGIN,
	[STATUS_COMPLETE] = null_state,
	[URL] = null_state,
	[HEADERS] = hm_HEADERS,
	[HEADERS_COMPLETE] = hm_HEADERS_COMPLETE,
	[BODY] = hm_BODY,
	[MESSAGE_COMPLETE] = hm_MESSAGE_COMPLETE,
}

local function parser_execute(self, len)
	local hm_parser = self.hm_parser
	local needs_input = false

	local rc = hm_parser:execute()
	if rc >= NEEDS_INPUT then
		if rc >= ERROR then
			-- parser error
			return 0
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
		return len
	end

	return parser_execute(self, len)
end

function meths:execute(data)
	local len = 0
	if data then
		len = #data
		if len > 0 then
			self.hm_parser:append(data)
		else
			self.hm_parser:eof()
		end
	end
	return parser_execute(self, len)
end

function meths:execute_buffer(buf)
	local len = 0
	if buf ~= nil then
		len = #buf
		if len > 0 then
			self.hm_parser:append_buffer(data)
		else
			self.hm_parser:eof()
		end
	end
	return parser_execute(self, len)
end

function meths:reset()
	self.last_state = NONE
	self.hm_parser:reset()
end

local function create_parser(hm_parser, self)
	self.last_state = NONE
	self.hm_parser = hm_parser
	return setmetatable(self, parser_mt)
end

module(...)

function request(cbs)
	return create_parser(hm.request(), cbs)
end

function response(cbs)
	return create_parser(hm.response(), cbs)
end

