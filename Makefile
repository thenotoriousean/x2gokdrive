include ../ephyr/Makefile

VPATH = ../../../../../../hw/kdrive/x2gokdrive

X2GO_OBJECTS = x2gokdriveselection.o x2gokdrive.o os.o \
	x2gokdriveinit.o x2gokdrivecursor.o remote.o

x2gokdrive: $(X2GO_OBJECTS) $(Xephyr_DEPENDENCIES) $(EXTRA_Xephyr_DEPENDENCIES)
	$(AM_V_CCLD)$(Xephyr_LINK) $(X2GO_OBJECTS) $(Xephyr_LDADD) $(LIBS) -lz -ljpeg -lpng -lpthread

x2goclean:
	rm *.o x2gokdrive

x2go: x2gokdrive
