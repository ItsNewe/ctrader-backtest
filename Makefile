# ctrader-backtest — top-level task runner
# Usage: make help

BUILD_DIR  ?= build
BUILD_TYPE ?= Release
JOBS       ?= $(shell nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

.PHONY: build clean rebuild test api dashboard dev install lint help

build: ## Build C++ project (CMake, Release, parallel)
	@mkdir -p $(BUILD_DIR)
	cd $(BUILD_DIR) && cmake .. -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) && cmake --build . -j$(JOBS)

clean: ## Remove build directory
	rm -rf $(BUILD_DIR)

rebuild: clean build ## Clean + build

test: ## Build and run all validation tests
	@mkdir -p $(BUILD_DIR)
	cd $(BUILD_DIR) && cmake .. -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) > /dev/null
	@echo "=== Building and running validation tests ==="
	@passed=0; failed=0; skipped=0; \
	for t in $$(cmake --build $(BUILD_DIR) --target help 2>/dev/null | grep -oE 'test_[a-z_]+' | sort -u); do \
		if cmake --build $(BUILD_DIR) --target $$t -j$(JOBS) > /dev/null 2>&1; then \
			echo "--- $$t ---"; \
			if $(BUILD_DIR)/validation/$$t; then \
				passed=$$((passed + 1)); \
			else \
				failed=$$((failed + 1)); \
			fi; \
		else \
			echo "--- $$t (build failed, skipped) ---"; \
			skipped=$$((skipped + 1)); \
		fi; \
	done; \
	echo ""; \
	echo "$$passed passed, $$failed failed, $$skipped skipped"; \
	[ $$failed -eq 0 ]

test-%: ## Build and run a single test (e.g. make test-margin_swap)
	@mkdir -p $(BUILD_DIR)
	cd $(BUILD_DIR) && cmake .. -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) && cmake --build . --target test_$* -j$(JOBS)
	$(BUILD_DIR)/validation/test_$*

api: ## Start FastAPI backend (port 8000)
	uv run uvicorn api.main:app --reload --host 0.0.0.0 --port 8000

dashboard: ## Start Vite dev server (port 5173)
	cd dashboard && npm run dev

dev: ## Start api + dashboard in parallel
	@echo "Starting api (port 8000) and dashboard (port 5173)..."
	@uv run uvicorn api.main:app --reload --host 0.0.0.0 --port 8000 & echo "api PID: $$!"
	@cd dashboard && npm run dev & echo "dashboard PID: $$!"
	@wait

install: ## Install Python + Node dependencies
	uv sync
	cd dashboard && npm install

lint: ## Run ESLint on dashboard
	cd dashboard && npm run lint

help: ## Show this help
	@grep -E '^[a-zA-Z_%-]+:.*## ' $(MAKEFILE_LIST) | \
		awk -F':.*## ' '{ printf "  \033[36m%-20s\033[0m %s\n", $$1, $$2 }'

# Default target
.DEFAULT_GOAL := help
