#This is a hack to pass arguments to the run command and probably only 
#works with gnu make. 
ifeq (run,$(firstword $(MAKECMDGOALS)))
  # use the rest as arguments for "run"
  RUN_ARGS := $(wordlist 2,$(words $(MAKECMDGOALS)),$(MAKECMDGOALS))
  # ...and turn them into do-nothing targets
  $(eval $(RUN_ARGS):;@:)
endif


all: FTPServer

#The following lines contain the generic build options
CC=gcc
CPPFLAGS=
CFLAGS=-g -Werror-implicit-function-declaration

#List all the .o files here that need to be linked 
OBJS=FTPServer.o usage.o dir.o netbuffer.o util.o

usage.o: usage.c usage.h

dir.o: dir.c dir.h

netbuffer.o: netbuffer.c netbuffer.h

util.o: util.c util.h

FTPServer.o: FTPServer.c dir.h usage.h util.h

FTPServer: $(OBJS) 
	$(CC) -o FTPServer $(OBJS) 

clean:
	rm -f *.o
	rm -f FTPServer

### ignore the below, for the hack above
.PHONY: run
run: FTPServer  
	./FTPServer $(RUN_ARGS)
