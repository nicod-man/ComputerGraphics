add_subdirectory(yocto)
add_subdirectory(yocto_pathtrace)

if(YOCTO_OPENGL)
add_subdirectory(yocto_gui)
endif(YOCTO_OPENGL)
