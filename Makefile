TARGET := ipmi-fru-it

SRC = ipmi-fru-it.c

OBJ = $(SRC:.c=.o)
DEP = $(OBJ:.o=.d)

INIPARSER 		:= iniparser
PARSER_DIR  	:= lib/$(INIPARSER)
PARSER_HEADERS 	:= $(PARSER_DIR)/src

HIDE     := @
CC       := gcc
CFLAGS   := -g -Wall
INCLUDES := -I $(PARSER_HEADERS)
LDFLAGS	 := -L $(PARSER_DIR) -liniparser -lz

ifeq (,$(strip $(filter $(MAKECMDGOALS),clean)))
	MAKEFLAGS+=--output-sync=target
	ifneq (,$(strip $(DEP)))
		-include $(DEP)
	endif
endif

%.d : %.c
	@printf "%b[1;36m%s%b[0m\n" "\0033" "Dependency: $< -> $@" "\0033"
	$(CC) -MM -MG -MT '$@ $(@:.d=.o)' $(CFLAGS) $(INCLUDES) -o $@ $<
	@printf "\n"

%.o : %.c
	@printf "%b[1;36m%s%b[0m\n" "\0033" "Compiling: $< -> $@" "\0033"
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@
	@printf "\n"

.PHONY: all clean
.DEFAULT_GOAL := all
all: $(TARGET)

$(INIPARSER):
	@printf "%b[1;36m%s%b[0m\n" "\0033" "Buidling: $@" "\0033"
	make -C $(PARSER_DIR)
	@printf "%b[1;32m%s%b[0m\n\n" "\0033" "$@ Done!" "\0033"

$(TARGET): $(OBJ) $(DEP) $(INIPARSER) Makefile
	@printf "%b[1;36m%s%b[0m\n" "\0033" "Buidling: $(OBJ) -> $@" "\0033"
	$(CC) -o $@ $(OBJ) $(LDFLAGS)
	@printf "%b[1;32m%s%b[0m\n\n" "\0033" "$@ Done!" "\0033"

RM_LIST = $(wildcard $(TARGET) *.o *.d)
clean:
	@printf "%b[1;36m%s%b[0m\n" "\0033" "Cleaning" "\0033"
ifneq (,$(RM_LIST))
	rm -rf $(RM_LIST)
	@printf "\n"
endif
	make -C $(PARSER_DIR) $@
	@printf "%b[1;32m%s%b[0m\n\n" "\0033" "Done!" "\0033"
