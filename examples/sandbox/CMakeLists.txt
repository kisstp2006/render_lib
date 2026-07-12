add_executable(sandbox main.cpp)
target_link_libraries(sandbox PRIVATE engine)

if(MSVC)
    target_compile_options(sandbox PRIVATE /W4 /permissive-)
else()
    target_compile_options(sandbox PRIVATE -Wall -Wextra)
endif()
