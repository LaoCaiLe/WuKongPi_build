src = $(wildcard *.c)
target = $(patsubst %.c,%,$(src))
obj = $(patsubst %.c,%.o,$(src))
CC=arm-linux-gnueabihf-gcc
args = -g -Wall
link = -lpthread -lrt
ALL:$(target)

%:%.c
	$(CC) $(args) $< $(link) -o $@

.PHONY: clean ALL rmtxt

clean:
	-rm -rf $(target)

print:
	@echo target=$(target)
	@echo src=$(src)
	@echo obj=$(obj)
