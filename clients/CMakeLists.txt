option(CLIENT_BASIC "Build basic client" ON)

if(CLIENT_BASIC)
	add_subdirectory(basic/)
endif(CLIENT_BASIC)
