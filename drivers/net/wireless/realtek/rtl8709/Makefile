CC=mipsel-linux-gcc
EXTRA_CFLAGS += -O2
EXTRA_CFLAGS += -I$(src)/include 

#include .config

#ifeq ($(CONFIG_SDIO), y)
#$(shell cp $(PWD)/autoconf_sdio_linux.h include/autoconf.h)
# _OS_INTFS_FILES := os_intf/linux/os_intfs.o \
			os_intf/linux/sdio_intf.o \

# _HAL_INTFS_FILES := hal/RTL8711/hal_init.o \
			hal/RTL8711/sdio_halinit.o \
			hal/RTL8711/sdio_ops.o \
			hal/RTL8711/sdio_ops_linux.o \

#endif

#ifeq ($(CONFIG_USB), y)
 $(shell cp -f $(src)/autoconf_usb8192u_linux.h $(src)/include/autoconf.h)
 _OS_INTFS_FILES := os_intf/linux/os_intfs.o \
			os_intf/linux/usb92u_intf.o \

 _HAL_INTFS_FILES := hal/RTL8192U/hal_init.o \
			hal/RTL8192U/usb_ops.o \
			hal/RTL8192U/usb_ops_linux.o 
#endif

#ifeq ($(CONFIG_CFIO), y)
# $(shell cp $(PWD)/autoconf_cfio.h include/autoconf.h)
# _OS_INTFS_FILES := osdep_service.o
#_HAL_INTFS_FILES := hal_init.o cfio_ops.o cfio_halinit.o
#endif


ifneq ($(KERNELRELEASE),)

obj-$(CONFIG_REALTEK_USB_RTL8709) := 8192u.o

8192u-y += cmd/rtl871x_cmd.o 
8192u-y += crypto/rtl871x_security.o 
8192u-y += debug/rtl871x_debug.o 
8192u-y += eeprom/rtl8192u_eeprom.o 

8192u-y += $(_HAL_INTFS_FILES)

8192u-y += io/rtl871x_io.o

8192u-y += ioctl/rtl871x_ioctl_query.o \
          ioctl/rtl871x_ioctl_set.o \
	   ioctl/rtl871x_ioctl_linux.o \

8192u-y += mlme/ieee80211.o mlme/rtl871x_mlme.o \
	mlme/wlan_cmd.o  \
	mlme/wlan_mlme.o \
	mlme/wlan_sme.o  \
	mlme/wlan_task.o \
	mlme/wlan_util.o  \
	mlme/RTL8711_RFCfg.o \
	mlme/irp_misc.o\

8711-y += mp/rtl871x_mp.o mp/rtl871x_mp_ioctl.o

8192u-y += os_dep/linux/xmit_linux.o \
		os_dep/linux/mlme_linux.o \
		os_dep/linux/recv_linux.o \

8192u-y += $(_OS_INTFS_FILES)

8192u-y += pwrctrl/rtl871x_pwrctrl.o
8192u-y += recv/rtl871x_recv.o
8192u-y += rf/rtl871x_rf.o 
8192u-y += sta_mgt/rtl871x_sta_mgt.o
8192u-y += xmit/rtl871x_xmit.o




else



#KVER  := $(shell uname -r)
#KSRC := /lib/modules/$(KVER)/build
KSRC := /opt/svn-src-dvr/linux-2.6.12

PWD := $(shell pwd)
INSTALL_PREFIX :=

all: modules
modules:
	$(MAKE) -C $(KSRC) M=$(PWD)  modules
.PHONY: modules clean
clean:
	rm -f include/autoconf.h
	rm -fr *.mod.c *.mod *.o .*.cmd *.ko *~
	cd cmd ; rm -fr *.mod.c *.mod *.o .*.cmd *.ko 
	cd crypto ; rm -fr *.mod.c *.mod *.o .*.cmd *.ko 
	cd debug ; rm -fr *.mod.c *.mod *.o .*.cmd *.ko 
	cd eeprom ; rm -fr *.mod.c *.mod *.o .*.cmd *.ko 
	cd hal/RTL8711 ; rm -fr *.mod.c *.mod *.o .*.cmd *.ko 
	cd hal/RTL8187b ; rm -fr *.mod.c *.mod *.o .*.cmd *.ko 
	cd hal/RTL8192U; rm -rf *.mod.c *.mod *.o .*.cmd *.ko
	cd io ; rm -fr *.mod.c *.mod *.o .*.cmd *.ko 
	cd ioctl ; rm -fr *.mod.c *.mod *.o .*.cmd *.ko 
	cd mlme ; rm -fr *.mod.c *.mod *.o .*.cmd *.ko 
	cd mp ; rm -fr *.mod.c *.mod *.o .*.cmd *.ko 
	cd os_dep/linux ; rm -fr *.mod.c *.mod *.o .*.cmd *.ko 
	cd os_intf ; rm -fr *.mod.c *.mod *.o .*.cmd *.ko 
	cd os_intf/linux ; rm -fr *.mod.c *.mod *.o .*.cmd *.ko 
	cd pwrctrl ; rm -fr *.mod.c *.mod *.o .*.cmd *.ko 
	cd recv ; rm -fr *.mod.c *.mod *.o .*.cmd *.ko 
	cd rf ; rm -fr *.mod.c *.mod *.o .*.cmd *.ko 
	cd sta_mgt ; rm -fr *.mod.c *.mod *.o .*.cmd *.ko 
	cd xmit; rm -fr *.mod.c *.mod *.o .*.cmd *.ko 

endif

