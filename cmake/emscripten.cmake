# See LICENSE file in the project root for license information.

# enable C++ exception catching
# https://emscripten.org/docs/optimizing/Optimizing-Code.html#c-exceptions
add_compile_options(-fexceptions)
add_link_options(-fexceptions)
add_link_options("SHELL:-s NO_DISABLE_EXCEPTION_CATCHING")

# enable demangling of C++ stack traces
# https://emscripten.org/docs/porting/Debugging.html
add_link_options("SHELL:-s DEMANGLE_SUPPORT=1")

# allows amount of memory used to change
# https://emscripten.org/docs/optimizing/Optimizing-Code.html#memory-growth
add_link_options("SHELL:-s ALLOW_MEMORY_GROWTH=1")

# get more information on undefined symbols
add_link_options("SHELL:-s LLD_REPORT_UNDEFINED")

# disable the HTML minification
add_link_options("SHELL:-s MINIFY_HTML=0")

# enable assertions
add_link_options("SHELL:-s ASSERTIONS=1")

# generate HTML files
set(CMAKE_EXECUTABLE_SUFFIX ".html")
