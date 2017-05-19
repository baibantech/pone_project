#CC=gcc
#INCLUDEDIR = /home/linux/include/
#MODCFLAGS :=-Wall -O -DMODULE -D__KERNEL__ -DLINUX -I$(INCLUDEDIR)
#map_driver.o: map_driver.c 
#	$(CC) $(MODCFLAGS) -c map_driver.c

obj-m := mapdr.o                   #要生成的模块名     
mapdr-objs:= map_driver.o        #生成这个模块名所需要的目标文件

KDIR := /lib/modules/`uname -r`/build
#KDIR := /home/yangzexin/work/pone_project/linux-4.4.1
PWD := $(shell pwd)

default:
	make -C $(KDIR) M=$(PWD) modules

clean:
	rm -rf *.o .* .cmd *.ko *.mod.c .tmp_version
