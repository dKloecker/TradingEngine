add_library(core_util INTERFACE
        overloaded_visitor.h)
target_include_directories(core_util INTERFACE ${CMAKE_SOURCE_DIR})
