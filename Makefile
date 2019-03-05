OUTPUTS = daytime-server daytime-client echo-client echo-server
CFLAGS = -g -lsctp

all: $(OUTPUTS)

%: %.c
	gcc  -o $(@) $(<) $(CFLAGS)
