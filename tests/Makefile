# SPDX-License-Identifier: GPL-3.0-or-later
# The sync code's tests, built with the PC's compiler:   make test
# The same on a big-endian CPU, like the PS3's, under qemu:
#   make test CROSS=powerpc64-linux-gnu- RUN=qemu-ppc64 JSON_C=<json-c built for it> \
#        MBEDTLS=<mbedTLS built for it>

CROSS	?=
RUN	?=
# Where json-c's and mbedTLS's include and lib directories are.
JSON_C	?= /usr
MBEDTLS	?= /usr

CXX	:= $(CROSS)g++
SHARED	:= sync/SyncState.cpp sync/SaveScanner.cpp sync/Sync.cpp sync/Tar.cpp sync/ParamSfo.cpp \
	   sync/CloudSaves.cpp sync/GameName.cpp sync/SaveArchives.cpp sync/SaveBrowser.cpp \
	   sync/ParamPfd.cpp sync/Resign.cpp json/Json.cpp \
	   app/Files.cpp app/DebugLog.cpp app/CloudSaveText.cpp app/XRegistry.cpp
SOURCES	:= $(wildcard *.cpp) $(addprefix ../source/,$(SHARED))
# include/ goes first: its headers stand in for the console's.
CXXFLAGS	:= -std=c++17 -O1 -g -Wall -Wextra -Iinclude -I../source -I$(JSON_C)/include \
	   -I$(MBEDTLS)/include
LDLIBS	:= -L$(JSON_C)/lib -ljson-c -L$(MBEDTLS)/lib -lmbedcrypto
# qemu runs a static program without the target's libraries.
ifneq ($(CROSS),)
LDFLAGS	:= -static
endif

.PHONY: test clean

test: run-tests
	TZ=UTC $(RUN) ./run-tests

run-tests: $(SOURCES) $(wildcard *.h ../source/*/*.h)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(SOURCES) $(LDLIBS) -o $@

clean:
	rm -f run-tests
