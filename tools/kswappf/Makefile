# Build:  make KDIR=<6.6 swappf kernel build tree> TREES=<dir with native_tree_model_spatial.h>
# (trees from gen_single_tree_code.py --integer_bounds)
KDIR ?= /data1/shaurya/linux-6.6.160
TREES ?= $(CURDIR)/trees

obj-m := eden_swappf.o
ccflags-y := -I$(TREES) -I$(src)/../fltrace

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) TREES=$(TREES) modules

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean
