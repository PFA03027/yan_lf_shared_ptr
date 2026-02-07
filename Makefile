
# If you would like to build with target or environment specific configuration:
# 1. please prepare XXXX.cmake that includes build options
# 2. provide file information of XXXX.cmake like below to CMakeLists.txt
#    with -D option like below
#        $ cmake -D BUILD_CONFIG=XXXX
#    or
#        $ make BUILD_CONFIG=XXXX
# 
#    common.cmake is default configurations
#    codecoverage.cmake is the configuration for code coverage of gcov
# 
BUILD_CONFIG?=common

# Debug or Release or ...
# BUILD_TYPE=Debug
# BUILD_TYPE=Release
# 
BUILD_TYPE?=Release

# Select build library type
# BUILD_SHARED_LIBS=OFF -> static library
# BUILD_SHARED_LIBS=ON -> shared library
BUILD_SHARED_LIBS?=ON

# Sanitizer test option:
# SANITIZER_TYPE= 1 ~ 20 or ""
#
# Please see common.cmake for detail
# 
SANITIZER_TYPE?=

##### internal variable
BUILD_DIR?=build
#MAKEFILE_DIR := $(dir $(lastword $(MAKEFILE_LIST)))	# 相対パス名を得るならこちら。
MAKEFILE_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))	# 絶対パス名を得るならこちら。

CPUS=$(shell grep cpu.cores /proc/cpuinfo | sort -u | sed 's/[^0-9]//g')
JOBS=$(shell expr ${CPUS} + ${CPUS} / 2)

CMAKE_CONFIGURE_OPTS  = -DCMAKE_EXPORT_COMPILE_COMMANDS=ON	# for clang-tidy
CMAKE_CONFIGURE_OPTS += -DCMAKE_BUILD_TYPE=${BUILD_TYPE}
CMAKE_CONFIGURE_OPTS += -DBUILD_CONFIG=${BUILD_CONFIG}
CMAKE_CONFIGURE_OPTS += -DSANITIZER_TYPE=${SANITIZER_TYPE}
CMAKE_CONFIGURE_OPTS += -DBUILD_SHARED_LIBS=${BUILD_SHARED_LIBS}

all: configure-cmake
	cmake --build ${BUILD_DIR} -j ${JOBS} -v --target all

test: build-test
	setarch $(uname -m) -R ctest --test-dir ${BUILD_DIR} -j ${JOBS} -v

build-test: configure-cmake
	cmake --build ${BUILD_DIR} -j ${JOBS} -v --target build-test

configure-cmake:
	cmake -S . -B ${BUILD_DIR} -G "Unix Makefiles" ${CMAKE_CONFIGURE_OPTS}

clean:
	-cmake --build ${BUILD_DIR} -j ${JOBS} -v --target clean

clean-all:
	-rm -fr ${BUILD_DIR}

# This is inatall command example
install: all
	DESTDIR=/tmp/install-test cmake --install ${BUILD_DIR} --prefix /opt/xxx

coverage: clean
	set -e; \
	make BUILD_CONFIG=codecoverage BUILD_TYPE=Debug test;  \
	cd ${BUILD_DIR}; \
	lcov -c -d . --include 'inc/*' --branch-coverage -o tmp.info; \
	genhtml --branch-coverage -o OUTPUT -p . -f tmp.info

profile: clean
	set -e; \
	make BUILD_CONFIG=gprof BUILD_TYPE=Release build-test;  \
	cd ${BUILD_DIR}; \
	./test/test_typeT_lf_heap --gtest_filter=YanLFSharedPtrWithTypedPoolHeapHighLoad.CanComparePerformanceWithRcSharedPtr; \
	gprof ./test/test_typeT_lf_heap ./gmon.out > ./prof.out.txt

sanitizer:
	set -e; \
	for i in `seq 1 5`; do \
		make sanitizer.$$i.sanitizer; \
		echo $$i / 5 done; \
	done

sanitizer.%.sanitizer: clean
	make BUILD_CONFIG=common BUILD_TYPE=Debug SANITIZER_TYPE=$* test

tidy-fix: configure-cmake
	find ./ -name '*.cpp'|grep -v googletest|grep -v ./build/|xargs -t -P${JOBS} -n1 clang-tidy -p=build --fix
	find ./ -name '*.cpp'|grep -v googletest|grep -v ./build/|xargs -t -P${JOBS} -n1 clang-format -i
	find ./ -name '*.hpp'|grep -v googletest|grep -v ./build/|xargs -t -P${JOBS} -n1 clang-format -i

tidy: configure-cmake
	find ./ -name '*.cpp'|xargs -t -P${JOBS} -n1 clang-tidy -p=build

.PHONY: test build-test configure-cmake clean-all coverage profile sanitizer tidy tidy-fix

perf: build-test
	${BUILD_DIR}/test/test_typeT_lf_heap --gtest_filter=YanLFSharedPtrWithTypedPoolHeapHighLoad.CanComparePerformanceWithRcSharedPtr:YanLFSharedPtrWithTypedPoolHeapHighLoad.CanComparePerformanceWithStdSharedPtr

