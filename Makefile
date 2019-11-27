CC		= arm-none-eabi-gcc
LINK	= arm-none-eabi-gcc
OBJCOPY	= arm-none-eabi-objcopy

TARGET = floppy
INC = libopencm3/include
LIB_DIR = libopencm3/lib
LIB_FILE = $(LIB_DIR)/libopencm3_stm32f4.a
LD_SCRIPT = $(LIB_DIR)/stm32/f4/stm32f405x6.ld
OBJS = main.o
DEPS = 

ARCH_FLAGS	= -mthumb -mcpu=cortex-m4 -ffunction-sections -fdata-sections -mfloat-abi=hard -mfpu=fpv4-sp-d16
CFLAGS		= -Os -g -Wextra -Wshadow -Wimplicit-function-declaration -Wredundant-decls -Wmissing-prototypes -Wstrict-prototypes -fno-common $(ARCH_FLAGS) -MD -Wall -Wundef -I$(INC) -DSTM32F4
LDFLAGS		= --static -nostartfiles -L$(LIB_DIR) -T$(LD_SCRIPT) $(ARCH_FLAGS) -Wl,--gc-sections -lopencm3_stm32f4 -Wl,--start-group -lc -lgcc -lnosys -Wl,--end-group

$(TARGET).hex: $(TARGET).elf
	$(OBJCOPY) -O ihex $(TARGET).elf $(TARGET).hex

$(TARGET).elf: $(OBJS) $(LIB_FILE)
	$(LINK) -o $@ $(OBJS) $(LDFLAGS) 

%.o: %.c $(OBJS)
	$(CC) -c $< -o $@ $(CFLAGS)
	
$(LIB_FILE):
	make -C libopencm3

clean:
	rm -f *.o *.d $(TARGET).elf $(TARGET).hex