BUILD_DIR ?= build
PREFIX ?= $(HOME)/.local
CMAKE_FLAGS ?=
.PHONY: build install format format-check test
build:
	cmake -S . -B $(BUILD_DIR) $(CMAKE_FLAGS)
	cmake --build $(BUILD_DIR)
install: build
	cmake --install $(BUILD_DIR) --prefix $(PREFIX)
test: build
	ctest --test-dir $(BUILD_DIR) --output-on-failure
format:
	find cpp tests -type f \( -name '*.cpp' -o -name '*.h' \) -exec clang-format -i {} +
format-check:
	find cpp tests -type f \( -name '*.cpp' -o -name '*.h' \) -exec clang-format --dry-run --Werror {} +
