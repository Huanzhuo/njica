all: release

TARGET_DIR = build

release:
	# Disable asserts
	meson build -Db_ndebug=true --buildtype=release
	cd $(TARGET_DIR) && ninja

debug:
	if [ ! -d $(TARGET_DIR) ]; then mkdir $(TARGET_DIR); fi
	meson build --buildtype=debug
	cd $(TARGET_DIR) && ninja

clean:
	rm -rf $(TARGET_DIR)

cppcheck:
	@echo "* Run cppcheck: "
	cd $(TARGET_DIR) && ninja cppcheck

scan-build:
	@echo "* Run Clang static analyzer:" 
	cd $(TARGET_DIR) && ninja scan-build

rebuild:
	cd $(TARGET_DIR) && ninja

test:
	@echo "* Run unit tests:"
	cd $(TARGET_DIR) && meson test --print-errorlogs

.PHONY: release debug clean cppcheck scan-build

PYTHON ?= python3
PYSRC = ./*.py
CHECKERRIGNORE=W503,E501,C0330,F401,F402,E402,E203,F841

codecheck: $(PYSRC)
	@echo "*** Running checks for code quality"
	$(PYTHON) -m flake8 --ignore=W503,E501,C0330,E402,F401 --max-complexity 10 $(PYSRC)

errcheck: $(PYSRC)
	@echo "*** Running checks for errors only"
	$(PYTHON) -m flake8 --ignore=$(CHECKERRIGNORE) $(PYSRC)

.PHONY: codecheck errcheck
