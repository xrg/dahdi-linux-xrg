# Install firmwares and initialization scripts for the Astribank
# drivers

FIRMWARES	= USB_FW.hex FPGA_FXS.hex FPGA_1141.hex FPGA_1151.hex
PROTO_VER	= 30
SCRIPTS_BASE	= $(patsubst %,init_card_%_$(PROTO_VER),1 2 3 4)
SCRIPTS		= $(SCRIPTS_BASE:%=../%)
TARGET		= $(DESTDIR)/usr/share/dahdi

all:

clean:

install:
	mkdir -p $(TARGET)
	install $(SCRIPTS) $(TARGET)/
	install -m 644 ../XppConfig.pm $(FIRMWARES) $(TARGET)/

