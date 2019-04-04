PROJECTS = daytime echo rsync
EXECUTABLES = rsync-server rsync-client
OUTPUTS = $(addprefix out/, $(EXECUTABLES))
CFLAGS = -g -lsctp

all: $(OUTPUTS)

out/%: %.c
	gcc  -o $(@) $(<) $(CFLAGS)
