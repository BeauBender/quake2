# ------------------------------------------------------ #
# Makefile for the "Yamagi Quake 2 Client"               #
#                                                        #
# Just type "make" to compile the                        #
#  - Client (quake2)                                     #
#  - Server (q2ded)                                      #
#  - Quake II Game (baseq2)                              #
#  - Renderer libraries (gl1, gl3, soft)                 #
#                                                        #
# Base dependencies:                                     #
#  - SDL 3 (vendored in third_party/SDL3 and built      #
#    statically on OS X; system SDL3 via pkgconf        #
#    elsewhere)                                         #
#  - libGL                                               #
#  - Vulkan headers                                      #
#                                                        #
# Optional dependencies:                                 #
#                                                        #
# Platforms:                                             #
#  - FreeBSD                                             #
#  - Linux                                               #
#  - NetBSD                                              #
#  - OpenBSD                                             #
#  - OS X                                                #
#  - Windows (MinGW)                                     #
# ------------------------------------------------------ #

# Variables
# ---------
# ASAN
#   Builds with address sanitizer, includes DEBUG.
# DEBUG
#   Builds a debug build, forces -O0 and adds debug symbols.
# MINGW_CHOST
#   If you use mingw this can specify architecture.
#   Available values:
#   x86_64-w64-mingw32 -> indicates x86_64
#   i686-w64-mingw32   -> indicates i386
# IOS_SDK, IOS_MIN, IOS_BUNDLE_ID, IOS_BUNDLE_DATA, IOS_ROOT
#   The iOS mode, selected by the goal "ios" (the app bundle) or
#   "ios_objects" (the engine objects and the Info.plist only, for
#   tests/check-ios.sh); macOS with Xcode only. It forces
#   STATIC_MODULES=1 STATIC_RENDERER=metal and builds the vendored
#   SDL3, the client, the game and ref_metal for the SDK into
#   IOS_ROOT/Quake2.app (doc/020_installation.md).
#   IOS_SDK: iphonesimulator (default, signed ad hoc) or iphoneos
#   (unsigned). IOS_MIN: the minimum iOS version (17.0).
#   IOS_BUNDLE_ID: CFBundleIdentifier (a neutral default).
#   IOS_BUNDLE_DATA: optional, a directory whose baseq2/ is copied
#   into the bundle; local builds only, never game data in a commit.
#   IOS_ROOT: the output, default build/ios-<IOS_SDK>/<debug|release>.
#   Never release/ or debug/ or under them, however spelled (symlinks
#   and .. resolved, letter case ignored). IOS_ROOT and IOS_BUNDLE_DATA hold letters, digits
#   and . _ / + @ - only: no make or shell metacharacter.
# QUIET
#   If defined, "===> CC ..." lines are silenced.
# STATIC_MODULES
#   STATIC_MODULES=1 (macOS only) links the game module and one
#   renderer into the client executable: no game.dylib and no
#   ref_*.dylib, GetGameAPI and GetRefAPI are resolved at link time.
#   Builds only the client (make STATIC_MODULES=1 client).
# STATIC_RENDERER
#   The renderer linked in by STATIC_MODULES=1: soft (default), gl1
#   or metal.
# STATIC_ROOT
#   Where a STATIC_MODULES=1 build writes its objects (STATIC_ROOT/obj-<renderer>)
#   and the executable (STATIC_ROOT/bin). Default build/<debug|release>/
#   static-<renderer>. Never release/ or debug/.
# SOURCE_DATE_EPOCH
#   For reproduceable builds, look here for details:
#   https://reproducible-builds.org/specs/source-date-epoch/
#   If set, adds a BUILD_DATE define to CFLAGS.
# UBSAN
#   Builds with undefined behavior sanitizer.includes DEBUG.
# VERBOSE
#   Prints full compile, linker and misc commands.
# WERR
#   Treat compiler warnings as errors.
#   If defined, -Werror is added to compiler flags.
# ----------

# User configurable options
# -------------------------

# WITH_EXECINFO
# Enables backtraces through execinfo on supported platforms.
# Usefull to get some basic debug information when the game
# crashes.
WITH_EXECINFO:=yes

# WITH_RPATH
# Sets an RPATH to $ORIGIN/lib. It can be used to
# inject custom libraries, e.g. a patches libSDL.so. Not supported on Windows.
WITH_RPATH:=yes

# WITH_SYSTEMWIDE
# Enable systemwide installation of game assets.
WITH_SYSTEMWIDE:=no

# WITH_SYSTEMDIR
# This will set the default SYSTEMDIR, a non-empty string
# would actually be used. On Windows normals slashes (/)
# instead of backslashed (\) should be used! The string
# MUST NOT be surrounded by quotation marks!
WITH_SYSTEMDIR:=""

# Enable XDG directories support
WITH_XDG:=yes

# WITH_METAL_SHIM
# macOS only. Builds ref_gl1 against the in-tree OpenGL subset on
# Metal (src/client/refresh/gl1/metal/) instead of Apple's OpenGL
# framework, for Macs where OpenGL is unavailable. Needs the Xcode
# Metal toolchain (xcrun metal); writes ref_gl1.metallib next to
# ref_gl1.dylib. Work in progress: see doc/100_renderpins.md.
WITH_METAL_SHIM:=no

# METAL_SHADERS
# macOS only, for ref_metal and the WITH_METAL_SHIM ref_gl1. "yes"
# compiles the shaders to a .metallib with xcrun metal. "no" builds the
# C and Objective-C objects and the dylibs only, without the shader
# compile and its toolchain check, so the build runs on a machine that
# has no Metal shader compiler (the CodeQL job). The dylibs built that
# way have no .metallib and cannot create a Metal context.
METAL_SHADERS:=yes

# OSX_APP
# This will set the build options to create an MacOS .app-bundle.
# The app-bundle itself will not be created, but the runtime paths
# will be set to expect the game-data in *.app/
# Contents/Resources
OSX_APP:=yes

# CONFIG_FILE
# This is an optional configuration file, it'll be used in
# case of presence.
CONFIG_FILE:=config.mk

# PKG_CONFIG
# Specify program that configures SDL3.
# Needs to be overridable for cross-compilation.
PKG_CONFIG ?= pkgconf

# ----------

# In case of a configuration file being present, we'll just use it
ifeq ($(wildcard $(CONFIG_FILE)), $(CONFIG_FILE))
include $(CONFIG_FILE)
endif

# ----------

# Normalize QUIET value to either "x" or ""
ifdef QUIET
	override QUIET := "x"
else
	override QUIET := ""
endif

# ----------

# Check if it's pkgconf or pkg-config.
ifeq (, $(shell which $(PKG_CONFIG)))
ifneq (, $(shell which pkgconf))
PKG_CONFIG:=pkgconf
else
PKG_CONFIG:=pkg-config
endif
endif

# ----------

# Detect the OS, normalize some abiguous YQ2_OSTYPE strings.
YQ2_OSTYPE ?= $(shell uname -s | sed -e 's/MINGW.*/Windows/' -e 's/Windows.*/Windows/')

# Detect the architecture
ifeq ($(YQ2_OSTYPE), Windows)
ifdef MINGW_CHOST
ifeq ($(MINGW_CHOST), x86_64-w64-mingw32)
YQ2_ARCH ?= x86_64
else # i686-w64-mingw32
YQ2_ARCH ?= i386
endif
else # windows, but MINGW_CHOST not defined
YQ2_ARCH ?= $(shell uname -m | sed -e 's/i.86/i386/')
endif # windows but MINGW_CHOST not defined
else
ifneq ($(YQ2_OSTYPE), Darwin)
# Normalize some abiguous YQ2_ARCH strings
YQ2_ARCH ?= $(shell uname -m | sed -e 's/i.86/i386/' -e 's/amd64/x86_64/' -e 's/arm64/aarch64/' -e 's/^arm.*/arm/')
else
YQ2_ARCH ?= $(shell uname -m)
endif
endif

# The iOS mode (see IOS_SDK at the top): the goal selects it, and it
# compiles with the SDK's clang for the SDK's target. YQ2_OSTYPE stays
# Darwin: every Darwin branch below (client objects as Objective-C, .m
# with ARC, the game rule, the vendored SDL3) is the one iOS needs.
ifneq ($(filter ios ios_objects,$(MAKECMDGOALS)),)
override IOS_MODE := 1
else
override IOS_MODE :=
endif
METAL_SDK := macosx
METALFX_LDLIBS := -weak_framework MetalFX
ifeq ($(IOS_MODE),1)
ifneq ($(YQ2_OSTYPE), Darwin)
$(error make ios needs macOS with Xcode, not $(YQ2_OSTYPE))
endif
ifneq ($(filter-out ios ios_objects config clean cleanall,$(MAKECMDGOALS)),)
$(error the iOS mode builds only the app: make ios (or ios_objects, config, clean), not "$(MAKECMDGOALS)")
endif
IOS_SDK ?= iphonesimulator
IOS_MIN ?= 17.0
IOS_BUNDLE_ID ?= org.yquake2.quake2
ifeq ($(IOS_SDK), iphonesimulator)
IOS_TARGET := arm64-apple-ios$(IOS_MIN)-simulator
IOS_PLATFORM := iPhoneSimulator
else ifeq ($(IOS_SDK), iphoneos)
IOS_TARGET := arm64-apple-ios$(IOS_MIN)
IOS_PLATFORM := iPhoneOS
else
$(error IOS_SDK must be iphonesimulator or iphoneos, not "$(IOS_SDK)")
endif
ifneq ($(shell printf '%s' '$(subst ',,$(IOS_MIN))' | LC_ALL=C grep -E -x '[0-9]+(\.[0-9]+){0,2}'),$(IOS_MIN))
$(error IOS_MIN must be a version such as 17.0, not "$(IOS_MIN)")
endif
ifneq ($(shell printf '%s' '$(subst ',,$(IOS_BUNDLE_ID))' | LC_ALL=C grep -E -x '[A-Za-z0-9][A-Za-z0-9.-]*'),$(IOS_BUNDLE_ID))
$(error IOS_BUNDLE_ID may hold letters, digits, dots and hyphens only, not "$(IOS_BUNDLE_ID)")
endif
IOS_SDK_PATH := $(shell xcrun --sdk $(IOS_SDK) --show-sdk-path 2>/dev/null)
ifeq ($(wildcard $(IOS_SDK_PATH)/SDKSettings.plist),)
$(error the $(IOS_SDK) SDK was not found (xcrun --sdk $(IOS_SDK) --show-sdk-path): Xcode with its iOS platform is needed)
endif
override YQ2_ARCH := arm64
override CC := xcrun -sdk $(IOS_SDK) clang
override LD := xcrun -sdk $(IOS_SDK) ld
override STATIC_MODULES := 1
override STATIC_RENDERER := metal
override WITH_RPATH := no
IOS_FLAGS := -target $(IOS_TARGET) -isysroot $(IOS_SDK_PATH)
METAL_SDK := $(IOS_SDK)
# the iOS device SDK has MetalFX, the simulator SDKs have not
METALFX_LDLIBS := $(if $(wildcard $(IOS_SDK_PATH)/System/Library/Frameworks/MetalFX.framework),-weak_framework MetalFX)
endif

# Detect the compiler
ifeq ($(shell $(CC) -v 2>&1 | grep -c "clang version"), 1)
COMPILER := clang
COMPILERVER := $(shell $(CC)  -dumpversion | sed -e 's/\.\([0-9][0-9]\)/\1/g' -e 's/\.\([0-9]\)/0\1/g' -e 's/^[0-9]\{3,4\}$$/&00/')
else ifeq ($(shell $(CC) -v 2>&1 | grep -c -E "(gcc version|gcc-Version)"), 1)
COMPILER := gcc
COMPILERVER := $(shell $(CC)  -dumpversion | sed -e 's/\.\([0-9][0-9]\)/\1/g' -e 's/\.\([0-9]\)/0\1/g' -e 's/^[0-9]\{3,4\}$$/&00/')
else
COMPILER := unknown
endif

# ASAN includes DEBUG
ifdef ASAN
DEBUG=1
endif

# UBSAN includes DEBUG
ifdef UBSAN
DEBUG=1
endif

# ----------

# Set up build and bin output directories

# Root dir names
override BINROOT :=
override BUILDROOT := build

ifdef DEBUG
	override BINDIR := $(BINROOT)debug
	override BUILDDIR := $(BUILDROOT)/debug
else
	override BINDIR := $(BINROOT)release
	override BUILDDIR := $(BUILDROOT)/release
endif

# The iOS mode writes everything under IOS_ROOT, the vendored SDL
# objects too (third_party/sdl3.make puts them under BUILDDIR as it is
# when included): never the copy the macOS builds share.
# IOS_ROOT and IOS_BUNDLE_DATA are paths, not make or shell text: their
# raw value ($(value), not expanded) holds no "$", and the value only
# the characters of IOS_PATH_RE, so a recipe can quote them as '...'.
ifeq ($(IOS_MODE),1)
IOS_PATH_RE := [A-Za-z0-9._/][A-Za-z0-9._/+@-]*
ios_path_out = $(shell printf '%s' '$(subst ',,$($(1)))' | LC_ALL=C grep -E -x '$(IOS_PATH_RE)')
ios_path_ok = $(and $(if $(findstring $$,$(value $(1))),,1),$(call ios_path_out,$(1)),$(if $(subst $(call ios_path_out,$(1)),,$(value $(1))),,1))
# lower case: the default APFS volume ignores case, so RELEASE is release
ios_lc = $(shell printf '%s' '$(1)' | tr 'A-Z' 'a-z')
ifeq ($(origin IOS_ROOT),undefined)
ifdef DEBUG
IOS_ROOT := $(BUILDROOT)/ios-$(IOS_SDK)/debug
else
IOS_ROOT := $(BUILDROOT)/ios-$(IOS_SDK)/release
endif
endif
ifeq ($(call ios_path_ok,IOS_ROOT),)
$(error IOS_ROOT must be one path of letters, digits and . _ / + @ - (no make or shell metacharacter, no leading + @ -), not "$(value IOS_ROOT)")
endif
ifneq ($(value IOS_BUNDLE_DATA),)
ifeq ($(call ios_path_ok,IOS_BUNDLE_DATA),)
$(error IOS_BUNDLE_DATA must be one path of letters, digits and . _ / + @ - (no make or shell metacharacter, no leading + @ -), not "$(value IOS_BUNDLE_DATA)")
endif
endif
# the path with the symlinks of its longest existing prefix resolved
ios_physical = $(if $(realpath $(1)),$(realpath $(1)),$(call ios_physical,$(or $(patsubst %/,%,$(dir $(1))),/))/$(notdir $(1)))
IOS_ROOT_ABS := $(abspath $(call ios_physical,$(abspath $(IOS_ROOT))))
IOS_NOT_ROOTS := $(sort $(foreach d,release debug,$(abspath $(d)) $(abspath $(call ios_physical,$(abspath $(d))))))
IOS_NOT_ROOTS_LC := $(call ios_lc,$(IOS_NOT_ROOTS))
ifneq ($(filter $(IOS_NOT_ROOTS_LC) $(addsuffix /%,$(IOS_NOT_ROOTS_LC)),$(call ios_lc,$(abspath $(IOS_ROOT)) $(IOS_ROOT_ABS))),)
$(error IOS_ROOT must not be release/ or debug/ or under them, not "$(IOS_ROOT)" ($(IOS_ROOT_ABS)))
endif
override BUILDDIR := $(IOS_ROOT)
override STATIC_ROOT := $(IOS_ROOT)
endif

# ----------

# Base CFLAGS. These may be overridden by the environment.
# Highest supported optimizations are -O2, higher levels
# will likely break this crappy code.
ifdef DEBUG
CFLAGS ?= -O0 -g -Wall -pipe -DDEBUG
ifdef ASAN
override CFLAGS += -fsanitize=address -DUSE_SANITIZER
endif
ifdef UBSAN
override CFLAGS += -fsanitize=undefined -DUSE_SANITIZER
endif
else
CFLAGS ?= -O2 -Wall -pipe -fomit-frame-pointer
endif

# Treat warnings as errors
ifdef WERR
override CFLAGS += -Werror
endif

# Always needed are:
#  -fno-strict-aliasing since the source doesn't comply
#   with strict aliasing rules and it's next to impossible
#   to get it there...
#  -fwrapv for defined integer wrapping. MSVC6 did this
#   and the game code requires it.
#  -fvisibility=hidden to keep symbols hidden. This is
#   mostly best practice and not really necessary.
override CFLAGS += -fno-strict-aliasing -fwrapv -fvisibility=hidden

# -MMD to generate header dependencies. Unsupported by
#  the Clang shipped with OS X.
ifneq ($(YQ2_OSTYPE), Darwin)
override CFLAGS += -MMD
endif

# OS X architecture.
ifeq ($(YQ2_OSTYPE), Darwin)
override CFLAGS += -arch $(YQ2_ARCH)
endif

# The iOS mode's target and SDK.
ifeq ($(IOS_MODE),1)
override CFLAGS += $(IOS_FLAGS)
endif

# ----------

# ARM needs a sane minimum architecture. We need the `yield`
# opcode, arm6k is the first iteration that supports it. arm6k
# is also the first Raspberry PI generation and older hardware
# is likely too slow to run the game. We're not enforcing the
# minimum architecture, but if you're build for something older
# like arm5 the `yield` opcode isn't compiled in and the game
# (especially q2ded) will consume more CPU time than necessary.
ifeq ($(YQ2_ARCH), arm)
CFLAGS += -march=armv6k
endif

# ----------

# Switch of some annoying warnings.
ifeq ($(COMPILER), clang)
	# -Wno-missing-braces because otherwise clang complains
	#  about totally valid 'vec3_t bla = {0}' constructs.
	# -fno-common avoids linker warnings due
	#  uninitialized global variables treated as "common".
	#  On most platforms clang sets no-commom by default,
	#  MacOS is a prominent exception.
	override CFLAGS += -Wno-missing-braces -fno-common
else ifeq ($(COMPILER), gcc)
	# GCC 8.0 or higher.
	ifeq ($(shell test $(COMPILERVER) -ge 80000; echo $$?),0)
	    # -Wno-format-truncation and -Wno-format-overflow
		# because GCC spams about 50 false positives.
		override CFLAGS += -Wno-format-truncation -Wno-format-overflow
	endif
endif

# ----------

# Defines the operating system and architecture
override CFLAGS += -DYQ2OSTYPE=\"$(YQ2_OSTYPE)\" -DYQ2ARCH=\"$(YQ2_ARCH)\"

# ----------

ifdef SOURCE_DATE_EPOCH
override CFLAGS += -DBUILD_DATE=\"$(shell date --utc --date="@${SOURCE_DATE_EPOCH}" +"%b %_d %Y" | sed -e 's/ /\\ /g')\"
endif

# ----------

# Using the default x87 float math on 32bit x86 causes rounding trouble
# -ffloat-store could work around that, but the better solution is to
# just enforce SSE - every x86 CPU since Pentium3 supports that
# and this should even improve the performance on old CPUs
ifeq ($(YQ2_ARCH), i386)
override CFLAGS += -msse -mfpmath=sse
endif

# Force SSE math on x86_64. All sane compilers should do this
# anyway, just to protect us from broken Linux distros.
ifeq ($(YQ2_ARCH), x86_64)
override CFLAGS += -mfpmath=sse
endif

# Disable floating-point expression contraction. While this shouldn't be
# a problem for C (only for C++) better be safe than sorry. See
# https://gcc.gnu.org/bugzilla/show_bug.cgi?id=100839 for details.
ifeq ($(COMPILER), gcc)
override CFLAGS += -ffp-contract=off
endif

# ----------

# Systemwide installation.
ifeq ($(WITH_SYSTEMWIDE),yes)
override CFLAGS += -DSYSTEMWIDE
ifneq ($(WITH_SYSTEMDIR),"")
override CFLAGS += -DSYSTEMDIR=\"$(WITH_SYSTEMDIR)\"
endif
endif

# ----------

# Extra CFLAGS for SDL. On OS X SDL3 is built from the
# vendored source (third_party/sdl3.make, included below);
# elsewhere the system SDL3 is found by pkgconf.
ifeq ($(YQ2_OSTYPE), Darwin)
SDLCFLAGS := -Ithird_party/SDL3/include
else
SDLCFLAGS := $(shell $(PKG_CONFIG) --cflags sdl3)
endif

ifdef NO_SDL_GYRO
SDLCFLAGS += -DNO_SDL_GYRO
endif

# ----------

# Base include path.
ifeq ($(YQ2_OSTYPE),Linux)
INCLUDE ?= -I/usr/include
else ifeq ($(YQ2_OSTYPE),FreeBSD)
INCLUDE ?= -I/usr/local/include
else ifeq ($(YQ2_OSTYPE),NetBSD)
INCLUDE ?= -I/usr/X11R7/include -I/usr/pkg/include
else ifeq ($(YQ2_OSTYPE),OpenBSD)
INCLUDE ?= -I/usr/local/include
else ifeq ($(YQ2_OSTYPE),Windows)
INCLUDE ?= -I/usr/include
else ifeq ($(YQ2_OSTYPE),Darwin)
# Nothing from /usr/local or Homebrew: SDL3 is vendored.
INCLUDE ?=
endif

# ----------

# Base LDFLAGS. This is just the library path.
ifeq ($(YQ2_OSTYPE),Linux)
LDFLAGS ?= -L/usr/lib
else ifeq ($(YQ2_OSTYPE),FreeBSD)
LDFLAGS ?= -L/usr/local/lib
else ifeq ($(YQ2_OSTYPE),NetBSD)
LDFLAGS ?= -L/usr/X11R7/lib -Wl,-R/usr/X11R7/lib -L/usr/pkg/lib -Wl,-R/usr/pkg/lib
else ifeq ($(YQ2_OSTYPE),OpenBSD)
LDFLAGS ?= -L/usr/local/lib
else ifeq ($(YQ2_OSTYPE),Windows)
LDFLAGS ?= -L/usr/lib
else ifeq ($(YQ2_OSTYPE),Darwin)
# Nothing from /usr/local or Homebrew: SDL3 is vendored.
LDFLAGS ?=
endif

# Link address sanitizer if requested.
ifdef ASAN
override LDFLAGS += -fsanitize=address
endif

# Link undefined behavior sanitizer if requested.
ifdef UBSAN
override LDFLAGS += -fsanitize=undefined
endif

ifeq ($(IOS_MODE),1)
override LDFLAGS += $(IOS_FLAGS)
endif

# Required libraries.
ifeq ($(YQ2_OSTYPE),Linux)
LDLIBS ?= -lm -ldl -rdynamic
else ifeq ($(YQ2_OSTYPE),FreeBSD)
LDLIBS ?= -lm
else ifeq ($(YQ2_OSTYPE),NetBSD)
LDLIBS ?= -lm
else ifeq ($(YQ2_OSTYPE),OpenBSD)
LDLIBS ?= -lm
else ifeq ($(YQ2_OSTYPE),Windows)
LDLIBS ?= -lws2_32 -lbcrypt -lwinmm -static-libgcc
else ifeq ($(YQ2_OSTYPE), Darwin)
LDLIBS ?= -arch $(YQ2_ARCH)
else ifeq ($(YQ2_OSTYPE), Haiku)
LDLIBS ?= -lm -lnetwork
else ifeq ($(YQ2_OSTYPE), SunOS)
LDLIBS ?= -lm -lsocket -lnsl
endif

# ASAN and UBSAN must not be linked
# with --no-undefined. OSX and OpenBSD
# don't support it at all.
ifndef ASAN
ifndef UBSAN
ifneq ($(YQ2_OSTYPE), Darwin)
ifneq ($(YQ2_OSTYPE), OpenBSD)
override LDFLAGS += -Wl,--no-undefined
endif
endif
endif
endif

# ----------

# Extra LDFLAGS for SDL
#
# On OS X the vendored SDL3 is linked statically into the
# client executable only, and all of it (-force_load), so
# that every public SDL function is defined and exported
# by quake2 itself. The renderer dylibs are loaded by the
# client with dlopen() and use SDL too (GL context,
# window, SDL_Renderer). They must use the client's SDL,
# not a second copy: a second static copy would have its
# own, uninitialised video subsystem and window list. So
# the renderers are linked without SDL, and only their
# undefined _SDL_* symbols are marked for lookup at load
# time (-Wl,-U,<symbol>, see SDL_DYNAMIC_LOOKUP), where
# dyld finds them in the already loaded executable. Any
# other undefined symbol still fails the link.
ifeq ($(YQ2_OSTYPE), Darwin)
include third_party/sdl3.make
ifeq ($(IOS_MODE),1)
SDL3_CFLAGS += $(IOS_FLAGS)
endif
SDLLDFLAGS := -Wl,-force_load,$(SDL3_LIB) $(SDL3_LDLIBS)
SDL_DYNAMIC_LOOKUP = $$(nm -u $(filter %.o,$^) | grep '^_SDL_' | sort -u | sed 's/^/-Wl,-U,/')
else
SDLLDFLAGS := $(shell $(PKG_CONFIG) --libs sdl3)
endif

# The renderer libs don't need libmingw32 or -mwindows.
ifeq ($(YQ2_OSTYPE), Windows)
DLL_SDLLDFLAGS = $(subst -mwindows,,$(subst -lmingw32,,$(SDLLDFLAGS)))
endif

# ----------

# The image decoder (src/client/refresh/files/imgdecode.c), linked into
# the client and every renderer: on OS X ImageIO decodes PNG, JPEG and
# TGA (docs/decisions/0010-imageio-decoder.md). Elsewhere there is no
# decoder and nothing is linked.
ifeq ($(YQ2_OSTYPE), Darwin)
IMGDECODE_LDLIBS := -framework ImageIO -framework CoreGraphics -framework CoreFoundation
endif

# ----------

# STATIC_MODULES=1: the game and one renderer are linked into the
# client executable (see the variables at the top). The objects of
# the three go to a tree of their own, so the dynamic build is not
# touched. This comes after the SDL3 include on purpose: the SDL
# library keeps its place and is shared by both builds.
STATIC_RENDERER ?= soft
STATIC_LINK_OBJS :=
STATIC_LDLIBS :=
ifeq ($(STATIC_MODULES),1)
ifneq ($(YQ2_OSTYPE), Darwin)
$(error STATIC_MODULES=1 is for macOS only)
endif
ifneq ($(words $(STATIC_RENDERER)),1)
$(error STATIC_RENDERER must be exactly one of soft, gl1 or metal, not "$(STATIC_RENDERER)")
endif
ifeq ($(filter $(STATIC_RENDERER),soft gl1 metal),)
$(error STATIC_RENDERER must be exactly one of soft, gl1 or metal, not "$(STATIC_RENDERER)")
endif
# Only the client is built. The recursive "make <bindir>/quake2" of the
# client target passes the executable as its goal.
ifneq ($(filter-out client static_modules ios ios_objects config clean cleanall %/bin/quake2,$(if $(MAKECMDGOALS),$(MAKECMDGOALS),all)),)
$(error STATIC_MODULES=1 builds only the client: make STATIC_MODULES=1 client (or static_modules, config, clean), not "$(if $(MAKECMDGOALS),$(MAKECMDGOALS),all)")
endif
ifeq ($(WITH_METAL_SHIM)$(STATIC_RENDERER), yesgl1)
$(error STATIC_MODULES=1 with STATIC_RENDERER=gl1 needs WITH_METAL_SHIM=no)
endif
STATIC_ROOT ?= $(BUILDDIR)/static-$(STATIC_RENDERER)
override BINDIR := $(STATIC_ROOT)/bin
# the renderer is in the object path, so an explicit STATIC_ROOT reused
# with another renderer never links objects built with the old
# -DSTATIC_RENDERER_NAME
override BUILDDIR := $(STATIC_ROOT)/obj-$(STATIC_RENDERER)
endif

# ----------

# When make is invoked by "make VERBOSE=1" print
# the compiler and linker commands.
ifdef VERBOSE
Q :=
else
Q := @
endif

# ----------

# Phony targets
.PHONY : all client game icon server ref_gl1 ref_gl3 ref_soft ref_metal static_modules ios ios_objects

# ----------

# Builds everything. Named as the default goal because
# third_party/sdl3.make, included above on OS X, defines
# targets of its own.
.DEFAULT_GOAL := all

all: config client server game ref_gl1 ref_gl3 ref_soft

# ----------


# Print config values
config:
	@echo "Build configuration"
	@echo "============================"
	@echo "YQ2_ARCH = $(YQ2_ARCH), YQ2_OSTYPE = $(YQ2_OSTYPE), COMPILER = $(COMPILER)"
	@echo "WITH_RPATH = $(WITH_RPATH)"
	@echo "WITH_SYSTEMWIDE = $(WITH_SYSTEMWIDE)"
	@echo "WITH_SYSTEMDIR = $(WITH_SYSTEMDIR)"
	@echo "WITH_XDG = $(WITH_XDG)"
	@echo "WITH_METAL_SHIM = $(WITH_METAL_SHIM)"
	@echo "METAL_SHADERS = $(METAL_SHADERS)"
	@echo "STATIC_MODULES = $(STATIC_MODULES), STATIC_RENDERER = $(STATIC_RENDERER)"
	@echo "============================"
	@echo ""

# ----------

# Special target to compile the icon on Windows
ifeq ($(YQ2_OSTYPE), Windows)
icon:
	@echo "===> WR $(BUILDROOT)/icon/icon.res"
	${Q}mkdir -p $(BUILDROOT)/icon
	${Q}windres src/backends/windows/icon.rc -O COFF -o $(BUILDROOT)/icon/icon.res
endif

# ----------

# Cleanup
clean:
	@echo "===> CLEAN"
	${Q}rm -Rf build debug/* release/*

cleanall:
	@echo "===> CLEAN"
	${Q}rm -Rf build debug release

# ----------

# The client
ifeq ($(YQ2_OSTYPE), Windows)
client:
	@echo "===> Building yquake2.exe"
	${Q}mkdir -p $(BINDIR)
	$(MAKE) $(BINDIR)/yquake2.exe
	@echo "===> Building quake2.exe Wrapper"
	$(MAKE) $(BINDIR)/quake2.exe

$(BUILDDIR)/client/%.o: %.c
	@if [ -z $(QUIET) ]; then\
		echo "===> CC $<";\
	fi
	${Q}mkdir -p $(@D)
	${Q}$(CC) -c $(CFLAGS) $(SDLCFLAGS) $(INCLUDE) -o $@ $<

$(BINDIR)/yquake2.exe : LDFLAGS += -mwindows

else # not Windows

client:
	@echo "===> Building quake2"
	${Q}mkdir -p $(BINDIR)
	$(MAKE) $(BINDIR)/quake2

ifeq ($(YQ2_OSTYPE), Darwin)
$(BUILDDIR)/client/%.o : %.c
	@if [ -z $(QUIET) ]; then\
		echo "===> CC $<";\
	fi
	${Q}mkdir -p $(@D)
	${Q}$(CC) -arch $(YQ2_ARCH) -x objective-c -c $(CFLAGS) $(SDLCFLAGS) $(INCLUDE)  $< -o $@

# Objective-C proper (cl_upscale_metalfx.m), with ARC
$(BUILDDIR)/client/%.o : %.m
	@if [ -z $(QUIET) ]; then\
		echo "===> CC $<";\
	fi
	${Q}mkdir -p $(@D)
	${Q}$(CC) -arch $(YQ2_ARCH) -c -fobjc-arc $(CFLAGS) $(SDLCFLAGS) $(INCLUDE) $< -o $@
else
$(BUILDDIR)/client/%.o: %.c
	@if [ -z $(QUIET) ]; then\
		echo "===> CC $<";\
	fi
	${Q}mkdir -p $(@D)
	${Q}$(CC) -c $(CFLAGS) $(SDLCFLAGS) $(INCLUDE) -o $@ $<
endif

$(BINDIR)/quake2 : CFLAGS += -Wno-unused-result

ifeq ($(WITH_EXECINFO),yes)
ifeq ($(YQ2_OSTYPE), Linux)
$(BINDIR)/quake2 : CFLAGS += -DHAVE_EXECINFO
else ifeq ($(YQ2_OSTYPE), Darwin)
$(BINDIR)/quake2 : CFLAGS += -DHAVE_EXECINFO
else ifeq ($(YQ2_OSTYPE), SunOS)
$(BINDIR)/quake2 : CFLAGS += -DHAVE_EXECINFO
else ifeq ($(YQ2_OSTYPE), FreeBSD)
$(BINDIR)/quake2 : CFLAGS += -DHAVE_EXECINFO
$(BINDIR)/quake2 : LDLIBS += -lexecinfo
else ifeq ($(YQ2_OSTYPE), NetBSD)
$(BINDIR)/quake2 : CFLAGS += -DHAVE_EXECINFO
$(BINDIR)/quake2 : LDLIBS += -lexecinfo
else ifeq ($(YQ2_OSTYPE), OpenBSD)
$(BINDIR)/quake2 : CFLAGS += -DHAVE_EXECINFO
$(BINDIR)/quake2 : LDLIBS += -lexecinfo
else ifeq ($(YQ2_OSTYPE), Haiku)
$(BINDIR)/quake2 : CFLAGS += -DHAVE_EXECINFO
$(BINDIR)/quake2 : LDLIBS += -lexecinfo
endif
endif

ifeq ($(WITH_RPATH),yes)
ifeq ($(YQ2_OSTYPE), Darwin)
$(BINDIR)/quake2 : LDFLAGS += -Wl,-rpath,'@executable_path/lib'
else
$(BINDIR)/quake2 : LDFLAGS += -Wl,-z,origin,-rpath='$$ORIGIN/lib'
endif
endif
endif

ifeq ($(WITH_XDG),yes)
$(BINDIR)/quake2 : CFLAGS += -DUSE_XDG
endif

# ----------

# The server
ifeq ($(YQ2_OSTYPE), Windows)
server:
	@echo "===> Building q2ded"
	${Q}mkdir -p $(BINDIR)
	$(MAKE) $(BINDIR)/q2ded.exe

$(BUILDDIR)/server/%.o: %.c
	@if [ -z $(QUIET) ]; then\
		echo "===> CC $<";\
	fi
	${Q}mkdir -p $(@D)
	${Q}$(CC) -c $(CFLAGS) $(INCLUDE) -o $@ $<

$(BINDIR)/q2ded.exe : CFLAGS += -DDEDICATED_ONLY

else # not Windows

server:
	@echo "===> Building q2ded"
	${Q}mkdir -p $(BINDIR)
	$(MAKE) $(BINDIR)/q2ded

$(BUILDDIR)/server/%.o: %.c
	@if [ -z $(QUIET) ]; then\
		echo "===> CC $<";\
	fi
	${Q}mkdir -p $(@D)
	${Q}$(CC) -c $(CFLAGS) $(INCLUDE) -o $@ $<

$(BINDIR)/q2ded : CFLAGS += -DDEDICATED_ONLY -Wno-unused-result

ifeq ($(YQ2_OSTYPE), FreeBSD)
$(BINDIR)/q2ded : LDLIBS += -lexecinfo
endif

ifeq ($(WITH_XDG),yes)
$(BINDIR)/q2ded : CFLAGS += -DUSE_XDG
endif

endif

# ----------


# The OpenGL 1.x renderer lib

ifeq ($(YQ2_OSTYPE), Windows)

ref_gl1:
	@echo "===> Building ref_gl1.dll"
	$(MAKE) $(BINDIR)/ref_gl1.dll

$(BINDIR)/ref_gl1.dll : LDFLAGS += -shared
$(BINDIR)/ref_gl1.dll : LDLIBS += -lopengl32

else ifeq ($(YQ2_OSTYPE), Darwin)

ref_gl1:
	@echo "===> Building ref_gl1.dylib"
	${Q}mkdir -p $(BINDIR)
	$(MAKE) $(BINDIR)/ref_gl1.dylib


ifeq ($(WITH_METAL_SHIM), yes)
# The OpenGL subset on Metal: no OpenGL framework is linked.
$(BINDIR)/ref_gl1.dylib : LDFLAGS += -shared -framework Metal -framework QuartzCore -framework Foundation
else
$(BINDIR)/ref_gl1.dylib : LDFLAGS += -shared -framework OpenGL
endif

# Both builds write release/ref_gl1.dylib from different objects: a
# stamp per build relinks it whenever WITH_METAL_SHIM changes. Switching
# to the OpenGL build also removes the shim's ref_gl1.metallib, which
# that build does not use.
REFGL1_MODE := $(if $(filter yes,$(WITH_METAL_SHIM)),metal,gl)

$(BINDIR)/ref_gl1.dylib : $(BUILDDIR)/ref_gl1.$(REFGL1_MODE).mode

$(BUILDDIR)/ref_gl1.$(REFGL1_MODE).mode:
	${Q}mkdir -p $(@D)
	${Q}rm -f $(BUILDDIR)/ref_gl1.*.mode
	$(if $(filter gl,$(REFGL1_MODE)),${Q}rm -f $(BINDIR)/ref_gl1.metallib)
	${Q}touch $@

ifeq ($(WITH_METAL_SHIM), yes)
MGL_DIR := src/client/refresh/gl1/metal
MGL_OBJDIR := $(BUILDDIR)/ref_gl1_metal
MGL_CFLAGS := -DMGL_RENDERER_SUFFIX=\"gl1-metal\"

ifeq ($(METAL_SHADERS), yes)
$(BINDIR)/ref_gl1.dylib : $(BINDIR)/ref_gl1.metallib
endif

# The shaders, loaded from next to the dylib at context creation.
$(BINDIR)/ref_gl1.metallib : $(MGL_DIR)/shaders.metal $(MGL_DIR)/mgl_texenv.h \
		src/client/refresh/metal/common/mtl_shader_math.h
	@echo "===> METAL $<"
	${Q}command -v xcrun >/dev/null 2>&1 || { echo "WITH_METAL_SHIM=yes needs xcrun and the Xcode Metal toolchain" >&2; exit 1; }
	${Q}xcrun -f metal >/dev/null 2>&1 || { echo "WITH_METAL_SHIM=yes needs the Metal compiler (xcrun metal); install the Xcode Metal toolchain" >&2; exit 1; }
	${Q}mkdir -p $(MGL_OBJDIR) $(@D)
	${Q}xcrun -sdk macosx metal -c $< -o $(MGL_OBJDIR)/shaders.air
	${Q}xcrun -sdk macosx metallib $(MGL_OBJDIR)/shaders.air -o $@

# The shim's own sources (C, and ARC Objective-C in mgl_layer.m), without the remap.
$(MGL_OBJDIR)/$(MGL_DIR)/%.o: $(MGL_DIR)/%.c
	@if [ -z $(QUIET) ]; then\
		echo "===> CC $<";\
	fi
	${Q}mkdir -p $(@D)
	${Q}$(CC) -c $(CFLAGS) $(MGL_CFLAGS) $(SDLCFLAGS) $(INCLUDE) -o $@ $<

$(MGL_OBJDIR)/$(MGL_DIR)/%.o: $(MGL_DIR)/%.m
	@if [ -z $(QUIET) ]; then\
		echo "===> CC $<";\
	fi
	${Q}mkdir -p $(@D)
	${Q}$(CC) -c -fobjc-arc $(CFLAGS) $(MGL_CFLAGS) $(SDLCFLAGS) $(INCLUDE) -o $@ $<

# The arithmetic shared with ref_metal (src/client/refresh/metal/common/,
# TASK85): plain C, no shim header and no remap.
$(MGL_OBJDIR)/src/client/refresh/metal/common/%.o: src/client/refresh/metal/common/%.c
	@if [ -z $(QUIET) ]; then\
		echo "===> CC $<";\
	fi
	${Q}mkdir -p $(@D)
	${Q}$(CC) -c $(CFLAGS) $(INCLUDE) -o $@ $<

# gl1 and the rest of ref_gl1, unchanged: <OpenGL/gl.h> resolves to the
# shim's subset header and shim_sdl.h remaps the SDL OpenGL calls.
$(MGL_OBJDIR)/%.o: %.c
	@if [ -z $(QUIET) ]; then\
		echo "===> CC $<";\
	fi
	${Q}mkdir -p $(@D)
	${Q}$(CC) -c $(CFLAGS) -I$(MGL_DIR)/include -include $(MGL_DIR)/shim_sdl.h $(SDLCFLAGS) $(INCLUDE) -o $@ $<
endif

else # not Windows or Darwin

ref_gl1:
	@echo "===> Building ref_gl1.so"
	$(MAKE) $(BINDIR)/ref_gl1.so


$(BINDIR)/ref_gl1.so : CFLAGS += -fPIC
$(BINDIR)/ref_gl1.so : LDFLAGS += -shared
$(BINDIR)/ref_gl1.so : LDLIBS += -lGL

endif # OS specific ref_gl1 stuff

$(BUILDDIR)/ref_gl1/%.o: %.c
	@if [ -z $(QUIET) ]; then\
		echo "===> CC $<";\
	fi
	${Q}mkdir -p $(@D)
	${Q}$(CC) -c $(CFLAGS) $(SDLCFLAGS) $(INCLUDE) -o $@ $<

# ----------

# The OpenGL 3.x renderer lib

ifeq ($(YQ2_OSTYPE), Windows)

ref_gl3:
	@echo "===> Building ref_gl3.dll"
	$(MAKE) $(BINDIR)/ref_gl3.dll

$(BINDIR)/ref_gl3.dll : GLAD_INCLUDE = -Isrc/client/refresh/gl3/glad/include
$(BINDIR)/ref_gl3.dll : LDFLAGS += -shared

else ifeq ($(YQ2_OSTYPE), Darwin)

ref_gl3:
	@echo "===> Building ref_gl3.dylib"
	$(MAKE) $(BINDIR)/ref_gl3.dylib

$(BINDIR)/ref_gl3.dylib : GLAD_INCLUDE = -Isrc/client/refresh/gl3/glad/include
$(BINDIR)/ref_gl3.dylib : LDFLAGS += -shared

else # not Windows or Darwin

ref_gl3:
	@echo "===> Building ref_gl3.so"
	$(MAKE) $(BINDIR)/ref_gl3.so

$(BINDIR)/ref_gl3.so : GLAD_INCLUDE = -Isrc/client/refresh/gl3/glad/include
$(BINDIR)/ref_gl3.so : CFLAGS += -fPIC
$(BINDIR)/ref_gl3.so : LDFLAGS += -shared

endif # OS specific ref_gl3 stuff

$(BUILDDIR)/ref_gl3/%.o: %.c
	@if [ -z $(QUIET) ]; then\
		echo "===> CC $<";\
	fi
	${Q}mkdir -p $(@D)
	${Q}$(CC) -c $(CFLAGS) $(SDLCFLAGS) $(INCLUDE) $(GLAD_INCLUDE) -o $@ $<

# ----------

# The soft renderer lib

ifeq ($(YQ2_OSTYPE), Windows)

ref_soft:
	@echo "===> Building ref_soft.dll"
	$(MAKE) $(BINDIR)/ref_soft.dll

$(BINDIR)/ref_soft.dll : LDFLAGS += -shared

else ifeq ($(YQ2_OSTYPE), Darwin)

ref_soft:
	@echo "===> Building ref_soft.dylib"
	$(MAKE) $(BINDIR)/ref_soft.dylib

$(BINDIR)/ref_soft.dylib : LDFLAGS += -shared

else # not Windows or Darwin

ref_soft:
	@echo "===> Building ref_soft.so"
	$(MAKE) $(BINDIR)/ref_soft.so

$(BINDIR)/ref_soft.so : CFLAGS += -fPIC
$(BINDIR)/ref_soft.so : LDFLAGS += -shared

endif # OS specific ref_soft stuff

$(BUILDDIR)/ref_soft/%.o: %.c
	@if [ -z $(QUIET) ]; then\
		echo "===> CC $<";\
	fi
	${Q}mkdir -p $(@D)
	${Q}$(CC) -c $(CFLAGS) $(SDLCFLAGS) $(INCLUDE) -o $@ $<

# ----------

# The native Metal renderer lib, macOS only and not part of "all"
# (docs/plans/ref-metal-native.md, section 10). C and ARC Objective-C;
# no OpenGL is linked, SDL is the executable's (SDL_DYNAMIC_LOOKUP).

ifeq ($(YQ2_OSTYPE), Darwin)

ref_metal:
	@echo "===> Building ref_metal.dylib"
	${Q}mkdir -p $(BINDIR)
	$(MAKE) $(BINDIR)/ref_metal.dylib

$(BINDIR)/ref_metal.dylib : LDFLAGS += -shared -framework Metal -framework QuartzCore -framework Foundation
# MetalFX (decision 0011): ref_metal only, weak, as a target OS may lack it
$(BINDIR)/ref_metal.dylib : LDFLAGS += -weak_framework MetalFX

# The shaders, loaded from next to the dylib when the context is made,
# compiled with the shim's xcrun metal flags (design section 4).
ifeq ($(METAL_SHADERS), yes)
$(BINDIR)/ref_metal.dylib : $(BINDIR)/ref_metal.metallib
endif

$(BINDIR)/ref_metal.metallib : src/client/refresh/metal/rm_shaders.metal \
		src/client/refresh/metal/rm_rt.metal \
		src/client/refresh/metal/rm_gbuf.metal \
		src/client/refresh/metal/rm_rtshadow.metal \
		src/client/refresh/metal/rm_rtao.metal \
		src/client/refresh/metal/rm_rtsun.metal \
		src/client/refresh/metal/rm_rtwater.metal \
		src/client/refresh/metal/common/mtl_shader_math.h \
		src/client/refresh/metal/common/mtl_gbuf_math.h \
		src/client/refresh/metal/common/mtl_rts_math.h \
		src/client/refresh/metal/common/mtl_rtao_math.h \
		src/client/refresh/metal/common/mtl_rtsun_math.h \
		src/client/refresh/metal/common/mtl_rtw_math.h
	@echo "===> METAL $<"
	${Q}command -v xcrun >/dev/null 2>&1 || { echo "ref_metal needs xcrun and the Xcode Metal toolchain" >&2; exit 1; }
	${Q}xcrun -f metal >/dev/null 2>&1 || { echo "ref_metal needs the Metal compiler (xcrun metal); install the Xcode Metal toolchain" >&2; exit 1; }
	${Q}mkdir -p $(BUILDDIR)/ref_metal $(@D)
	${Q}xcrun -sdk $(METAL_SDK) metal -c $< -o $(BUILDDIR)/ref_metal/rm_shaders.air
	${Q}xcrun -sdk $(METAL_SDK) metal -c src/client/refresh/metal/rm_rt.metal -o $(BUILDDIR)/ref_metal/rm_rt.air
	${Q}xcrun -sdk $(METAL_SDK) metal -c src/client/refresh/metal/rm_gbuf.metal -o $(BUILDDIR)/ref_metal/rm_gbuf.air
	${Q}xcrun -sdk $(METAL_SDK) metal -c src/client/refresh/metal/rm_rtshadow.metal -o $(BUILDDIR)/ref_metal/rm_rtshadow.air
	${Q}xcrun -sdk $(METAL_SDK) metal -c src/client/refresh/metal/rm_rtao.metal -o $(BUILDDIR)/ref_metal/rm_rtao.air
	${Q}xcrun -sdk $(METAL_SDK) metal -c src/client/refresh/metal/rm_rtsun.metal -o $(BUILDDIR)/ref_metal/rm_rtsun.air
	${Q}xcrun -sdk $(METAL_SDK) metal -c src/client/refresh/metal/rm_rtwater.metal -o $(BUILDDIR)/ref_metal/rm_rtwater.air
	${Q}xcrun -sdk $(METAL_SDK) metallib $(BUILDDIR)/ref_metal/rm_shaders.air $(BUILDDIR)/ref_metal/rm_rt.air \
		$(BUILDDIR)/ref_metal/rm_gbuf.air $(BUILDDIR)/ref_metal/rm_rtshadow.air \
		$(BUILDDIR)/ref_metal/rm_rtao.air $(BUILDDIR)/ref_metal/rm_rtsun.air $(BUILDDIR)/ref_metal/rm_rtwater.air -o $@

# Header dependencies: the Darwin CFLAGS carry no -MMD (see above), but
# the Apple clang this target needs writes them for C and Objective-C
# alike; REFMETAL_DEPS includes the .d files.
REFMETAL_DEPFLAGS = -MMD -MP -MF $(@:.o=.d)

$(BUILDDIR)/ref_metal/%.o: %.c
	@if [ -z $(QUIET) ]; then\
		echo "===> CC $<";\
	fi
	${Q}mkdir -p $(@D)
	${Q}$(CC) -c $(CFLAGS) $(REFMETAL_DEPFLAGS) $(SDLCFLAGS) $(INCLUDE) -o $@ $<

$(BUILDDIR)/ref_metal/%.o: %.m
	@if [ -z $(QUIET) ]; then\
		echo "===> CC $<";\
	fi
	${Q}mkdir -p $(@D)
	${Q}$(CC) -c -fobjc-arc $(CFLAGS) $(REFMETAL_DEPFLAGS) $(SDLCFLAGS) $(INCLUDE) -o $@ $<

else

ref_metal:
	@echo "ref_metal is for macOS only, not for $(YQ2_OSTYPE)" >&2; exit 1

endif # OS specific ref_metal stuff

# ----------

# The baseq2 game
ifeq ($(YQ2_OSTYPE), Windows)
game:
	@echo "===> Building baseq2/game.dll"
	${Q}mkdir -p $(BINDIR)/baseq2
	$(MAKE) $(BINDIR)/baseq2/game.dll

$(BUILDDIR)/baseq2/%.o: %.c
	@if [ -z $(QUIET) ]; then\
		echo "===> CC $<";\
	fi
	${Q}mkdir -p $(@D)
	${Q}$(CC) -c $(CFLAGS) $(INCLUDE) -o $@ $<

$(BINDIR)/baseq2/game.dll : LDFLAGS += -shared

else ifeq ($(YQ2_OSTYPE), Darwin)

game:
	@echo "===> Building baseq2/game.dylib"
	${Q}mkdir -p $(BINDIR)/baseq2
	$(MAKE) $(BINDIR)/baseq2/game.dylib

$(BUILDDIR)/baseq2/%.o: %.c
	@if [ -z $(QUIET) ]; then\
		echo "===> CC $<";\
	fi
	${Q}mkdir -p $(@D)
	${Q}$(CC) -c $(CFLAGS) $(INCLUDE) -o $@ $<

$(BINDIR)/baseq2/game.dylib : CFLAGS += -fPIC
$(BINDIR)/baseq2/game.dylib : LDFLAGS += -shared

else # not Windows or Darwin

game:
	@echo "===> Building baseq2/game.so"
	${Q}mkdir -p $(BINDIR)/baseq2
	$(MAKE) $(BINDIR)/baseq2/game.so

$(BUILDDIR)/baseq2/%.o: %.c
	@if [ -z $(QUIET) ]; then\
		echo "===> CC $<";\
	fi
	${Q}mkdir -p $(@D)
	${Q}$(CC) -c $(CFLAGS) $(INCLUDE) -o $@ $<

$(BINDIR)/baseq2/game.so : CFLAGS += -fPIC -Wno-unused-result
$(BINDIR)/baseq2/game.so : LDFLAGS += -shared
endif

# ----------

# Used by the game
GAME_OBJS_ = \
	src/common/shared/flash.o \
	src/common/shared/rand.o \
	src/common/shared/shared.o \
	src/game/g_ai.o \
	src/game/g_chase.o \
	src/game/g_cmds.o \
	src/game/g_combat.o \
	src/game/g_func.o \
	src/game/g_items.o \
	src/game/g_main.o \
	src/game/g_misc.o \
	src/game/g_monster.o \
	src/game/g_phys.o \
	src/game/g_spawn.o \
	src/game/g_svcmds.o \
	src/game/g_target.o \
	src/game/g_trigger.o \
	src/game/g_turret.o \
	src/game/g_utils.o \
	src/game/g_weapon.o \
	src/game/monster/berserker/berserker.o \
	src/game/monster/boss2/boss2.o \
	src/game/monster/boss3/boss3.o \
	src/game/monster/boss3/boss31.o \
	src/game/monster/boss3/boss32.o \
	src/game/monster/brain/brain.o \
	src/game/monster/chick/chick.o \
	src/game/monster/flipper/flipper.o \
	src/game/monster/float/float.o \
	src/game/monster/flyer/flyer.o \
	src/game/monster/gladiator/gladiator.o \
	src/game/monster/gunner/gunner.o \
	src/game/monster/hover/hover.o \
	src/game/monster/infantry/infantry.o \
	src/game/monster/insane/insane.o \
	src/game/monster/medic/medic.o \
	src/game/monster/misc/move.o \
	src/game/monster/mutant/mutant.o \
	src/game/monster/parasite/parasite.o \
	src/game/monster/soldier/soldier.o \
	src/game/monster/supertank/supertank.o \
	src/game/monster/tank/tank.o \
	src/game/player/client.o \
	src/game/player/hud.o \
	src/game/player/trail.o \
	src/game/player/view.o \
	src/game/player/weapon.o \
	src/game/savegame/savegame.o

# ----------

# Used by the client
CLIENT_OBJS_ := \
	src/backends/generic/misc.o \
	src/client/cl_cin.o \
	src/client/cl_cinsize.o \
	src/client/cl_image.o \
	src/client/cl_conback.o \
	src/client/cl_console.o \
	src/client/cl_download.o \
	src/client/cl_effects.o \
	src/client/cl_entities.o \
	src/client/cl_input.o \
	src/client/cl_inventory.o \
	src/client/cl_keyboard.o \
	src/client/cl_lights.o \
	src/client/cl_main.o \
	src/client/cl_pins.o \
	src/client/cl_network.o \
	src/client/cl_parse.o \
	src/client/cl_particles.o \
	src/client/cl_prediction.o \
	src/client/cl_screen.o \
	src/client/cl_tempentities.o \
	src/client/cl_upscale.o \
	src/client/cl_upscale_cmd.o \
	src/client/cl_view.o \
	src/client/input/gyro.o \
	src/client/input/touch.o \
	src/client/menu/menu.o \
	src/client/menu/qmenu.o \
	src/client/menu/vidrules.o \
	src/client/menu/videomenu.o \
	src/client/refresh/files/imgdecode.o \
	src/client/refresh/files/imgscale.o \
	src/client/sound/ogg.o \
	src/client/sound/sdl.o \
	src/client/sound/sound.o \
	src/client/sound/wave.o \
	src/client/vid/glimp_hidpi.o \
	src/client/vid/refrecord.o \
	src/client/vid/vid.o \
	src/common/argproc.o \
	src/common/clientserver.o \
	src/common/collision.o \
	src/common/crc.o \
	src/common/cmdparser.o \
	src/common/cvar.o \
	src/common/filesystem.o \
	src/common/glob.o \
	src/common/md4.o \
	src/common/movemsg.o \
	src/common/frame.o \
	src/common/netchan.o \
	src/common/pmove.o \
	src/common/szone.o \
	src/common/zone.o \
	src/common/shared/flash.o \
	src/common/shared/rand.o \
	src/common/shared/shared.o \
	src/server/sv_cmd.o \
	src/server/sv_conless.o \
	src/server/sv_entities.o \
	src/server/sv_game.o \
	src/server/sv_init.o \
	src/server/sv_main.o \
	src/server/sv_save.o \
	src/server/sv_send.o \
	src/server/sv_user.o \
	src/server/sv_world.o

CLIENT_OBJS_ += \
	src/client/input/sdl3.o \
	src/client/vid/glimp_sdl3.o

# The metalfx backend of upscale_textures: the macOS client only, with
# MetalFX weak-linked (decision 0012); elsewhere cl_upscale_cmd.c says
# it is not built.
ifeq ($(YQ2_OSTYPE)$(IOS_MODE), Darwin)
CLIENT_OBJS_ += src/client/cl_upscale_metalfx.o
UPSCALE_METALFX_LDLIBS := -weak_framework MetalFX
else
UPSCALE_METALFX_LDLIBS :=
endif

ifeq ($(YQ2_OSTYPE), Windows)
CLIENT_OBJS_ += \
	src/backends/windows/main.o \
	src/backends/windows/network.o \
	src/backends/windows/system.o \
	src/backends/windows/shared/hunk.o
else
CLIENT_OBJS_ += \
	src/backends/unix/main.o \
	src/backends/unix/network.o \
	src/backends/unix/signalhandler.o \
	src/backends/unix/system.o \
	src/backends/unix/shared/hunk.o
endif

# ----------

REFGL1_OBJS_ := \
	src/client/refresh/gl1/qgl.o \
	src/client/refresh/gl1/gl1_draw.o \
	src/client/refresh/gl1/gl1_image.o \
	src/client/refresh/gl1/gl1_light.o \
	src/client/refresh/gl1/gl1_lightmap.o \
	src/client/refresh/gl1/gl1_main.o \
	src/client/refresh/gl1/gl1_mesh.o \
	src/client/refresh/gl1/gl1_misc.o \
	src/client/refresh/gl1/gl1_model.o \
	src/client/refresh/gl1/gl1_scrap.o \
	src/client/refresh/gl1/gl1_surf.o \
	src/client/refresh/gl1/gl1_warp.o \
	src/client/refresh/gl1/gl1_sdl.o \
	src/client/refresh/gl1/gl1_buffer.o \
	src/client/refresh/files/common.o \
	src/client/refresh/files/surf.o \
	src/client/refresh/files/models.o \
	src/client/refresh/files/pcx.o \
	src/client/refresh/files/image.o \
	src/client/refresh/files/imgdecode.o \
	src/client/refresh/files/imgscale.o \
	src/client/refresh/files/wal.o \
	src/client/refresh/files/pvs.o \
	src/client/vid/glimp_hidpi.o \
	src/common/shared/shared.o \
	src/common/md4.o

ifeq ($(YQ2_OSTYPE), Windows)
REFGL1_OBJS_ += \
	src/backends/windows/shared/hunk.o
else # not Windows
REFGL1_OBJS_ += \
	src/backends/unix/shared/hunk.o
endif

# ----------

REFGL3_OBJS_ := \
	src/client/refresh/gl3/gl3_draw.o \
	src/client/refresh/gl3/gl3_image.o \
	src/client/refresh/gl3/gl3_light.o \
	src/client/refresh/gl3/gl3_lightmap.o \
	src/client/refresh/gl3/gl3_main.o \
	src/client/refresh/gl3/gl3_mesh.o \
	src/client/refresh/gl3/gl3_misc.o \
	src/client/refresh/gl3/gl3_model.o \
	src/client/refresh/gl3/gl3_sdl.o \
	src/client/refresh/gl3/gl3_surf.o \
	src/client/refresh/gl3/gl3_warp.o \
	src/client/refresh/gl3/gl3_shaders.o \
	src/client/refresh/files/common.o \
	src/client/refresh/files/surf.o \
	src/client/refresh/files/models.o \
	src/client/refresh/files/pcx.o \
	src/client/refresh/files/image.o \
	src/client/refresh/files/imgdecode.o \
	src/client/refresh/files/imgscale.o \
	src/client/refresh/files/wal.o \
	src/client/refresh/files/pvs.o \
	src/common/shared/shared.o \
	src/common/md4.o

REFGL3_OBJS_GLADE_ := \
	src/client/refresh/gl3/glad/src/glad.o

ifeq ($(YQ2_OSTYPE), Windows)
REFGL3_OBJS_ += \
	src/backends/windows/shared/hunk.o
else # not Windows
REFGL3_OBJS_ += \
	src/backends/unix/shared/hunk.o
endif

# ----------

REFSOFT_OBJS_ := \
	src/client/refresh/soft/sw_aclip.o \
	src/client/refresh/soft/sw_alias.o \
	src/client/refresh/soft/sw_bsp.o \
	src/client/refresh/soft/sw_draw.o \
	src/client/refresh/soft/sw_edge.o \
	src/client/refresh/soft/sw_image.o \
	src/client/refresh/soft/sw_light.o \
	src/client/refresh/soft/sw_main.o \
	src/client/refresh/soft/sw_misc.o \
	src/client/refresh/soft/sw_model.o \
	src/client/refresh/soft/sw_part.o \
	src/client/refresh/soft/sw_poly.o \
	src/client/refresh/soft/sw_polyset.o \
	src/client/refresh/soft/sw_rast.o \
	src/client/refresh/soft/sw_scan.o \
	src/client/refresh/soft/sw_sprite.o \
	src/client/refresh/soft/sw_surf.o \
	src/client/refresh/soft/sw_view.o \
	src/client/refresh/files/surf.o \
	src/client/refresh/files/common.o \
	src/client/refresh/files/models.o \
	src/client/refresh/files/pcx.o \
	src/client/refresh/files/image.o \
	src/client/refresh/files/imgdecode.o \
	src/client/refresh/files/imgscale.o \
	src/client/refresh/files/wal.o \
	src/client/refresh/files/pvs.o \
	src/client/vid/glimp_hidpi.o \
	src/common/shared/shared.o \
	src/common/md4.o

ifeq ($(YQ2_OSTYPE), Windows)
REFSOFT_OBJS_ += \
	src/backends/windows/shared/hunk.o
else # not Windows
REFSOFT_OBJS_ += \
	src/backends/unix/shared/hunk.o
endif

# ----------

REFMETAL_OBJS_ := \
	src/client/refresh/metal/rm_draw.o \
	src/client/refresh/metal/rm_effects.o \
	src/client/refresh/metal/rm_gbuf.o \
	src/client/refresh/metal/rm_gbufscene.o \
	src/client/refresh/metal/rm_image.o \
	src/client/refresh/metal/rm_light.o \
	src/client/refresh/metal/rm_lightmap.o \
	src/client/refresh/metal/rm_main.o \
	src/client/refresh/metal/rm_metalfx.o \
	src/client/refresh/metal/rm_fxscene.o \
	src/client/refresh/metal/rm_mesh.o \
	src/client/refresh/metal/rm_misc.o \
	src/client/refresh/metal/rm_model.o \
	src/client/refresh/metal/rm_pure.o \
	src/client/refresh/metal/rm_rt.o \
	src/client/refresh/metal/rm_rtscene.o \
	src/client/refresh/metal/rm_rtshadow.o \
	src/client/refresh/metal/rm_rtshadowscene.o \
	src/client/refresh/metal/rm_rtao.o \
	src/client/refresh/metal/rm_rtaoscene.o \
	src/client/refresh/metal/rm_rtsun.o \
	src/client/refresh/metal/rm_rtsunscene.o \
	src/client/refresh/metal/rm_rtwater.o \
	src/client/refresh/metal/rm_rtwaterscene.o \
	src/client/refresh/metal/rm_scrap.o \
	src/client/refresh/metal/rm_sdl.o \
	src/client/refresh/metal/rm_surf.o \
	src/client/refresh/metal/rm_warp.o \
	src/client/refresh/metal/rmb_device.o \
	src/client/refresh/metal/rmb_encode.o \
	src/client/refresh/metal/rmb_gbuf.o \
	src/client/refresh/metal/rmb_metalfx.o \
	src/client/refresh/metal/rmb_resource.o \
	src/client/refresh/metal/rmb_rt.o \
	src/client/refresh/metal/rmb_rtshadow.o \
	src/client/refresh/metal/rmb_rtao.o \
	src/client/refresh/metal/rmb_rtsun.o \
	src/client/refresh/metal/rmb_rtwater.o \
	src/client/refresh/metal/common/mtl_matrix.o \
	src/client/refresh/metal/common/mtl_ring.o \
	src/client/refresh/files/common.o \
	src/client/refresh/files/models.o \
	src/client/refresh/files/pcx.o \
	src/client/refresh/files/pvs.o \
	src/client/refresh/files/image.o \
	src/client/refresh/files/imgdecode.o \
	src/client/refresh/files/imgscale.o \
	src/client/refresh/files/surf.o \
	src/client/refresh/files/wal.o \
	src/client/vid/glimp_hidpi.o \
	src/backends/unix/shared/hunk.o \
	src/common/md4.o \
	src/common/shared/shared.o

# ----------

# Used by the server
SERVER_OBJS_ := \
	src/backends/generic/misc.o \
	src/common/argproc.o \
	src/common/clientserver.o \
	src/common/collision.o \
	src/common/crc.o \
	src/common/cmdparser.o \
	src/common/cvar.o \
	src/common/filesystem.o \
	src/common/glob.o \
	src/common/md4.o \
	src/common/frame.o \
	src/common/movemsg.o \
	src/common/netchan.o \
	src/common/pmove.o \
	src/common/szone.o \
	src/common/zone.o \
	src/common/shared/rand.o \
	src/common/shared/shared.o \
	src/server/sv_cmd.o \
	src/server/sv_conless.o \
	src/server/sv_entities.o \
	src/server/sv_game.o \
	src/server/sv_init.o \
	src/server/sv_main.o \
	src/server/sv_save.o \
	src/server/sv_send.o \
	src/server/sv_user.o \
	src/server/sv_world.o

ifeq ($(YQ2_OSTYPE), Windows)
SERVER_OBJS_ += \
	src/backends/windows/main.o \
	src/backends/windows/network.o \
	src/backends/windows/system.o \
	src/backends/windows/shared/hunk.o
else # not Windows
SERVER_OBJS_ += \
	src/backends/unix/main.o \
	src/backends/unix/network.o \
	src/backends/unix/signalhandler.o \
	src/backends/unix/system.o \
	src/backends/unix/shared/hunk.o
endif

# ----------

# Rewrite paths to our object directory.
CLIENT_OBJS = $(patsubst %,$(BUILDDIR)/client/%,$(CLIENT_OBJS_))
ifeq ($(WITH_METAL_SHIM), yes)
ifneq ($(YQ2_OSTYPE), Darwin)
$(error WITH_METAL_SHIM=yes is for macOS only)
endif
# The shim build keeps its objects apart, they are compiled with other flags.
REFGL1_OBJS = $(patsubst %,$(BUILDDIR)/ref_gl1_metal/%,$(REFGL1_OBJS_)) \
	$(patsubst %,$(BUILDDIR)/ref_gl1_metal/src/client/refresh/gl1/metal/%,\
		mgl_state.o mgl_draw.o mgl_tex.o mgl_raster.o mgl_frame.o mgl_layer.o) \
	$(patsubst %,$(BUILDDIR)/ref_gl1_metal/src/client/refresh/metal/common/%,\
		mtl_matrix.o mtl_ring.o)
else
REFGL1_OBJS = $(patsubst %,$(BUILDDIR)/ref_gl1/%,$(REFGL1_OBJS_))
endif
REFGL3_OBJS = $(patsubst %,$(BUILDDIR)/ref_gl3/%,$(REFGL3_OBJS_))
REFGL3_OBJS += $(patsubst %,$(BUILDDIR)/ref_gl3/%,$(REFGL3_OBJS_GLADE_))
REFSOFT_OBJS = $(patsubst %,$(BUILDDIR)/ref_soft/%,$(REFSOFT_OBJS_))
REFMETAL_OBJS = $(patsubst %,$(BUILDDIR)/ref_metal/%,$(REFMETAL_OBJS_))

# rm_rtsunscene.c builds in the sun's per-map table (.incbin, a path from the
# top directory, where the compiler runs), so the table is its prerequisite (TASK56).
$(BUILDDIR)/ref_metal/src/client/refresh/metal/rm_rtsunscene.o: scripts/rt/sun-angles.txt

SERVER_OBJS = $(patsubst %,$(BUILDDIR)/server/%,$(SERVER_OBJS_))
GAME_OBJS = $(patsubst %,$(BUILDDIR)/baseq2/%,$(GAME_OBJS_))

# ----------

# Generate header dependencies.
CLIENT_DEPS= $(CLIENT_OBJS:.o=.d)
GAME_DEPS= $(GAME_OBJS:.o=.d)
REFGL1_DEPS= $(REFGL1_OBJS:.o=.d)
REFGL3_DEPS= $(REFGL3_OBJS:.o=.d)
REFSOFT_DEPS= $(REFSOFT_OBJS:.o=.d)
REFMETAL_DEPS= $(REFMETAL_OBJS:.o=.d)
SERVER_DEPS= $(SERVER_OBJS:.o=.d)

# Suck header dependencies in.
-include $(CLIENT_DEPS)
-include $(GAME_DEPS)
-include $(REFGL1_DEPS)
-include $(REFGL3_DEPS)
-include $(REFMETAL_DEPS)
-include $(SERVER_DEPS)

# ----------

# STATIC_MODULES=1: the modules in the client executable. The game
# and the chosen renderer are each combined by "ld -r" into one
# relocatable object that exports only its entry point (GetGameAPI,
# GetRefAPI). Every other global of the module becomes local to that
# object, so the three copies of shared.c, hunk.c, md4.c and the
# helpers (the client's, the game's, the renderer's) neither clash nor
# bind to one another, as they did not as separate libraries. The
# modules reach the engine only through game_import_t and refimport_t,
# as before, and SDL through the executable's own copy. Checked on the
# executable by tests/check-static-modules.sh.
ifneq ($(STATIC_MODULES),1)
static_modules:
	@echo "static_modules needs STATIC_MODULES=1" >&2; exit 1
endif
ifeq ($(STATIC_MODULES),1)
STATIC_GAME_OBJ := $(BUILDDIR)/static/game.o
ifeq ($(STATIC_RENDERER),soft)
STATIC_REF_OBJS := $(REFSOFT_OBJS)
else ifeq ($(STATIC_RENDERER),gl1)
STATIC_REF_OBJS := $(REFGL1_OBJS)
STATIC_LDLIBS += -framework OpenGL
else
STATIC_REF_OBJS := $(REFMETAL_OBJS)
STATIC_LDLIBS += -framework Metal -framework QuartzCore -framework Foundation $(METALFX_LDLIBS)
endif
ifeq ($(STATIC_RENDERER)$(METAL_SHADERS), metalyes)
# ref_metal finds its shader library beside the executable (dladdr on
# the code, which now is the executable)
$(BINDIR)/quake2 : $(BINDIR)/ref_metal.metallib
endif
STATIC_REF_OBJ := $(BUILDDIR)/static/ref_$(STATIC_RENDERER).o
STATIC_LINK_OBJS := $(STATIC_GAME_OBJ) $(STATIC_REF_OBJ)

# The two combined objects and the two client objects that hold the
# loader seats, without the rest of the client or SDL: what
# tests/check-static-modules.sh checks in the gate.
static_modules: $(STATIC_LINK_OBJS) $(BUILDDIR)/client/src/backends/unix/system.o $(BUILDDIR)/client/src/client/vid/vid.o

# Only the client's own objects see the macros the loaders check.
$(BUILDDIR)/client/%.o : CFLAGS += -DSTATIC_MODULES -DSTATIC_RENDERER_NAME=\"$(STATIC_RENDERER)\"

$(STATIC_GAME_OBJ) : $(GAME_OBJS)
	@echo "===> LD -r $@"
	${Q}mkdir -p $(@D)
	${Q}$(LD) -r -arch $(YQ2_ARCH) -exported_symbol _GetGameAPI $(GAME_OBJS) -o $@

$(STATIC_REF_OBJ) : $(STATIC_REF_OBJS)
	@echo "===> LD -r $@"
	${Q}mkdir -p $(@D)
	${Q}$(LD) -r -arch $(YQ2_ARCH) -exported_symbol _GetRefAPI $(STATIC_REF_OBJS) -o $@
endif

# ----------

# The iOS mode (see IOS_SDK at the top): the app bundle. Its executable
# is the client of STATIC_MODULES=1 above, linked for the SDK, and the
# bundle holds it, the Info.plist made from stuff/ios/Info.plist.in,
# ref_metal.metallib (METAL_SHADERS=yes; ref_metal looks for it beside
# the executable) and, only with IOS_BUNDLE_DATA, that directory's
# baseq2/. The simulator's bundle is signed ad hoc; the device's
# (iphoneos) is left unsigned, for scripts/ios-device.sh to sign with
# the owner's identity and profile. ios_objects builds the engine's objects
# and the Info.plist only, without SDL and the link: what
# tests/check-ios.sh checks in the gate.
ifeq ($(IOS_MODE),1)
IOS_APP := $(IOS_ROOT)/Quake2.app
IOS_PLIST := $(IOS_ROOT)/Info.plist
# CFBundleVersion takes digits and dots only: 8.71 of 8.71pre
IOS_VERSION := $(shell sed -n 's/^.define YQ2VERSION "\([0-9][0-9.]*\).*/\1/p' src/common/header/common.h)
ifeq ($(IOS_VERSION),)
$(error no YQ2VERSION found in src/common/header/common.h for the Info.plist)
endif

.PHONY : ios_plist

ios_plist:
	@echo "===> PLIST $(IOS_PLIST)"
	${Q}mkdir -p '$(dir $(IOS_PLIST))'
	${Q}sed -e 's/@BUNDLE_ID@/$(IOS_BUNDLE_ID)/g' -e 's/@VERSION@/$(IOS_VERSION)/g' \
		-e 's/@PLATFORM@/$(IOS_PLATFORM)/g' -e 's/@MIN_OS@/$(IOS_MIN)/g' \
		stuff/ios/Info.plist.in > '$(IOS_PLIST).tmp'
	${Q}plutil -lint -s '$(IOS_PLIST).tmp'
	${Q}mv '$(IOS_PLIST).tmp' '$(IOS_PLIST)'

ios_objects: $(CLIENT_OBJS) $(STATIC_LINK_OBJS) ios_plist

# the compile flags the executable's target gives its objects (the
# client section above), so ios_objects compiles what ios links
ios_objects : CFLAGS += -Wno-unused-result
ifeq ($(WITH_EXECINFO),yes)
ios_objects : CFLAGS += -DHAVE_EXECINFO
endif
ifeq ($(WITH_XDG),yes)
ios_objects : CFLAGS += -DUSE_XDG
endif

ios: $(BINDIR)/quake2 ios_plist
	@echo "===> APP $(IOS_APP)"
	${Q}rm -rf '$(IOS_APP)'
	${Q}mkdir -p '$(IOS_APP)'
	${Q}cp '$(BINDIR)/quake2' '$(IOS_APP)/quake2'
	${Q}cp '$(IOS_PLIST)' '$(IOS_APP)/Info.plist'
ifeq ($(METAL_SHADERS), yes)
	${Q}cp '$(BINDIR)/ref_metal.metallib' '$(IOS_APP)/ref_metal.metallib'
endif
ifneq ($(IOS_BUNDLE_DATA),)
	${Q}test -d '$(IOS_BUNDLE_DATA)/baseq2' || { echo "IOS_BUNDLE_DATA has no baseq2 directory: $(IOS_BUNDLE_DATA)" >&2; exit 1; }
	${Q}cp -R '$(IOS_BUNDLE_DATA)/baseq2' '$(IOS_APP)/baseq2'
endif
ifeq ($(IOS_SDK), iphonesimulator)
	${Q}codesign --force --sign - --timestamp=none '$(IOS_APP)'
endif
endif

# ----------

# quake2
ifeq ($(YQ2_OSTYPE), Windows)
$(BINDIR)/yquake2.exe : $(CLIENT_OBJS) icon
	@echo "===> LD $@"
	${Q}mkdir -p $(@D)
	${Q}$(CC) $(LDFLAGS) $(BUILDROOT)/icon/icon.res $(CLIENT_OBJS) $(LDLIBS) $(SDLLDFLAGS) -o $@
$(BINDIR)/quake2.exe : src/win-wrapper/wrapper.c icon
	$(Q)$(CC) -Wall -mwindows $(BUILDROOT)/icon/icon.res src/win-wrapper/wrapper.c -o $@
else ifeq ($(YQ2_OSTYPE), Darwin)
$(BINDIR)/quake2 : $(CLIENT_OBJS) $(SDL3_LIB)
$(BINDIR)/quake2 : $(STATIC_LINK_OBJS)
	@echo "===> LD $@"
	${Q}mkdir -p $(@D)
	${Q}$(CC) $(LDFLAGS) $(CLIENT_OBJS) $(STATIC_LINK_OBJS) $(LDLIBS) $(IMGDECODE_LDLIBS) $(UPSCALE_METALFX_LDLIBS) $(STATIC_LDLIBS) $(SDLLDFLAGS) -o $@
else
$(BINDIR)/quake2 : $(CLIENT_OBJS)
	@echo "===> LD $@"
	${Q}mkdir -p $(@D)
	${Q}$(CC) $(LDFLAGS) $(CLIENT_OBJS) $(LDLIBS) $(SDLLDFLAGS) -o $@
endif

# q2ded
ifeq ($(YQ2_OSTYPE), Windows)
$(BINDIR)/q2ded.exe : $(SERVER_OBJS) icon
	@echo "===> LD $@"
	${Q}mkdir -p $(@D)
	${Q}$(CC) $(LDFLAGS) $(BUILDROOT)/icon/icon.res $(SERVER_OBJS) $(LDLIBS) -o $@
else
$(BINDIR)/q2ded : $(SERVER_OBJS)
	@echo "===> LD $@"
	${Q}mkdir -p $(@D)
	${Q}$(CC) $(LDFLAGS) $(SERVER_OBJS) $(LDLIBS) -o $@
endif

# ref_gl1.so
ifeq ($(YQ2_OSTYPE), Windows)
$(BINDIR)/ref_gl1.dll : $(REFGL1_OBJS)
	@echo "===> LD $@"
	${Q}mkdir -p $(@D)
	${Q}$(CC) $(LDFLAGS) $(REFGL1_OBJS) $(LDLIBS) $(DLL_SDLLDFLAGS) -o $@
else ifeq ($(YQ2_OSTYPE), Darwin)
$(BINDIR)/ref_gl1.dylib : $(REFGL1_OBJS)
	@echo "===> LD $@"
	${Q}mkdir -p $(@D)
	${Q}$(CC) $(LDFLAGS) $(REFGL1_OBJS) $(LDLIBS) $(IMGDECODE_LDLIBS) $(SDL_DYNAMIC_LOOKUP) -o $@
else
$(BINDIR)/ref_gl1.so : $(REFGL1_OBJS)
	@echo "===> LD $@"
	${Q}mkdir -p $(@D)
	${Q}$(CC) $(LDFLAGS) $(REFGL1_OBJS) $(LDLIBS) $(SDLLDFLAGS) -o $@
endif

# ref_gl3.so
ifeq ($(YQ2_OSTYPE), Windows)
$(BINDIR)/ref_gl3.dll : $(REFGL3_OBJS)
	@echo "===> LD $@"
	${Q}mkdir -p $(@D)
	${Q}$(CC) $(LDFLAGS) $(REFGL3_OBJS) $(LDLIBS) $(DLL_SDLLDFLAGS) -o $@
else ifeq ($(YQ2_OSTYPE), Darwin)
$(BINDIR)/ref_gl3.dylib : $(REFGL3_OBJS)
	@echo "===> LD $@"
	${Q}mkdir -p $(@D)
	${Q}$(CC) $(LDFLAGS) $(REFGL3_OBJS) $(LDLIBS) $(IMGDECODE_LDLIBS) $(SDL_DYNAMIC_LOOKUP) -o $@
else
$(BINDIR)/ref_gl3.so : $(REFGL3_OBJS)
	@echo "===> LD $@"
	${Q}mkdir -p $(@D)
	${Q}$(CC) $(LDFLAGS) $(REFGL3_OBJS) $(LDLIBS) $(SDLLDFLAGS) -o $@
endif

# ref_soft.so
ifeq ($(YQ2_OSTYPE), Windows)
$(BINDIR)/ref_soft.dll : $(REFSOFT_OBJS)
	@echo "===> LD $@"
	${Q}mkdir -p $(@D)
	${Q}$(CC) $(LDFLAGS) $(REFSOFT_OBJS) $(LDLIBS) $(DLL_SDLLDFLAGS) -o $@
else ifeq ($(YQ2_OSTYPE), Darwin)
$(BINDIR)/ref_soft.dylib : $(REFSOFT_OBJS)
	@echo "===> LD $@"
	${Q}mkdir -p $(@D)
	${Q}$(CC) $(LDFLAGS) $(REFSOFT_OBJS) $(LDLIBS) $(IMGDECODE_LDLIBS) $(SDL_DYNAMIC_LOOKUP) -o $@
else
$(BINDIR)/ref_soft.so : $(REFSOFT_OBJS)
	@echo "===> LD $@"
	${Q}mkdir -p $(@D)
	${Q}$(CC) $(LDFLAGS) $(REFSOFT_OBJS) $(LDLIBS) $(SDLLDFLAGS) -o $@
endif

# ref_metal.dylib
ifeq ($(YQ2_OSTYPE), Darwin)
$(BINDIR)/ref_metal.dylib : $(REFMETAL_OBJS)
	@echo "===> LD $@"
	${Q}mkdir -p $(@D)
	${Q}$(CC) $(LDFLAGS) $(REFMETAL_OBJS) $(LDLIBS) $(IMGDECODE_LDLIBS) $(SDL_DYNAMIC_LOOKUP) -o $@
endif

# baseq2/game.so
ifeq ($(YQ2_OSTYPE), Windows)
$(BINDIR)/baseq2/game.dll : $(GAME_OBJS)
	@echo "===> LD $@"
	${Q}mkdir -p $(@D)
	${Q}$(CC) $(LDFLAGS) $(GAME_OBJS) $(LDLIBS) -o $@
else ifeq ($(YQ2_OSTYPE), Darwin)
$(BINDIR)/baseq2/game.dylib : $(GAME_OBJS)
	@echo "===> LD $@"
	${Q}mkdir -p $(@D)
	${Q}$(CC) $(LDFLAGS) $(GAME_OBJS) $(LDLIBS) -o $@
else
$(BINDIR)/baseq2/game.so : $(GAME_OBJS)
	@echo "===> LD $@"
	${Q}mkdir -p $(@D)
	${Q}$(CC) $(LDFLAGS) $(GAME_OBJS) $(LDLIBS) -o $@
endif

# ----------

# The unit-test suite (tests/unit, see doc/085_unittests.md). The test
# binary links the dedicated server's own objects (SERVER_OBJS_ below,
# built with q2ded's flags into a tree of their own) except the unix
# main.c; tests/unit/main.c supplies main(). Ten client objects
# (cl_conback, cl_download, cl_entities, cl_network, cl_parse, cl_screen,
# the upscaler's checks and pixel arithmetic cl_upscale, the sound
# frontend sound.c, the window code's SDL-free part vid/glimp_hidpi.c and
# the video menu's rules menu/vidrules.c)
# are linked against the stubs in tests/unit/stub_client.c
# and stub_client_full.c, and three renderer objects, refresh/files/imgscale.c, refresh/files/surf.c
# and soft/sw_view.c, which need only shared.c; none of it includes SDL. Needs no SDL and no game
# data. `make test` builds and runs it; UPDATE_PINS=1 (exactly 1, any
# other value is ignored) rewrites the pins under tests/pins/unit/
# instead of comparing; TEST_ARGS passes options or test-name filters to
# the runner. scripts/test.sh and scripts/coverage.sh clear both on the
# make command line, so the gate always compares and runs every test. COVERAGE=1 builds an instrumented
# copy under build/coverage (scripts/coverage.sh drives that).
ifneq ($(YQ2_OSTYPE), Windows)

ifdef COVERAGE
override TESTDIR := $(BUILDROOT)/coverage
ifeq ($(COMPILER), clang)
TEST_COVFLAGS := -O0 -fprofile-instr-generate -fcoverage-mapping
else
TEST_COVFLAGS := -O0 --coverage
endif
else
override TESTDIR := $(BUILDDIR)/test
TEST_COVFLAGS :=
endif

TEST_CFLAGS := -DDEDICATED_ONLY -Wno-unused-result $(TEST_COVFLAGS)
# An undefined-behaviour report ends the test run instead of being printed
# and passed over. Only the test objects get this, never a shipped binary.
ifdef UBSAN
TEST_CFLAGS += -fno-sanitize-recover=undefined
endif
ifeq ($(WITH_XDG),yes)
TEST_CFLAGS += -DUSE_XDG
endif

TEST_SRCS := $(sort $(wildcard tests/unit/*.c))
TEST_OBJS = $(patsubst %,$(TESTDIR)/%,$(filter-out src/backends/unix/main.o,$(SERVER_OBJS_))) \
	$(TESTDIR)/src/client/cl_cinsize.o \
	$(TESTDIR)/src/client/cl_conback.o \
	$(TESTDIR)/src/client/cl_download.o \
	$(TESTDIR)/src/client/cl_entities.o \
	$(TESTDIR)/src/client/cl_network.o \
	$(TESTDIR)/src/client/cl_parse.o \
	$(TESTDIR)/src/client/cl_screen.o \
	$(TESTDIR)/src/client/cl_upscale.o \
	$(TESTDIR)/src/client/input/touch.o \
	$(TESTDIR)/src/client/menu/vidrules.o \
	$(TESTDIR)/src/client/sound/sound.o \
	$(TESTDIR)/src/client/sound/wave.o \
	$(TESTDIR)/src/client/vid/glimp_hidpi.o \
	$(TESTDIR)/src/client/refresh/files/imgscale.o \
	$(TESTDIR)/src/client/refresh/files/surf.o \
	$(TESTDIR)/src/client/refresh/soft/sw_view.o \
	$(patsubst %.c,$(TESTDIR)/%.o,$(TEST_SRCS))
TEST_BIN = $(TESTDIR)/yq2test

# Header dependencies of the test objects (-MMD, not on Darwin), so that
# an incremental `make test` after a header edit rebuilds what uses it.
-include $(TEST_OBJS:.o=.d)

.PHONY : test

test: $(TEST_BIN)
	@echo "===> TEST $(TEST_BIN)"
	${Q}$(TEST_BIN) $(if $(filter 1,$(UPDATE_PINS)),--update-pins) $(TEST_ARGS)

$(TESTDIR)/%.o: %.c
	@if [ -z $(QUIET) ]; then\
		echo "===> CC $< (test)";\
	fi
	${Q}mkdir -p $(@D)
	${Q}$(CC) -c $(CFLAGS) $(TEST_CFLAGS) $(INCLUDE) -o $@ $<

$(patsubst %.c,$(TESTDIR)/%.o,$(TEST_SRCS)) : tests/unit/test.h

$(TEST_BIN) : $(TEST_OBJS)
	@echo "===> LD $@"
	${Q}mkdir -p $(@D)
	${Q}$(CC) $(LDFLAGS) $(TEST_COVFLAGS) $(TEST_OBJS) $(LDLIBS) -o $@

# test_ipfilter.c includes the game's g_svcmds.c whole
$(TESTDIR)/tests/unit/test_ipfilter.o : src/game/g_svcmds.c

endif

# ----------
