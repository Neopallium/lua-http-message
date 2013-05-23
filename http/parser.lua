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
local assert = assert

local hm = require"http_message"
local states = hm.states

local callback_names = {
	"on_message_begin",
	"on_url",
	"on_header",
	"on_headers_complete",
	"on_body",
	"on_message_complete",
}

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

local function hm_MESSAGE_COMPLETE(self, state)
	-- Send on_body(nil) message to comply with LTN12
	self.on_body()
	self.on_message_complete()
	-- Prepare parser for next message.
	self.hm_parser:next_message()
	return NONE
end

local function hm_BODY(self, state)
	local on_body = self.on_body
	repeat
		local data = self.hm_parser:next_body()
		if not data then break end
		on_body(data)
	until false
	if state >= MESSAGE_COMPLETE then
		return hm_MESSAGE_COMPLETE(self, state)
	end
	return HEADERS_COMPLETE
end

local function flush_url(self)
	if not self.url then
		local url = self.hm_parser:get_url()
		self.url = url
		return self.on_url(url)
	end
end

local function hm_HEADERS_COMPLETE(self, state)
	local hm_parser = self.hm_parser
	flush_url(self)
	local count = hm_parser:count_headers()
	local on_header = self.on_header
	for i=0,count-1 do
		local id, name, value = hm_parser:get_header(i)
		on_header(name, value)
	end
	self.on_headers_complete()
	if state >= BODY then
		return hm_BODY(self, state)
	end
	return HEADERS_COMPLETE
end

local function hm_HEADERS(self, state)
	if state >= HEADERS_COMPLETE then
		return hm_HEADERS_COMPLETE(self, state)
	end
	flush_url(self)
	return HEADERS -- last state.
end

local function hm_URL(self, state)
	if state >= HEADERS then
		return hm_HEADERS(self, state)
	end
	return URL
end

local function hm_STATUS_COMPLETE(self, state)
	if state >= URL then
		return hm_URL(self, state)
	end
	return STATUS_COMPLETE
end

local function hm_MESSAGE_BEGIN(self, state)
	self.on_message_begin()
	self.url = nil

	if state >= STATUS_COMPLETE then
		return hm_STATUS_COMPLETE(self, state)
	end
	return MESSAGE_BEGIN
end

local parser_states = {
	[NONE] = function()end,
	[MESSAGE_BEGIN] = hm_MESSAGE_BEGIN,
	[STATUS_COMPLETE] = hm_STATUS_COMPLETE,
	[URL] = hm_URL,
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
		-- flush all states from last to new.
		self.last_state = parser_states[last + 1](self, rc)
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
	return self:execute(tostring(buf) or '')
end

function meths:reset()
	self.last_state = 0
	self.hm_parser:reset()
end

local function create_parser(hm_parser, self)
	self.last_state = 0
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

