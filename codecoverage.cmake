
# Common compile options

#set(CMAKE_CXX_STANDARD 11)	# for test purpose
#set(CMAKE_CXX_STANDARD 14)	# for test purpose
#set(CMAKE_CXX_STANDARD 17)	# for test purpose

set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} ${CMAKE_C_FLAGS} -g -O0 --coverage")
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} ${CMAKE_C_FLAGS} -g -O0 --coverage")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} ${CMAKE_CXX_FLAGS} -g -O0 --coverage")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} ${CMAKE_CXX_FLAGS} -g -O0 --coverage")

