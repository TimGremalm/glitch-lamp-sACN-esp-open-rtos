# make
# make flash
# make test
# make test ESPPORT=/dev/ttyUSB0
PROGRAM=glitch-lamp
EXTRA_COMPONENTS = extras/multipwm
include $(ESPOPENRTOS)/common.mk
