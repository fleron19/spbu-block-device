KVER ?= $(shell uname -r)
LK_BUILD_DIR ?= /lib/modules/$(KVER)/build
all: build

build:
	$(MAKE) -j -C $(LK_BUILD_DIR) M=$(CURDIR) modules
clean:
	$(MAKE) -j -C $(LK_BUILD_DIR) M=$(CURDIR) clean 
