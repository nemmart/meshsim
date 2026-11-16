# doccheck.mk, included by the Makefile: the documents' checks (tools/doccheck.sh).
#   make doccheck            run them
#   make check DOCCHECK=1    also run them first in make check; off by default
DOCCHECK ?= 0
.PHONY: doccheck
doccheck:
	@bash tools/doccheck.sh
ifeq ($(DOCCHECK),1)
check: doccheck
endif
