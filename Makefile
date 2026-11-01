# Builds and tests all three components.
#
#   make          build everything
#   make test     run every test suite
#   make demo     run each component's walkthrough
#   make clean    remove build output and generated .db files

COMPONENTS := 01-heap-file 02-bplus-tree 03-external-sort

all test demo clean:
	@for c in $(COMPONENTS); do \
	  echo "== $$c: make $@"; \
	  $(MAKE) --no-print-directory -C $$c $@ || exit 1; \
	done

.PHONY: all test demo clean
