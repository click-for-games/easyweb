cmake_minimum_required(VERSION 3.10)
project(easyweb C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

if(MSVC)
  add_compile_options(/W3)
else()
  add_compile_options(-Wall -Wextra)
endif()

set(EASYWEB_CORE
  src/util.c
  src/lexer.c
  src/ast.c
  src/parser.c
  src/value.c
  src/interp.c
  src/jsgen.c
  src/kid.c
  src/render.c
  src/http.c
  src/server.c
  src/pkg.c
  src/packages.c
  src/highlight.c
  src/json.c
  src/webhelp.c
)

add_library(easyweb_core STATIC ${EASYWEB_CORE})

add_executable(easyweb src/main.c)
target_link_libraries(easyweb easyweb_core)

add_executable(webhelp src/webhelp_main.c)
target_link_libraries(webhelp easyweb_core)

if(UNIX)
  find_library(MATH_LIBRARY m)
  if(MATH_LIBRARY)
    target_link_libraries(easyweb_core ${MATH_LIBRARY})
  endif()
endif()

if(WIN32)
  target_link_libraries(easyweb_core ws2_32)
endif()

install(TARGETS easyweb webhelp RUNTIME DESTINATION bin)
