# MicroPython の embed ポートから、組み込み用のソース一式を生成する。
# 直接は呼ばず tools/setup_micropython.sh から使う。
#
#   make MICROPYTHON_TOP=<クローンの場所> PACKAGE_DIR=../embed
include $(MICROPYTHON_TOP)/py/mkenv.mk

# 自前のモジュールも QSTR の走査に含める。
EXTMOD_USED := modjson.c modre.c modbinascii.c modrandom.c modtime.c moddeflate.c
SRC_QSTR := $(wildcard mod_*.c) pd_file.c $(addprefix $(MICROPYTHON_TOP)/extmod/,$(EXTMOD_USED)) \
	$(MICROPYTHON_TOP)/shared/runtime/sys_stdio_mphal.c $(MICROPYTHON_TOP)/shared/runtime/pyexec.c \
	$(MICROPYTHON_TOP)/shared/readline/readline.c

include $(MICROPYTHON_TOP)/ports/embed/embed.mk

CFLAGS += -I$(MICROPYTHON_TOP)

.PHONY: extras
extras: micropython-embed-package
	$(Q)$(MKDIR) -p $(PACKAGE_DIR)/shared/readline $(PACKAGE_DIR)/shared/timeutils $(PACKAGE_DIR)/lib/re1.5 $(PACKAGE_DIR)/lib/uzlib
	$(Q)$(CP) $(TOP)/shared/runtime/pyexec.c $(TOP)/shared/runtime/pyexec.h $(PACKAGE_DIR)/shared/runtime
	$(Q)$(CP) $(TOP)/shared/runtime/sys_stdio_mphal.c $(TOP)/shared/runtime/stdout_helpers.c $(TOP)/shared/runtime/interrupt_char.c $(TOP)/shared/runtime/interrupt_char.h $(PACKAGE_DIR)/shared/runtime
	$(Q)$(CP) $(TOP)/shared/readline/readline.c $(TOP)/shared/readline/readline.h $(PACKAGE_DIR)/shared/readline
	$(Q)$(CP) $(TOP)/shared/timeutils/timeutils.c $(TOP)/shared/timeutils/timeutils.h $(PACKAGE_DIR)/shared/timeutils
	$(Q)$(CP) $(TOP)/lib/re1.5/*.c $(TOP)/lib/re1.5/*.h $(PACKAGE_DIR)/lib/re1.5
	$(Q)$(CP) $(TOP)/lib/uzlib/*.c $(TOP)/lib/uzlib/*.h $(PACKAGE_DIR)/lib/uzlib
	$(Q)$(CP) $(TOP)/extmod/moddeflate.c $(TOP)/extmod/modjson.c $(TOP)/extmod/modre.c $(TOP)/extmod/modbinascii.c $(TOP)/extmod/modrandom.c $(TOP)/extmod/modtime.c $(TOP)/extmod/modtime.h $(PACKAGE_DIR)/extmod
	$(Q)$(RM) -f $(PACKAGE_DIR)/shared/runtime/gchelper_generic.c 
	$(Q)$(RM) -rf $(PACKAGE_DIR)/port
