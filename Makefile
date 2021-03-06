PROJECTS = daytime echo rsync
EXECUTABLES = $(foreach project, $(PROJECTS), $(project)-server $(project)-client)
OUTPUTS = $(addprefix out/, $(EXECUTABLES))
CFLAGS = -g -lsctp
LDFLAGS = -lpthread

.PHONY: all clean

all: $(OUTPUTS)
clean:
	rm $(OUTPUTS)

out/%: %.c
	gcc  -o $(@) $(<) $(CFLAGS) $(LDFLAGS)
