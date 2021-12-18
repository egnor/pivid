ifndef FFINSTALL
FFINSTALL=/usr
endif
CFLAGS=-I$(FFINSTALL)/include/arm-linux-gnueabihf -I/usr/include/libdrm
LDFLAGS=-L$(FFINSTALL)/lib/arm-linux-gnueabihf
LDLIBS=-lavcodec -lavfilter -lavutil -lavformat -ldrm -lpthread

hello_drmprime: hello_drmprime.o drmprime_out.o

