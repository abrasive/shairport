ifdef CONFIG_DYNAMIC_PLUGINS

PLUGIN_OBJS = $(subst .c,.o,$(PLUGIN_SRCS))
PLUGIN_TARGET = $(PLUGIN_DIR)/$(PLUGIN_NAME).so

$(PLUGIN_OBJS): %.o: %.c
	$(CC) $(CFLAGS) -fPIC -c -o $@ $<

$(PLUGIN_TARGET): $(PLUGIN_OBJS)
	$(CC) $(LDFLAGS) -shared -o $@ $^

clean_$(PLUGIN_NAME):
	rm -f $(PLUGIN_OBJS) $(PLUGIN_TARGET)

install_$(PLUGIN_NAME): $(PLUGINS_PATH) $(PLUGIN_TARGET)
	install -c $(PLUGIN_TARGET) $(PLUGINS_PATH)/$(notdir $(PLUGIN_TARGET))

all: $(PLUGIN_TARGET)
clean: clean_$(PLUGIN_NAME)
install: install_$(PLUGIN_NAME)

else
    SRCS += $(PLUGIN_SRCS)
endif
