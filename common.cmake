

if("${SANITIZER_TYPE}" EQUAL "1")
 set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DTEST_ENABLE_ADDRESSSANITIZER -fno-omit-frame-pointer -fsanitize=address")	# for test purpose
 set(CMAKE_EXE_LINKER_FLAGS_INIT "${CMAKE_EXE_LINKER_FLAGS_INIT} -fsanitize=address")	# for test purpose
elseif("${SANITIZER_TYPE}" EQUAL "2")
 set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-omit-frame-pointer -fstack-protector-strong")	# for test purpose
elseif("${SANITIZER_TYPE}" EQUAL "3")
 set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DTEST_ENABLE_LEAKSANITIZER -fno-omit-frame-pointer -fsanitize=leak")	# for test purpose. Should NOT set ALCONCURRENT_CONF_USE_MALLOC_ALLWAYS_FOR_DEBUG_WITH_SANITIZER
elseif("${SANITIZER_TYPE}" EQUAL "4")
 set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DTEST_ENABLE_THREADSANITIZER -O2 -fno-omit-frame-pointer -fsanitize=thread")	# for test purpose. thread sanitizer needs -O1/-O2. Unfortunately this finds false positive.
elseif("${SANITIZER_TYPE}" EQUAL "5")
 set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}  -DTEST_ENABLE_UBSANITIZER -fno-omit-frame-pointer -fsanitize=undefined -fno-sanitize-recover=all")	# for test purpose
else()
 # no sanitizer option
 message("[Sanitizer] No Sanitizer")
endif()


set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} ${CMAKE_C_FLAGS} -g")
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} ${CMAKE_C_FLAGS} -O2")
# set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} ${CMAKE_CXX_FLAGS} -g")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} ${CMAKE_CXX_FLAGS} -g -rdynamic")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} ${CMAKE_CXX_FLAGS} -O2")

