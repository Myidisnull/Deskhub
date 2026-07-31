ifeq ($(OS),Windows_NT)
CODESTYLE := powershell -NoProfile -ExecutionPolicy Bypass -File scripts\codestyle.ps1
CHECKFLAG := -Check
ONLYFLAG  := -Only
else
CODESTYLE := scripts/codestyle.sh
CHECKFLAG := --check
ONLYFLAG  := --only
endif

format:
	@$(CODESTYLE)

format-cpp:
	@$(CODESTYLE) $(ONLYFLAG) cpp

format-kotlin:
	@$(CODESTYLE) $(ONLYFLAG) kotlin

format-swift:
	@$(CODESTYLE) $(ONLYFLAG) swift

lint:
	@$(CODESTYLE) $(CHECKFLAG)

lint-cpp:
	@$(CODESTYLE) $(CHECKFLAG) $(ONLYFLAG) cpp

lint-kotlin:
	@$(CODESTYLE) $(CHECKFLAG) $(ONLYFLAG) kotlin

lint-swift:
	@$(CODESTYLE) $(CHECKFLAG) $(ONLYFLAG) swift

.PHONY: format format-cpp format-kotlin format-swift lint lint-cpp lint-kotlin lint-swift
