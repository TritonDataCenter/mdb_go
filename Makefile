PROTO_AREA=	${HOME}/smartos-live/proto
KERNEL_SOURCE=	${HOME}/smartos-live/projects/illumos
MDB_SOURCE=	$(KERNEL_SOURCE)/usr/src/cmd/mdb
DMOD_SRCS=	mdb_go.c
DMOD_LIBS=	-lc

DMOD_LDFLAGS = \
	-m64 \
	-shared \
	-nodefaultlibs \
	-Wl,-M$(KERNEL_SOURCE)/usr/src/common/mapfiles/common/map.pagealign \
	-Wl,-M$(KERNEL_SOURCE)/usr/src/common/mapfiles/common/map.noexdata \
	-Wl,-ztext \
	-Wl,-zdefs \
	-Wl,-M$(MDB_SOURCE)/common/modules/conf/mapfile-extern \
	-L$(PROTO_AREA)/lib \
	-L$(PROTO_AREA)/usr/lib

ALWAYS_CFLAGS = \
        -fident \
        -fno-builtin \
        -nodefaultlibs \
        -Wall \
        -fno-inline-functions

#        -Werror 
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
	-isystem $(PROTO_AREA)/usr/include \
	-I. \
	-Ihw

.PHONY: world
world: mdb_go.so
mdb_go.so: $(DMOD_SRCS)
	$(CC) $(DMOD_CPPFLAGS) $(DMOD_CFLAGS) $(DMOD_LDFLAGS) -o $@ \
		$(DMOD_SRCS) $(DMOD_LIBS)

.PHONY: clean
clean:
	rm -f mdb_go.so
