.PHONY: build test clean rebuild format format-check quality-gate wasm wasm-configure wasm-clean serve demo

BUILD_DIR := build
WASM_BUILD_DIR := build-wasm

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
	yarn lint:fix
	@echo "Formatted all source files."
	# scripts/*.py (bachlib/) has no configured formatter (no ruff/black config
	# anywhere in this repo) - left unformatted.

format-check:
	@find src tests -name '*.cpp' -o -name '*.h' | xargs clang-format --dry-run --Werror
	yarn lint

quality-gate: format build test
	@echo "Quality gate passed."

wasm-configure:
	emcmake cmake -B $(WASM_BUILD_DIR) -DBUILD_WASM=ON -DCMAKE_BUILD_TYPE=Release

wasm: wasm-configure
	cmake --build $(WASM_BUILD_DIR) --parallel
	@ls -lh dist/*.wasm dist/*.js 2>/dev/null || echo "WASM files not found"
	yarn build:js

wasm-clean:
	rm -rf $(WASM_BUILD_DIR)
	rm -rf dist/*.wasm dist/*.js

serve:
	@echo "Starting demo server at http://localhost:8080/demo/"
	@echo "Press Ctrl+C to stop"
	python3 -m http.server 8080

demo: wasm serve
