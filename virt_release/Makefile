obj-m := virt_release.o
virt_release-y =  virt_release_m.o
    KERNELBUILD :=/lib/modules/$(shell uname -r)/build  
default:
	make -C $(KERNELBUILD) M=$(shell pwd) modules  
clean:
	rm -rf *.o *.ko *.mod.c .*.cmd *.markers *.order *.symvers .tmp_versions 
