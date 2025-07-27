
# If you would like to build with target or environment specific configuration:
# 1. please prepare XXXX.cmake that includes build options
# 2. provide file information of XXXX.cmake like below to CMakeLists.txt
#    with -D option like below
#        $ cmake -D BUILD_TARGET=XXXX
#    or
#        $ make BUILDTARGET=XXXX
# 
#    common.cmake is default configurations
#    codecoverage.cmake is the configuration for code coverage of gcov
# 
BUILDTARGET?=common

# Debug or Release or ...
# BUILDTYPE=Debug
# BUILDTYPE=Release
# 
BUILDTYPE?=Release

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
BUILDIMPLTARGET?=all
BUILD_DIR?=build
#MAKEFILE_DIR := $(dir $(lastword $(MAKEFILE_LIST)))	# 相対パス名を得るならこちら。
MAKEFILE_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))	# 絶対パス名を得るならこちら。

CPUS=$(shell grep cpu.cores /proc/cpuinfo | sort -u | sed 's/[^0-9]//g')
JOBS=$(shell expr ${CPUS} + ${CPUS} / 2)

CMAKE_CONFIGURE_OPTS  = -DCMAKE_EXPORT_COMPILE_COMMANDS=ON	# for clang-tidy
CMAKE_CONFIGURE_OPTS += -DCMAKE_BUILD_TYPE=${BUILDTYPE}
CMAKE_CONFIGURE_OPTS += -DBUILD_TARGET=${BUILDTARGET}
CMAKE_CONFIGURE_OPTS += -DSANITIZER_TYPE=${SANITIZER_TYPE}
CMAKE_CONFIGURE_OPTS += -DBUILD_SHARED_LIBS=${BUILD_SHARED_LIBS}

all: configure-cmake
	set -e; \
	cd ${BUILD_DIR}; \
	cmake --build . -j ${JOBS} -v --target ${BUILDIMPLTARGET}

configure-cmake:
	set -e; \
	mkdir -p ${BUILD_DIR}; \
	cd ${BUILD_DIR}; \
	cmake ${CMAKE_CONFIGURE_OPTS} -G "Unix Makefiles" ${MAKEFILE_DIR}

test: build-test
	set -e; \
	cd ${BUILD_DIR}; \
	setarch $(uname -m) -R ctest -j ${JOBS} -v

sample: build-test
	build/sample/sample_of_constrained_any

build-test:
	make BUILDIMPLTARGET=build-test all

clean:
	-rm -fr ${BUILD_DIR}

coverage: clean
	set -e; \
	make BUILDTARGET=codecoverage BUILDTYPE=Debug test;  \
	cd ${BUILD_DIR}; \
	lcov -c -d . --include 'inc/*' --branch-coverage -o tmp.info; \
	genhtml --branch-coverage -o OUTPUT -p . -f tmp.info

profile: clean
	set -e; \
	make BUILDTARGET=gprof BUILDTYPE=Release build-test;  \
	cd ${BUILD_DIR}; \
	./test/test_performance_constrained_any; \
	gprof ./test/test_performance_constrained_any ./gmon.out > ./prof.out.txt

sanitizer:
	set -e; \
	for i in `seq 1 5`; do \
		make sanitizer.$$i.sanitizer; \
		echo $$i / 5 done; \
	done

sanitizer.%.sanitizer: clean
	make BUILDTARGET=common BUILDTYPE=Debug SANITIZER_TYPE=$* test

tidy-fix: configure-cmake
	find ./ -name '*.cpp'|grep -v googletest|grep -v ./build/|xargs -t -P${JOBS} -n1 clang-tidy -p=build --fix
	find ./ -name '*.cpp'|grep -v googletest|grep -v ./build/|xargs -t -P${JOBS} -n1 clang-format -i
	find ./ -name '*.hpp'|grep -v googletest|grep -v ./build/|xargs -t -P${JOBS} -n1 clang-format -i

tidy: configure-cmake
	find ./ -name '*.cpp'|xargs -t -P${JOBS} -n1 clang-tidy -p=build

.PHONY: test build sanitizer


