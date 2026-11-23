EE_BIN  = pokegold.elf
EE_OBJS = main.o
EE_INCS = -I$(GSKIT)/include
EE_LIBS = -L$(GSKIT)/lib -lgskit -ldmakit -lpad -lgraph -lc

all: $(EE_BIN)

main.o: src/main.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c src/main.c -o main.o

clean:
	rm -f *.o *.elf

assets:
	python3 tools/build_assets.py

include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal