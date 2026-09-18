# exifreader - Image EXIF Information-Tree Reader
#   make            build build/exifreader
#   make test       build and run the end-to-end suite
#   make debug      ASan + UBSan build (then run the suite by hand)
#   make ubsan      UBSan-only build, for kernels where ASan cannot start
#   make fuzz       libFuzzer campaign over the parsers (needs clang)
#   make install    install into $(DESTDIR)$(PREFIX)
#   make format     reformat src/ with clang-format
#   make dist       source tarball of HEAD (requires a git checkout)
#   make help       list the documented targets

CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic -Wshadow -Wconversion
LDFLAGS  ?=

PREFIX       ?= /usr/local
DESTDIR      ?=
CLANG_FORMAT ?= clang-format
VERSION      := $(shell sed -n 's/.*kVersion = "\([^"]*\)".*/\1/p' src/main.cpp)

# libFuzzer needs clang: use "make fuzz CXX=clang++".
FUZZ_SANITIZERS ?= fuzzer,address
FUZZ_SECONDS    ?= 60
FUZZ_CORPUS     ?= tests/fixtures
FUZZ_BIN        := build/fuzz_exif
# Recursive on purpose: SRC is defined further down.
FUZZ_LIB         = $(filter-out src/main.cpp,$(SRC))

SRC      := $(wildcard src/*.cpp)
HEADERS  := $(wildcard src/*.hpp)
OBJ      := $(SRC:src/%.cpp=build/obj/%.o)
BIN      := build/exifreader

.PHONY: all clean test fixtures run debug ubsan fuzz install uninstall format format-check dist help

all: $(BIN)

$(BIN): $(OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJ) $(LDFLAGS)

build/obj/%.o: src/%.cpp $(HEADERS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

debug: CXXFLAGS := -std=c++17 -O0 -g -fsanitize=address,undefined -Wall -Wextra -Wpedantic
debug: clean all

# UBSan without ASan. Use this on kernels whose user virtual address space is too
# small for the ASan shadow mapping (some Android/Termux kernels abort at startup
# with "heap size ... exceeds max user virtual address").
ubsan: CXXFLAGS := -std=c++17 -O0 -g -fsanitize=undefined -fno-sanitize-recover=all -Wall -Wextra -Wpedantic
ubsan: clean all

fixtures:
	python3 tests/make_fixtures.py tests/fixtures

# Coverage-guided fuzzing of the parsers. Needs clang (libFuzzer). The corpus
# starts from the generated fixtures; new interesting inputs are written to
# build/corpus, which is git-ignored.
fuzz: $(FUZZ_BIN)
	@mkdir -p build/corpus
	@test -f tests/fixtures/sample.jpg || $(MAKE) fixtures
	$(FUZZ_BIN) -max_total_time=$(FUZZ_SECONDS) $(FUZZ_CORPUS) build/corpus

$(FUZZ_BIN): fuzz/fuzz_exif.cc $(SRC) $(HEADERS)
	@mkdir -p $(dir $@)
	$(CXX) -std=c++17 -g -O1 -fno-omit-frame-pointer -fsanitize=$(FUZZ_SANITIZERS) \
	  -Isrc -o $@ fuzz/fuzz_exif.cc $(FUZZ_LIB)

test: all
	tests/run_tests.sh

run: all
	./$(BIN) $(ARGS)

install: all
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 0755 $(BIN) $(DESTDIR)$(PREFIX)/bin/exifreader
	install -d $(DESTDIR)$(PREFIX)/share/doc/exifreader
	install -m 0644 README.md CHANGELOG.md LICENSE $(DESTDIR)$(PREFIX)/share/doc/exifreader/

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/exifreader
	rm -rf $(DESTDIR)$(PREFIX)/share/doc/exifreader

format:
	$(CLANG_FORMAT) -i $(SRC) $(HEADERS)

# Advisory: the tree predates .clang-format, so pre-existing violations are
# expected. Keep the files you touch clean; CI treats this target as advisory.
format-check:
	$(CLANG_FORMAT) --dry-run --Werror $(SRC) $(HEADERS)

dist:
	@git rev-parse --git-dir >/dev/null 2>&1 || { echo "dist: not a git repository" >&2; exit 1; }
	mkdir -p dist
	git archive --format=tar.gz --prefix=exifreader-$(VERSION)/ \
	  -o dist/exifreader-$(VERSION)-src.tar.gz HEAD
	@if command -v sha256sum >/dev/null 2>&1; then \
	  (cd dist && sha256sum exifreader-$(VERSION)-src.tar.gz > exifreader-$(VERSION)-src.tar.gz.sha256); \
	else \
	  (cd dist && shasum -a 256 exifreader-$(VERSION)-src.tar.gz > exifreader-$(VERSION)-src.tar.gz.sha256); \
	fi
	@ls -l dist

help:
	@sed -n 's/^#   //p' $(firstword $(MAKEFILE_LIST))

clean:
	rm -rf build/obj $(BIN)
