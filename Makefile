AURABUILD_CI ?= 0
AURABUILD_STATIC ?= 0
AURABUILD_CPR ?= 1
AURABUILD_DPP ?= 1
AURABUILD_MDNS ?= 0
AURABUILD_MINIUPNP ?= 1
AURABUILD_PJASS ?= 0

AURABUILD_CI := $(strip $(AURABUILD_CI))
AURABUILD_STATIC := $(strip $(AURABUILD_STATIC))
AURABUILD_CPR := $(strip $(AURABUILD_CPR))
AURABUILD_DPP := $(strip $(AURABUILD_DPP))
AURABUILD_MDNS := $(strip $(AURABUILD_MDNS))
AURABUILD_MINIUPNP := $(strip $(AURABUILD_MINIUPNP))
AURABUILD_PJASS := $(strip $(AURABUILD_PJASS))

# This doesn't work...
#define VALIDATE_BOOL
#ifneq ($($1),0)
#ifneq ($($1),1)
#$(error $1 must be 0 or 1, but got '$($1)')
#endif
#endif
#endef

#$(foreach flag,AURABUILD_STATIC AURABUILD_CPR AURABUILD_DPP AURABUILD_MDNS AURABUILD_MINIUPNP AURABUILD_PJASS,$(eval $(call VALIDATE_BOOL,$(flag))))

SHELL = /bin/sh
SYSTEM = $(shell uname)
ARCH = $(shell uname -m)
INSTALL_DIR = /usr

DFLAGS ?= -DNDEBUG
OFLAGS ?= -O3 -flto -fno-fat-lto-objects

CC ?= gcc
CXX ?= g++
CCFLAGS += -fno-builtin
CXXFLAGS += -g0 -std=c++17 -pipe -pthread $(WFLAGS) -fno-builtin -fno-rtti -MMD -MP

LDLIBS_CORE = -lstorm -lbncsutil -lgmp -lbz2 -lz
LDLIBS_SYS = 
LDLIBS_OPT = 
LDFLAGS_SYS = -pthread
LDFLAGS_PATHS = -L. -Llib/ -L/usr/local/lib/ -Ldeps/bncsutil/src/bncsutil/

ifeq ($(AURABUILD_STATIC),1)
  LDFLAGS_SYS += -static
endif

ifeq ($(AURABUILD_CPR),1)
  LDLIBS_OPT += -lcpr
else
  CPPFLAGS += -DDISABLE_CPR
endif

ifeq ($(AURABUILD_DPP),1)
  LDLIBS_OPT += -ldpp
else
  CPPFLAGS += -DDISABLE_DPP
endif

ifeq ($(AURABUILD_MINIUPNP),1)
  LDLIBS_OPT += -lminiupnpc
else
  CPPFLAGS += -DDISABLE_MINIUPNP
endif

CPPFLAGS += -DDISABLE_PJASS
CPPFLAGS += -DDISABLE_MDNS

ifeq ($(AURABUILD_PJASS),1)
$(error AURABUILD_PJASS is not yet supported. Please set AURABUILD_PJASS=0)
endif

ifeq ($(AURABUILD_MDNS),1)
$(error AURABUILD_MDNS is not yet supported. Please set AURABUILD_MDNS=0)
endif

ifeq ($(ARCH),x86_64)
  CCFLAGS += -m64
  CXXFLAGS += -m64
endif

ifeq ($(SYSTEM),Darwin)
  INSTALL_DIR = /usr/local
  CXXFLAGS += -stdlib=libc++
  CC = clang
  CXX = clang++
  CPPFLAGS += -D__APPLE__
else
  LDLIBS_SYS += -lrt
endif

ifeq ($(SYSTEM),FreeBSD)
  CPPFLAGS += -D__FREEBSD__
endif

ifeq ($(SYSTEM),SunOS)
  CPPFLAGS += -D__SOLARIS__
  LDLIBS_SYS += -lresolv -lsocket -lnsl
endif

LDLIBS_SYS += -ldl

CPPFLAGS += -DSQLITE_CUSTOM_INCLUDE=sqlite3opt.h -DCRC32_USE_LOOKUP_TABLE_SLICING_BY_16
CPPFLAGS += $(DFLAGS)
CPPFLAGS += -I. -Ilib/ -Ideps/bncsutil/src/ -Ideps/StormLib/src/ -Ideps/miniupnpc/include/ -Icpr-src/include/ -Idpp-src/include/

CCFLAGS += $(OFLAGS)
CXXFLAGS += $(OFLAGS)

LDFLAGS = $(LDFLAGS_SYS) $(LDFLAGS_PATHS)
LDLIBS = $(LDLIBS_CORE) $(LDLIBS_OPT) $(LDLIBS_SYS)

BUILDDIR := build/

OBJDIR := $(BUILDDIR)obj/

OBJS = $(OBJDIR)lib/base64/base64.o \
       $(OBJDIR)lib/csvparser/csvparser.o \
       $(OBJDIR)lib/crc32/crc32.o \
       $(OBJDIR)lib/sha1/sha1.o \
       $(OBJDIR)src/protocol/bnet_protocol.o \
       $(OBJDIR)src/protocol/game_protocol.o \
       $(OBJDIR)src/protocol/gps_protocol.o \
       $(OBJDIR)src/protocol/vlan_protocol.o \
       $(OBJDIR)src/config/config.o \
       $(OBJDIR)src/config/config_bot.o \
       $(OBJDIR)src/config/config_db.o \
       $(OBJDIR)src/config/config_realm.o \
       $(OBJDIR)src/config/config_commands.o \
       $(OBJDIR)src/config/config_game.o \
       $(OBJDIR)src/config/config_irc.o \
       $(OBJDIR)src/config/config_discord.o \
       $(OBJDIR)src/config/config_net.o \
       $(OBJDIR)src/proxy/gproxy_server.o \
       $(OBJDIR)src/proxy/tcp_proxy.o \
       $(OBJDIR)src/auradb.o \
       $(OBJDIR)src/bncsutil_interface.o \
       $(OBJDIR)src/mdns.o \
       $(OBJDIR)src/optional.o \
       $(OBJDIR)src/util.o \
       $(OBJDIR)src/file_util.o \
       $(OBJDIR)src/json.o \
       $(OBJDIR)src/os_util.o \
       $(OBJDIR)src/pjass.o \
       $(OBJDIR)src/map.o \
       $(OBJDIR)src/packed.o \
       $(OBJDIR)src/save_game.o \
       $(OBJDIR)src/socket.o \
       $(OBJDIR)src/connection.o \
       $(OBJDIR)src/net.o \
       $(OBJDIR)src/realm.o \
       $(OBJDIR)src/realm_chat.o \
       $(OBJDIR)src/realm_games.o \
       $(OBJDIR)src/async_observer.o \
       $(OBJDIR)src/game_controller_data.o \
       $(OBJDIR)src/game_host.o \
       $(OBJDIR)src/game_interactive_host.o \
       $(OBJDIR)src/game_result.o \
       $(OBJDIR)src/game_seeker.o \
       $(OBJDIR)src/game_setup.o \
       $(OBJDIR)src/game_slot.o \
       $(OBJDIR)src/game_stat.o \
       $(OBJDIR)src/game_structs.o \
       $(OBJDIR)src/game_user.o \
       $(OBJDIR)src/game_virtual_user.o \
       $(OBJDIR)src/game.o \
       $(OBJDIR)src/aura.o \
       $(OBJDIR)src/cli.o \
       $(OBJDIR)src/command.o \
       $(OBJDIR)src/command_history.o \
       $(OBJDIR)src/locations.o \
       $(OBJDIR)src/rate_limiter.o \
       $(OBJDIR)src/integration/discord.o \
       $(OBJDIR)src/integration/irc.o \
       $(OBJDIR)src/stats/dota.o \
       $(OBJDIR)src/stats/w3mmd.o \
       $(OBJDIR)src/test/runner.o

COBJS = $(OBJDIR)lib/sqlite3/sqlite3.o

DIRS = $(sort $(dir $(OBJS) $(COBJS)))

PROG = aura

SRC_OBJS = $(filter src/%, $(OBJS)) 
SRC_CPP  = $(patsubst %.o, %.cpp, $(SRC_OBJS))

all: $(OBJS) $(COBJS) $(PROG)
	@echo "Used CCFLAGS: $(CPPFLAGS) $(CCFLAGS)"
	@echo "Used CXXFLAGS: $(CPPFLAGS) $(CXXFLAGS)"

$(PROG): $(OBJS) $(COBJS)
	@$(CXX) -o aura $(OBJS) $(COBJS) $(CPPFLAGS) $(CXXFLAGS) $(LDFLAGS) $(LDLIBS)
	@echo "[BIN] $@ created."
	@strip "$(PROG)"
	@echo "[BIN] Stripping the binary."

clean:
	@rm -f $(OBJS) $(COBJS) $(PROG)
	@echo "Binary and object files cleaned."

install:
	@install -d "$(DESTDIR)$(INSTALL_DIR)/bin"
	@install $(PROG) "$(DESTDIR)$(INSTALL_DIR)/bin/$(PROG)"
	@echo "Binary $(PROG) installed to $(DESTDIR)$(INSTALL_DIR)/bin"

$(OBJS): $(OBJDIR)%.o: %.cpp $(dir $@)
	@$(CXX) -o $@ $(CPPFLAGS) $(CXXFLAGS) -c $<
	@echo "[$(CXX)] $@"

$(COBJS): $(OBJDIR)%.o: %.c $(dir $@)
	@$(CC) -o $@ $(CPPFLAGS) $(CCFLAGS) -c $<
	@echo "[$(CC)] $@"
  
$(OBJS) $(COBJS): | $(DIRS)

$(DIRS):
	mkdir -p $@

clang-tidy:
	@for file in $(SRC_CPP); do \
		clang-tidy "$$file" -fix -checks=* -header-filter="src/.* src/.*/.*" -- $(CPPFLAGS) $(CXXFLAGS); \
	done;

-include $(OBJS:.o=.d) $(COBJS:.o=.d)
