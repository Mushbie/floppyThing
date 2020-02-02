/*
	This file is part of floppyThing, a floppy imaging tool.
	Copyright 2020 Mads Thore Theodor Hansen

    floppyThing is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    floppyThing is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with floppyThing.  If not, see <https://www.gnu.org/licenses/>.
*/

const struct usb_device_descriptor device_desc = {
	.bLength = USB_DT_DEVICE_SIZE,
	.bDescriptorType = USB_DT_DEVICE,
	.bcdUSB = 0x0200,		// usb version number
	.bDeviceClass = USB_CLASS_CDC,
	.bDeviceSubClass = 0,
	.bDeviceProtocol = 0,
	.bMaxPacketSize0 = 64,
	.idVendor = 0x0483,		// STM Vendor ID used while testing
	.idProduct = 0x5740,	// STM virtuel serial ID
	.bcdDevice = 0x0200,	// device version number
	.iManufacturer = 1,		// manufacturer string index position
	.iProduct = 2,			// product string index position
	.iSerialNumber = 3,		// serial number string index position
	.bNumConfigurations = 1,// number of configurations.
};

const char *usb_strings[] = {
	"Mushbie Technology",
	"floppyThing",
	"DEMO",
};


/* Libopencm3 example code claims this endpoint is optional,
in the cdc acm standard. As far a I can see it is not, and
apparently the linux driver crashes without it. */
const struct usb_endpoint_descriptor notification_endpoint = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = 0x83,
	.bmAttributes = USB_ENDPOINT_ATTR_INTERRUPT,
	.wMaxPacketSize = 16,
	.bInterval = 255,
};

const struct usb_endpoint_descriptor data_endpoints[] = {
	{
		.bLength = USB_DT_ENDPOINT_SIZE,
		.bDescriptorType = USB_DT_ENDPOINT,
		.bEndpointAddress = 0x01,
		.bmAttributes = USB_ENDPOINT_ATTR_BULK,
		.wMaxPacketSize = 64,
		.bInterval = 1,
	},
	{
		.bLength = USB_DT_ENDPOINT_SIZE,
		.bDescriptorType = USB_DT_ENDPOINT,
		.bEndpointAddress = 0x82,
		.bmAttributes = USB_ENDPOINT_ATTR_BULK,
		.wMaxPacketSize = 64,
		.bInterval = 1,
	}
};

const struct {
	struct usb_cdc_header_descriptor header;
	struct usb_cdc_acm_descriptor acm;
	struct usb_cdc_call_management_descriptor call_mngmnt;
	struct usb_cdc_union_descriptor cdc_union;
} __attribute__((packed)) cdc_functional_descriptors =
{
	.header = {
		.bFunctionLength = sizeof(struct usb_cdc_header_descriptor),
		.bDescriptorType = CS_INTERFACE,
		.bDescriptorSubtype = USB_CDC_TYPE_HEADER,
		.bcdCDC = 0x0110,	// version of the cdc standard
	},
	.acm = {
		.bFunctionLength = sizeof(struct usb_cdc_acm_descriptor),
		.bDescriptorType = CS_INTERFACE,
		.bDescriptorSubtype = USB_CDC_TYPE_ACM,
		.bmCapabilities = 0,
	},
	.call_mngmnt = {
		.bFunctionLength = sizeof(struct usb_cdc_call_management_descriptor),
		.bDescriptorType = CS_INTERFACE,
		.bDescriptorSubtype = USB_CDC_TYPE_CALL_MANAGEMENT,
		.bmCapabilities = 0,
		.bDataInterface = 1,
	},
	.cdc_union = {
		.bFunctionLength = sizeof(struct usb_cdc_union_descriptor),
		.bDescriptorType = CS_INTERFACE,
		.bDescriptorSubtype = USB_CDC_TYPE_UNION,
		.bControlInterface = 0,
		.bSubordinateInterface0 = 1,
	}
};

const struct usb_interface_descriptor com_interface = {
	.bLength = USB_DT_INTERFACE_SIZE,
	.bDescriptorType = USB_DT_INTERFACE,
	.bInterfaceNumber = 0,
	.bAlternateSetting = 0,
	.bNumEndpoints = 1,
	.bInterfaceClass = USB_CLASS_CDC,
	.bInterfaceSubClass = USB_CDC_SUBCLASS_ACM,
	.bInterfaceProtocol = USB_CDC_PROTOCOL_AT,
	.iInterface = 0,
	.endpoint = &notification_endpoint,
	.extra = &cdc_functional_descriptors,
	.extralen = sizeof(cdc_functional_descriptors)
};

const struct usb_interface_descriptor data_interface = {
	.bLength = USB_DT_INTERFACE_SIZE,
	.bDescriptorType = USB_DT_INTERFACE,
	.bInterfaceNumber = 1,
	.bAlternateSetting = 0,
	.bNumEndpoints = 2,
	.bInterfaceClass = USB_CLASS_DATA,
	.bInterfaceSubClass = 0,
	.bInterfaceProtocol = 0,
	.iInterface = 0,
	.endpoint = data_endpoints
};

const struct usb_interface interfaces[] = {
	{
		.num_altsetting = 1,
		.altsetting = &com_interface,
	},
	{
		.num_altsetting = 1,
		.altsetting = &data_interface,
	}
};

const struct usb_config_descriptor configuration_desc = {
	.bLength = USB_DT_CONFIGURATION_SIZE,
	.bDescriptorType = USB_DT_CONFIGURATION,
	.wTotalLength = 0,
	.bNumInterfaces = 2,
	.bConfigurationValue = 1,
	.iConfiguration = 0,
	.bmAttributes = 0x80, // bus powered
	.bMaxPower = 0x32,
	.interface = interfaces,
};