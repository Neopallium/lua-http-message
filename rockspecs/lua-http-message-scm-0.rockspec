#!/usr/bin/env lua

package = 'lua-http-message'
version = 'scm-0'
source = {
	url = 'git://github.com/Neopallium/lua-http-message.git'
}
description = {
	summary  = "A HTTP protocol parser",
	detailed = [[
A HTTP protocol parser.
]],
	homepage = 'http://github.com/Neopallium/lua-http-message',
	license  = 'MIT',
}
dependencies = {
	'lua >= 5.1'
}
build = {
	type = 'cmake',
	variables = {
		INSTALL_CMOD      = "$(LIBDIR)",
		CMAKE_BUILD_TYPE  = "$(CMAKE_BUILD_TYPE)",
		["CFLAGS:STRING"] = "$(CFLAGS)",
	},
}
