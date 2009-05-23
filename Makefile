# make && make install

# what to do
MODULES = nls_string
DATA_built = nls_string.sql
DOCS = README.nls_string
MODULE_VERSION = 8.02
MODULE_DIST = postgresql-nls-string

# postgresql extension framework
PGXS = $(shell pg_config --pgxs)
include $(PGXS)

dist:
	mkdir -p $(MODULE_DIST)-$(MODULE_VERSION) && \
	cp --preserve=timestamps $(shell cat MANIFEST) $(MODULE_DIST)-$(MODULE_VERSION) && \
	tar cvzf $(MODULE_DIST)-$(MODULE_VERSION).tar.gz $(MODULE_DIST)-$(MODULE_VERSION) && \
	mv $(MODULE_DIST)-$(MODULE_VERSION).tar.gz .. && \
	rm -rf $(MODULE_DIST)-$(MODULE_VERSION)


