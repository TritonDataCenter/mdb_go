DMOD_SRCS=	mdb_go.c
DMOD_LIBS=	-lc

DMOD_LDFLAGS = \
	-m64 \
	-shared \
	-nodefaultlibs \
	-Wl,-ztext \
	-Wl,-zdefs \
	-Wl,-M./mapfile-extern

ALWAYS_CFLAGS = \
        -fident \
        -fno-builtin \
        -nodefaultlibs \
        -Wall \
        -fno-inline-functions

USER_CFLAGS = \
        -finline \
        -gdwarf-2 \
        -std=gnu89

DMOD_CFLAGS = \
        $(ALWAYS_CFLAGS) \
        $(USER_CFLAGS) \
        -m64 \
        -fno-strict-aliasing \
        -fno-unit-at-a-time \
        -fno-optimize-sibling-calls \
        -O2 \
        -fno-inline-small-functions \
        -fno-inline-functions-called-once \
        -mtune=opteron \
        -ffreestanding \
        -fPIC

DMOD_CPPFLAGS = \
	-D_KERNEL \
	-DTEXT_DOMAIN="SUNW_OST_OSCMD" \
	-D_TS_ERRNO \
	-D_ELF64 \
	-Ui386 \
	-U__i386 \
	-I. \
	-Ihw

.PHONY: world
world: go.so
go.so: $(DMOD_SRCS)
	$(CC) $(DMOD_CPPFLAGS) $(DMOD_CFLAGS) $(DMOD_LDFLAGS) -o $@ \
		$(DMOD_SRCS) $(DMOD_LIBS)

.PHONY: clean
clean:
	rm -f go.so
