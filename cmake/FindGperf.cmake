# - Find gperf
# This module looks for gperf. This module defines the 
# following values:
#  GPERF_EXECUTABLE: the full path to the gperf tool.
#  GPERF_FOUND: True if gperf has been found.

INCLUDE(FindCygwin)

FIND_PROGRAM(GPERF_EXECUTABLE
    gperf
    ${CYGWIN_INSTALL_PATH}/bin
)

# handle the QUIETLY and REQUIRED arguments and set GPERF_FOUND to TRUE if 
# all listed variables are TRUE
INCLUDE(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(Gperf DEFAULT_MSG GPERF_EXECUTABLE)

MARK_AS_ADVANCED(GPERF_EXECUTABLE)

macro(GenGperfFiles _src_files_var)
	set(_new_src_files)
	foreach(_src_file ${${_src_files_var}})
		if(_src_file MATCHES ".gperf")
			string(REGEX REPLACE ".gperf" ".h" _src_file_out ${_src_file})
			add_custom_command(OUTPUT ${_src_file_out}
				COMMAND ${GPERF_EXECUTABLE} --output-file=${CMAKE_CURRENT_BINARY_DIR}/${_src_file_out} ${_src_file}
				WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
				DEPENDS ${_src_file}
			)
			set_source_files_properties(${_src_file_out} PROPERTIES GENERATED TRUE)
			set(_new_src_files ${_new_src_files} ${_src_file_out})
		else(_src_file MATCHES ".gperf")
			set(_new_src_files ${_new_src_files} ${_src_file})
		endif(_src_file MATCHES ".gperf")
	endforeach(_src_file)
	set(${_src_files_var} ${_new_src_files})
endmacro(GenGperfFiles _src_files_var)

