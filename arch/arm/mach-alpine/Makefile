
KBUILD_CPPFLAGS +=	-Iarch/arm/mach-alpine/include/al_hal \
					-Iarch/arm/mach-alpine/include \
					-Iarch/arm/mach-alpine/include/mach

obj-y					+= alpine_machine.o
obj-y					+= al_fabric.o
obj-y 					+= al_hal/
obj-$(CONFIG_SMP)			+= platsmp.o alpine_cpu_pm.o
obj-$(CONFIG_ARM_CPU_IDLE_ALPINE)	+= sleep-alpine.o
