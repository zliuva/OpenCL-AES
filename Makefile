# modify the following path before building
OPENCLDIR=	/System/Library/OpenSSL/opencl
ENGINEDIR=	/usr/lib/openssl/engines
OPENCLINC=	/System/Library/Frameworks/OpenCL.framework/Headers
OPENCLLIB=

ifeq ($(shell uname), Darwin)			# Apple
	LIBOPENCL=-framework OpenCL
else									# Linux
	LIBOPENCL=-L$(OPENCLLIB) -lOpenCL
endif

CC= 		gcc
LD=			cc

CPPFLAGS=	-I$(OPENCLINC)
CFLAGS=		-std=c99 -O3 -Wno-deprecated-declarations -DOPENCLDIR=\"$(OPENCLDIR)\"
LDFLAGS=	$(LIBOPENCL) -lcrypto

ifeq ($(shell uname), Darwin)			# Apple
	LIBNAME=	libOpenCL.1.dylib
else									# Linux
	LIBNAME=	libOpenCL.1.so
endif
LIBOBJ=		eng_opencl.o

SOURCES= 	eng_opencl.c
HEADERS= 	eng_opencl.h

BENCHMARKS=	Benchmark-OpenSSL Benchmark-OpenCL

all: $(LIBNAME) $(BENCHMARKS)

$(LIBNAME): $(LIBOBJ)
ifeq ($(shell uname), Darwin)			# Apple
	$(LD) -dynamiclib $(LDFLAGS) -o $(LIBNAME) $(LIBOBJ)
else									# Linux
	$(LD) -shared -Wl,-soname,$(LIBNAME) $(LDFLAGS) -o $(LIBNAME) $(LIBOBJ)
endif


$(LIBOBJ): $(SOURCES) $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $(LIBOBJ) $(SOURCES)

install: $(LIBNAME)
	sudo mkdir -p $(OPENCLDIR)
	sudo cp *.cl $(OPENCLDIR)/
	sudo mkdir -p $(ENGINEDIR)
	sudo cp $(LIBNAME) $(ENGINEDIR)/

clean:
	rm -f *.o *.dylib
	rm -f $(BENCHMARKS)

Benchmark-OpenSSL: benchmark.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $^

Benchmark-OpenCL: benchmark.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -DOPENCL_ENGINE=\"$(ENGINEDIR)/$(LIBNAME)\" $(LDFLAGS) -o $@ $^

# dependency
eng_opencl.c: eng_opencl.h
