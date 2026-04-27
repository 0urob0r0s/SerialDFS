CC     = wcl
CFLAGS = -bt=dos -ms -0 -os -s -d0 -we -wx -Isrc
OUTDIR = build

all: $(OUTDIR)/HELLO.EXE $(OUTDIR)/SERPING.EXE $(OUTDIR)/CRCFUZZ.EXE $(OUTDIR)/SERDIR.EXE $(OUTDIR)/SERTYPE.EXE $(OUTDIR)/SERCOPY.EXE $(OUTDIR)/SERDEL.EXE $(OUTDIR)/SERREN.EXE $(OUTDIR)/SERDFS.EXE

hello: $(OUTDIR)/HELLO.EXE

serping: $(OUTDIR)/SERPING.EXE

crcfuzz: $(OUTDIR)/CRCFUZZ.EXE

serdir: $(OUTDIR)/SERDIR.EXE

sertype: $(OUTDIR)/SERTYPE.EXE

$(OUTDIR)/HELLO.EXE: src/hello.c
	$(CC) $(CFLAGS) -fe=$(OUTDIR)/HELLO.EXE src/hello.c

$(OUTDIR)/SERPING.EXE: tools/serping.c src/seruart.c src/serframe.c src/seruart.h src/serframe.h
	$(CC) $(CFLAGS) -fe=$(OUTDIR)/SERPING.EXE tools/serping.c src/seruart.c src/serframe.c

$(OUTDIR)/CRCFUZZ.EXE: tools/crcfuzz.c src/serframe.c src/serframe.h
	$(CC) $(CFLAGS) -fe=$(OUTDIR)/CRCFUZZ.EXE tools/crcfuzz.c src/serframe.c

$(OUTDIR)/SERDIR.EXE: tools/serdir.c src/seruart.c src/serframe.c src/serrpc.c src/seruart.h src/serframe.h src/serrpc.h
	$(CC) $(CFLAGS) -fe=$(OUTDIR)/SERDIR.EXE tools/serdir.c src/seruart.c src/serframe.c src/serrpc.c

$(OUTDIR)/SERTYPE.EXE: tools/sertype.c src/seruart.c src/serframe.c src/serrpc.c src/seruart.h src/serframe.h src/serrpc.h
	$(CC) $(CFLAGS) -fe=$(OUTDIR)/SERTYPE.EXE tools/sertype.c src/seruart.c src/serframe.c src/serrpc.c

clean: .SYMBOLIC
	rm -f $(OUTDIR)/*.EXE $(OUTDIR)/*.obj $(OUTDIR)/*.map $(OUTDIR)/*.err

sercopy: $(OUTDIR)/SERCOPY.EXE

serdel: $(OUTDIR)/SERDEL.EXE

serren: $(OUTDIR)/SERREN.EXE

$(OUTDIR)/SERCOPY.EXE: tools/sercopy.c src/seruart.c src/serframe.c src/serrpc.c src/seruart.h src/serframe.h src/serrpc.h
	$(CC) $(CFLAGS) -fe=$(OUTDIR)/SERCOPY.EXE tools/sercopy.c src/seruart.c src/serframe.c src/serrpc.c

$(OUTDIR)/SERDEL.EXE: tools/serdel.c src/seruart.c src/serframe.c src/serrpc.c src/seruart.h src/serframe.h src/serrpc.h
	$(CC) $(CFLAGS) -fe=$(OUTDIR)/SERDEL.EXE tools/serdel.c src/seruart.c src/serframe.c src/serrpc.c

$(OUTDIR)/SERREN.EXE: tools/serren.c src/seruart.c src/serframe.c src/serrpc.c src/seruart.h src/serframe.h src/serrpc.h
	$(CC) $(CFLAGS) -fe=$(OUTDIR)/SERREN.EXE tools/serren.c src/seruart.c src/serframe.c src/serrpc.c

serdfs: $(OUTDIR)/SERDFS.EXE

SERDFS_SRCS = src/serdfs.c src/seruart.c src/serframe.c src/serrpc.c
SERDFS_HDRS = src/globals.h src/dosstruc.h src/seruart.h src/serframe.h src/serrpc.h
SERDFS_MSGS = src/msg/help.c src/msg/unsupdos.c src/msg/noredir.c src/msg/alrload.c src/msg/notload.c src/msg/tsrcomfa.c src/msg/nomultpx.c src/msg/othertsr.c src/msg/unloaded.c src/msg/mapfail.c src/msg/drvactiv.c src/msg/memfail.c src/msg/relfail.c src/msg/comfail.c src/msg/instlled.c

$(OUTDIR)/chint086.obj: src/chint086.asm
	wasm -bt=dos -ms -0 src/chint086.asm -fo=$(OUTDIR)/chint086.obj

$(OUTDIR)/SERDFS.EXE: $(OUTDIR)/chint086.obj $(SERDFS_SRCS) $(SERDFS_HDRS) $(SERDFS_MSGS)
	wcc $(CFLAGS) -fo=$(OUTDIR)/serdfs.obj src/serdfs.c
	wlink system dos name $(OUTDIR)/SERDFS.EXE file $(OUTDIR)/serdfs.obj file $(OUTDIR)/chint086.obj

src/msg/help.c: tools/genmsg.py
	cd src && python3 ../tools/genmsg.py msg
