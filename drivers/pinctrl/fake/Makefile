obj-$(CONFIG_PINCTRL_FAKE) += pinctrl-fake.o

obj-$(CONFIG_I2C_FAKE) := i2c-fake.o 
i2c-fake-y := i2c-fake-main.o
ifeq ($(CONFIG_I2C_FAKE_AT24),y)
i2c-fake-y += i2c-fake-at24.o
endif
ifeq ($(CONFIG_I2C_FAKE_MCP9808),y)
i2c-fake-y += i2c-fake-mcp9808.o
endif
ifeq ($(CONFIG_I2C_FAKE_MCP9808_WORKER),y)
i2c-fake-y += i2c-fake-mcp9808-worker.o
endif

obj-$(CONFIG_SPI_FAKE) := spi-fake.o 
spi-fake-y := spi-fake-main.o
ifeq ($(CONFIG_SPI_FAKE_AT25),y)
spi-fake-y += spi-fake-at25.o
endif
ifeq ($(CONFIG_SPI_FAKE_MAX6662),y)
spi-fake-y += spi-fake-max6662.o
endif
ifeq ($(CONFIG_SPI_FAKE_MAX6662_WORKER),y)
spi-fake-y += spi-fake-max6662-worker.o
endif

#pinctrl-fake-$(CONFIG_PINCTRL_FAKE_MMC)  += pinctrl-fake-mmc.o
#pinctrl-fake-$(CONFIG_PINCTRL_FAKE_UART) += pinctrl-fake-uart.o
#pinctrl-fake-$(CONFIG_PINCTRL_FAKE_PWM)  += pinctrl-fake-uart.o
