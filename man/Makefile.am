man_MANS = shairport-sync.7

if HAVE_XMLTOMAN
all-local: shairport-sync.html

shairport-sync.7: shairport-sync.7.xml
	xmltoman $< > $@

shairport-sync.html: shairport-sync.7.xml
	xmlmantohtml $< > $@
endif
