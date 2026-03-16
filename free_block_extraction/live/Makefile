# Makefile for ext4 free block extractor module and application
# how to build: make -> insmod ext4_freeblock_module.ko

# 모듈 이름
MODULE_NAME := ext4_freeblock_module

# 커널 빌드 디렉토리
KDIR := /lib/modules/$(shell uname -r)/build
KERNEL_SRC := /usr/src/linux-source-6.8.0/linux-source-6.8.0
PWD := $(shell pwd)

# 모듈 빌드 대상 정의
obj-m := $(MODULE_NAME).o

# 사용자 프로그램 이름
USER_PROGRAM := freeblock_user

# 전체 빌드
all: $(MODULE_NAME).ko $(USER_PROGRAM)

# 커널 모듈 빌드
$(MODULE_NAME).ko:
	$(MAKE) -C $(KDIR) M=$(PWD) \
		EXTRA_CFLAGS="-I$(KERNEL_SRC)/fs/ext4" modules

# 사용자 프로그램 빌드
$(USER_PROGRAM): $(USER_PROGRAM).c
	gcc -Wall -o $@ $<

# 빌드 파일 정리
clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	rm -f $(USER_PROGRAM)
