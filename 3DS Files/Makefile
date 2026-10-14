#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

# GNU Make's $(notdir ...)/$(dir ...)/etc. are whitespace-list based, so this
# Makefile (like all devkitPro templates) cannot be invoked from a path
# containing a space -- and this folder is "3DS Files". Build with
# tools/build_cia.ps1, which subst's a temporary space-free drive letter onto
# this directory, or `subst Z: "<path to 3DS Files>"` by hand and run make
# from Z:\.
ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITARM)/3ds_rules

#---------------------------------------------------------------------------------
# TARGET is the name of the output
# BUILD is the directory where object files & intermediate files will be placed
# SOURCES is a list of directories containing source code
# DATA is a list of directories containing data files
# INCLUDES is a list of directories containing header files
# ROMFS is the game's data for the .3dsx's and the .cia's RomFS, converted from
# your own APK by tools/prepare_data.py. tools/build_cia.ps1 passes
# ROMFS=romfs (a junction to out/ZombieOutbreakSim) unless -NoData; left empty,
# the build is engine-only and the game reads sdmc:/ZombieOutbreakSim/.
#---------------------------------------------------------------------------------
TARGET		:=	ZOS-3DS
BUILD		:=	build
# Source file names must be unique across these: objects land flat in build/.
SOURCES		:=	source source/gfx source/cocos source/game source/net
DATA		:=	data
INCLUDES	:=	include source
ROMFS		?=

APP_TITLE		:= Zombie Outbreak Simulator
APP_DESCRIPTION	:= Zombie Outbreak Simulator Android Game Port to Nintendo 3DS
APP_AUTHOR		:= Octoblimp
# The title version, which FBI and the HOME Menu show: makerom's -ver packs
# it as major << 10 | minor << 4 | micro.
APP_VERSION_MAJOR	:= 1
APP_VERSION_MINOR	:= 1
APP_VERSION_MICRO	:= 0

#---------------------------------------------------------------------------------
# options for code generation
#---------------------------------------------------------------------------------
ARCH	:=	-march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft

CFLAGS	:=	-g -Wall -O2 -mword-relocations \
			-ffunction-sections \
			$(ARCH)

CFLAGS	+=	$(INCLUDE) -D__3DS__

# RTTI and exceptions stay on: the Android build was compiled with both (its
# .so carries typeinfo for every game class, and Simulation and the scenes
# throw std::runtime_error on bad state), and the ported code keeps that shape.
CXXFLAGS	:= $(CFLAGS) -std=gnu++17

ASFLAGS	:=	-g $(ARCH)
LDFLAGS	=	-specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

LIBS	:= -lcitro3d -lcurl -lmbedtls -lmbedx509 -lmbedcrypto -lctru -lz -lm

#---------------------------------------------------------------------------------
# list of directories containing libraries, this must be the top level containing
# include and lib
#---------------------------------------------------------------------------------
LIBDIRS	:= $(CTRULIB) $(PORTLIBS)

#---------------------------------------------------------------------------------
# no real need to edit anything past this point unless you need to add additional
# rules for different file extensions
#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT	:=	$(CURDIR)/$(TARGET)
export TOPDIR	:=	$(CURDIR)

export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
			$(foreach dir,$(DATA),$(CURDIR)/$(dir))

export DEPSDIR	:=	$(CURDIR)/$(BUILD)

CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
BINFILES	:=	$(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))
PICAFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.v.pica)))

#---------------------------------------------------------------------------------
ifeq ($(strip $(CPPFILES)),)
#---------------------------------------------------------------------------------
	export LD	:=	$(CC)
#---------------------------------------------------------------------------------
else
#---------------------------------------------------------------------------------
	export LD	:=	$(CXX)
#---------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------

export OFILES_SOURCES 	:=	$(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)

export OFILES_BIN	:=	$(addsuffix .o,$(BINFILES)) $(PICAFILES:.v.pica=.shbin.o)

export OFILES := $(OFILES_BIN) $(OFILES_SOURCES)

export HFILES	:=	$(addsuffix .h,$(subst .,_,$(BINFILES))) $(PICAFILES:.v.pica=_shbin.h)

export INCLUDE	:=	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
			$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
			-I$(CURDIR)/$(BUILD)

export LIBPATHS	:=	$(foreach dir,$(LIBDIRS),-L$(dir)/lib)

export _3DSXDEPS	:=	$(if $(NO_SMDH),,$(OUTPUT).smdh)

export APP_ICON := $(TOPDIR)/meta/icon.png

ifeq ($(strip $(NO_SMDH)),)
	export _3DSXFLAGS += --smdh=$(CURDIR)/$(TARGET).smdh
endif

ifneq ($(ROMFS),)
	export _3DSXFLAGS += --romfs=$(CURDIR)/$(ROMFS)
endif

.PHONY: all clean cia

#---------------------------------------------------------------------------------
all: $(BUILD) $(DEPSDIR)
	@$(MAKE) --no-print-directory -C "$(BUILD)" -f "$(CURDIR)/Makefile"

$(BUILD):
	@mkdir -p "$@"

ifneq ($(DEPSDIR),$(BUILD))
$(DEPSDIR):
	@mkdir -p "$@"
endif

#---------------------------------------------------------------------------------
# CIA packaging. makerom/bannertool aren't in devkitPro's package repos: put
# their Windows builds in tools/3ds-bin (the README says where to get them),
# or pass MAKEROM= and BANNERTOOL=. Requires meta/banner.png, meta/icon.png,
# meta/banner_sound.wav (bannertool) plus meta/app.rsf (makerom).
# tools/make_meta.py makes the two PNGs from the APK, tools/banner_sound.py
# the sound (a zombie moan, played by the HOME Menu when the icon is
# selected); tools/build_cia.ps1 runs them when they are missing.
#---------------------------------------------------------------------------------
MAKEROM		:= $(TOPDIR)/../tools/3ds-bin/makerom.exe
BANNERTOOL	:= $(TOPDIR)/../tools/3ds-bin/bannertool.exe

cia: all
	@"$(BANNERTOOL)" makebanner -i meta/banner.png -a meta/banner_sound.wav -o meta/banner.bnr
	@"$(BANNERTOOL)" makesmdh -s "$(APP_TITLE)" -l "$(APP_DESCRIPTION)" -p "$(APP_AUTHOR)" -i meta/icon.png -o meta/icon.icn
	@"$(MAKEROM)" -f cia -o "$(TARGET).cia" -DAPP_ENCRYPTED=false -target t \
		-ver $$(( ($(APP_VERSION_MAJOR) << 10) | ($(APP_VERSION_MINOR) << 4) | $(APP_VERSION_MICRO) )) \
		-elf "$(OUTPUT).elf" -icon meta/icon.icn -banner meta/banner.bnr \
		-rsf meta/app.rsf $(if $(ROMFS),-DAPP_ROMFS=$(ROMFS))
	@echo built ... $(TARGET).cia

#---------------------------------------------------------------------------------
clean:
	@echo clean ...
	@rm -fr "$(BUILD)" "$(TARGET).3dsx" "$(OUTPUT).smdh" "$(TARGET).elf" "$(TARGET).cia" meta/banner.bnr meta/icon.icn

#---------------------------------------------------------------------------------
else
#---------------------------------------------------------------------------------
# main targets
#---------------------------------------------------------------------------------
$(OUTPUT).3dsx	:	$(OUTPUT).elf $(_3DSXDEPS)

$(OFILES_SOURCES) : $(HFILES)

$(OUTPUT).elf	:	$(OFILES)

#---------------------------------------------------------------------------------
# you need a rule like this for each extension you use as binary data
#---------------------------------------------------------------------------------
%.bin.o	%_bin.h :	%.bin
#---------------------------------------------------------------------------------
	@echo $(notdir $<)
	@$(bin2o)

#---------------------------------------------------------------------------------
# PICA200 shader compilation (picasso + bin2s): .v.pica
# compiles straight to .shbin.o + _shbin.h, skipping an intermediate .shbin
# target that conflicts with 3ds_rules' own %.shbin rule.
#---------------------------------------------------------------------------------
define shader-as
	$(eval CURBIN := $*.shbin)
	echo "$(CURBIN).o: $< $1" > $(DEPSDIR)/$*.shbin.d
	echo "extern const u8" `(echo $(CURBIN) | tr . _)`"_end[];" > `(echo $(CURBIN) | tr . _)`.h
	echo "extern const u8" `(echo $(CURBIN) | tr . _)`"[];" >> `(echo $(CURBIN) | tr . _)`.h
	echo "extern const u32" `(echo $(CURBIN) | tr . _)`_size";" >> `(echo $(CURBIN) | tr . _)`.h
	picasso -o $(CURBIN) $1
	bin2s $(CURBIN) | $(AS) -o $*.shbin.o
endef

%.shbin.o %_shbin.h : %.v.pica
#---------------------------------------------------------------------------------
	@echo $(notdir $<)
	@$(call shader-as,$<)

-include $(DEPSDIR)/*.d

#---------------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------------
