.PHONY: build test clean rebuild format quality-gate

BUILD_DIR := build

# Default target
build:
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && cmake .. -DCMAKE_BUILD_TYPE=Debug
	@cmake --build $(BUILD_DIR) -j$$(sysctl -n hw.logicalcpu 2>/dev/null || nproc 2>/dev/null || echo 4)

test: build
	@cd $(BUILD_DIR) && ctest --output-on-failure

clean:
	@rm -rf $(BUILD_DIR)

rebuild: clean build

format:
	@find src tests -name '*.cpp' -o -name '*.h' | xargs clang-format -i
	@echo "Formatted all source files."

quality-gate: format build test
	@echo "Quality gate passed."
