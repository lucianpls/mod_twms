MODULE = mod_twms
C_SRC = $(MODULE).cpp
FILES = $(C_SRC)
OBJECTS = $(FILES:.cpp=.lo)

DEFINES = -DLINUX -D_REENTRANT -D_GNU_SOURCE

LIBS =

# Create Makefile.lcl, which should define
# DEST, the folder where the apache module should reside
# EXTRA_INCLUDES, includes for httpd, apr headers
# For example look at Makefile.lcl.example

include Makefile.lcl
TARGET = .libs/$(MODULE).so

# Can't use apxs to build c++ modules
# The options used here might depend on how apache was built

default	: $(TARGET)

$(TARGET)       :       $(OBJECTS)
	libtool --mode=link g++ -o $(MODULE).la -rpath $(DEST) -module -avoid-version $^ $(LIBS)

%.lo	:	%.cpp
	libtool --mode=compile g++ -std=c++0x -prefer-pic -O2 -Wall $(DEFINES) $(EXTRA_INCLUDES) -pthread -c -o $@ $< && touch $(@:.lo=.slo)

install : $(TARGET)
	$(SUDO) $(CP) $(TARGET) $(DEST)

clean   :
	$(RM) -r .libs *.o *.lo *.slo *.la
