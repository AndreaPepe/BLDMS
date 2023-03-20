cmake_minimum_required(VERSION 3.0.0 FATAL_ERROR)

project("BLDMS: Block-Level Data Management Service for Linux Kernel" VERSION 1.0.0 LANGUAGES C)
set(CMAKE_C_STANDARD 90)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Find kernel headers
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
find_package(KernelHeaders REQUIRED)


# find MODULE_LICENSE("GPL"), MODULE_AUTHOR() etc.
# thanks to "merseyviking" from stack overflow
add_definitions(-D__KERNEL__ -DMODULE)

set(srcs
        bldms.c
        file_ops.c
        dir_ops.c
        lib/usctm.c
        lib/lib/vtpmo.c
        )

set(hdrs
        include/bldms.h
        lib/include/vtpmo.h
        )
# this is needed in order for CLion IDE to provide syntax highlightning
# this is independent from the actual kernel object that is built
add_executable(dummy
        # add all *.h and *.c files here that # CLion should cover
        ${srcs}
        ${hdrs}
        )

# CLion IDE will find symbols from <linux/*>
target_include_directories("dummy" PRIVATE ${KERNELHEADERS_INCLUDE_DIRS})
