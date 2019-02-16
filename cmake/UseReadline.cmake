function(eth_apply TARGET REQUIRED)	
	set (CMAKE_PREFIX_PATH "/usr/local/Cellar/readline/6.3.8" ${CMAKE_PREFIX_PATH})
	find_package (Readline 6.3.8)
	eth_show_dependency(READLINE readline)
	if (READLINE_FOUND)
		target_include_directories(${TARGET} SYSTEM PUBLIC ${READLINE_INCLUDE_DIRS})
		target_link_libraries(${TARGET} ${READLINE_LIBRARIES})
		target_compile_definitions(${TARGET} PUBLIC ETH_READLINE)
	elseif (NOT ${REQUIRED} STREQUAL "OPTIONAL")
		message(FATAL_ERROR "Readline library not found")
	endif()
endfunction()
