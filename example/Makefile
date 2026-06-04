
RTLAB_ROOT ?= /usr/opalrt

INCPATH  := $(RTLAB_ROOT)/common/include
LIBPATH  := -L$(RTLAB_ROOT)/common/bin
LIBS = -lOpalApi
OBJS = basic_example2.o
EXE  = basic_example2

# Intel - default compiler
RTLAB_INTEL_COMPILER ?= 1

# Intel Compiler support
ifeq ($(RTLAB_INTEL_COMPILER),1)
        CC = opicc
        LD = opicpc
# Gnu Compiler support
else
        CC = gcc
        LD = g++

        LIBPATH += -L$(RTLAB_ROOT)/common/lib/redhawk
        LIBS += -limf -lirc
endif

.SUFFIXES: .c .o

.c.o:
	$(CC) -c $(CFLAGS) -I$(INCPATH) $<

$(EXE): $(OBJS)
	$(CC) -o $@ $(OBJS) $(LIBPATH) $(LIBS)

clean:
	rm -f $(EXE) $(OBJS) 
