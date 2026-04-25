CC     = wcl
CFLAGS = -bt=dos -ms -0 -os -s -d0 -we -wx
OUTDIR = build

all: $(OUTDIR)/HELLO.EXE

hello: $(OUTDIR)/HELLO.EXE

$(OUTDIR)/HELLO.EXE: src/hello.c
	$(CC) $(CFLAGS) -fe=$(OUTDIR)/HELLO.EXE src/hello.c

clean: .SYMBOLIC
	rm -f $(OUTDIR)/*.EXE $(OUTDIR)/*.obj $(OUTDIR)/*.map $(OUTDIR)/*.err
