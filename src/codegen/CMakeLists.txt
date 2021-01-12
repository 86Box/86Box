if(DYNAREC)
	add_library(dynarec OBJECT codegen.c codegen_ops.c)

	if(CMAKE_TARGET_ARCHITECTURES STREQUAL "i386")
		target_sources(dynarec PRIVATE codegen_x86.c
			codegen_accumulate_x86.c)
	elseif(CMAKE_TARGET_ARCHITECTURES STREQUAL "x86_64")
		target_sources(dynarec PRIVATE codegen_x86-64.c
			codegen_accumulate_x86-64.c)
	else()
		message(SEND_ERROR 
			"Dynarec is incompatible with target platform "
			${CMAKE_TARGET_ARCHITECTURES})
	endif()

	target_link_libraries(86Box dynarec cgt)
endif()