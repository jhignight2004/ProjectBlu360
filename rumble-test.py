import usb.core
import usb.util
import time

VID = 0x045e
PID = 0x0719  # wireless receiver

dev = usb.core.find(idVendor=VID, idProduct=PID)

if dev is None:
    raise Exception("Receiver not found")

dev.set_configuration()

cfg = dev.get_active_configuration()

# find interrupt OUT endpoint
ep_out = None
intf = None

for interface in cfg:
    for ep in interface:
        if usb.util.endpoint_direction(ep.bEndpointAddress) == usb.util.ENDPOINT_OUT:
            if usb.util.endpoint_type(ep.bmAttributes) == usb.util.ENDPOINT_TYPE_INTR:
                ep_out = ep
                intf = interface
                break

if ep_out is None:
    raise Exception("Interrupt OUT endpoint not found")

usb.util.claim_interface(dev, intf.bInterfaceNumber)

# rumble packet
data = [0x00, 0x08, 0xFF, 0xFF, 0,0,0,0]

# send rumble
dev.write(ep_out.bEndpointAddress, data)

time.sleep(2)

# stop rumble
data = [0x00, 0x08, 0x00, 0x00, 0,0,0,0]
dev.write(ep_out.bEndpointAddress, data)

print("Done")