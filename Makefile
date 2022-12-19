.phony all:
all: disklist diskinfo diskget diskput

disklist: disklist.c
	gcc disklist.c -o disklist

diskinfo: diskinfo.c
	gcc diskinfo.c -o diskinfo

diskget: diskget.c
	gcc diskget.c -o diskget

diskput: diskput.c
	gcc diskput.c -o diskput

.PHONY clean:
clean:
	-rm -rf *.o *.exe