# chain.mk: the rules of the pipeline's host-side files' readers (tests/chain.h), included by the Makefile.
CHAIN_LINK := $(B)/tests/chain.o $(B)/src/units/file_store.o
$(B)/test_chain: $(CHAIN_LINK)
