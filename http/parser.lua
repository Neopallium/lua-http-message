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

local null_callbacks = {
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

local parser_mt = {}
parser_mt.__index = parser_mt

function parser_mt:is_upgrade()
	return self.hm_parser:is_upgrade()
end

function parser_mt:should_keep_alive()
	return self.hm_parser:should_keep_alive()
end

function parser_mt:method()
	return self.hm_parser:method_str()
end

function parser_mt:version()
	local version = self.hm_parser:version()
	return (version / 65536), (version % 65536)
end

function parser_mt:status_code()
	return self.hm_parser:status_code()
end

function parser_mt:is_error()
	return self.hm_parser:is_error()
end

function parser_mt:error()
	return self.hm_parser:error(), self.hm_parser:error_name(), self.hm_parser:error_description()
end

local NEEDS_INPUT = states.NEEDS_INPUT
local ERROR = states.ERROR
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
	self:flush(rc)

	if needs_input then
		return len
	end

	return parser_execute(self, len)
end

function parser_mt:execute(data)
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

function parser_mt:execute_buffer(buf)
	return self:execute(tostring(buf) or '')
end

function parser_mt:reset()
	self.last_state = 0
	self.hm_parser:reset()
end

local function create_parser(hm_parser, cbs)
	local self = {
		hm_parser = hm_parser,
		cbs = cbs,
		last_state = 0,
	}
	-- create null callbacks for missing ones.
	for i=1,#callback_names do
		local name = callback_names[i]
		if not cbs[name] then cbs[name] = null_callbacks[name] end
	end

	local function consume_body(self)
		repeat
			local data = hm_parser:next_body()
			if not data then break end
			cbs.on_body(data)
		until false
	end

	local url
	local function flush_url(self)
		if not url then
			url = hm_parser:get_url()
			return cbs.on_url(url)
		end
	end

	local function head_complete(self)
		flush_url(self)
		local count = hm_parser:count_headers()
		for i=0,count-1 do
			local id, name, value = hm_parser:get_header(i)
			cbs.on_header(name, value)
		end
		cbs.on_headers_complete()
	end

	local handlers = {
		[states.MESSAGE_BEGIN] = function(self)
			url = nil
			return cbs.on_message_begin()
		end,
		[states.STATUS_COMPLETE] = function(self)
		end,
		[states.URL] = function(self)
		end,
		[states.HEADERS] = function(self)
			flush_url(self)
		end,
		[states.HEADERS_COMPLETE] = head_complete,
		[states.BODY] = consume_body,
		[states.MESSAGE_COMPLETE] = function(self)
			consume_body()
			-- Send on_body(nil) message to comply with LTN12
			cbs.on_body()
			cbs.on_message_complete()
			-- Prepare parser for next message.
			url = nil
			hm_parser:next_message()
			self.last_state = 0
		end,
	}
	-- states that should always be flushed.
	local BODY = states.BODY
	self.flush = function(self, new_state)
		local last = self.last_state
		if new_state == last then
			if new_state == BODY then
				-- keep flushing BODY state.
				handlers[new_state](self)
			end
			return
		end
		self.last_state = new_state
		-- flush all states from last to new.
		for s=last+1,new_state do
			handlers[s](self)
		end
	end
	return setmetatable(self, parser_mt)
end

module(...)

function request(cbs)
	return create_parser(hm.request(), cbs)
end

function response(cbs)
	return create_parser(hm.response(), cbs)
end

