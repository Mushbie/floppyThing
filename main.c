
#include <stdlib.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/usb/usbd.h>
#include <libopencm3/usb/cdc.h>

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

enum usbd_request_return_codes cdcacm_request_handler(usbd_device *device,
	struct usb_setup_data *request, uint8_t **buffer, uint16_t *length,
	void(**complete)(usbd_device *device, struct usb_setup_data *request))
{
	// tell the compiler these valriable not used
	(void)device;
	(void)buffer;
	(void)complete;
	
	switch(request->bRequest)
	{
		case USB_CDC_REQ_SET_CONTROL_LINE_STATE:
			return USBD_REQ_HANDLED;
		case USB_CDC_REQ_SET_LINE_CODING:
			if(*length < sizeof(struct usb_cdc_line_coding))
			{
				return USBD_REQ_NOTSUPP;
			}
			return USBD_REQ_HANDLED;
		default:
			return USBD_REQ_NOTSUPP;
	}
}

void data_rx_handler(usbd_device *device, uint8_t endpoint)
{
	(void)endpoint;	// tell the computer this is not used
	
	char buffer[64];
	int length = usbd_ep_read_packet(device, 0x01, buffer, 64);
	
	if(length)
	{
		buffer[0] +=1;
		while(usbd_ep_write_packet(device, 0x82, buffer, length) == 0);
	}
	
}

void cdcacm_set_config(usbd_device *device, uint16_t wValue)
{
	(void)wValue;	// tell the computer this is not used
	
	usbd_ep_setup(device, 0x01, USB_ENDPOINT_ATTR_BULK, 64, data_rx_handler);	// data rx endpoint
	usbd_ep_setup(device, 0x82, USB_ENDPOINT_ATTR_BULK, 64, NULL);				// data tx endpoint
	usbd_ep_setup(device, 0x83, USB_ENDPOINT_ATTR_INTERRUPT, 16, NULL);			// notification endpoint
	
	usbd_register_control_callback(device, USB_REQ_TYPE_CLASS | USB_REQ_TYPE_INTERFACE,
		USB_REQ_TYPE_TYPE | USB_REQ_TYPE_RECIPIENT, cdcacm_request_handler);
}

void setup_timer()
{
	rcc_periph_clock_enable(RCC_TIM6);
	TIM6_PSC |= 4;	// 21MHz at sysclk 168MHz
	TIM6_DIER |= 1;
	TIM6_ARR = 0x40ff;	// If I have a timer that can count to 191 I need to start at 64(0x40)
	
}

uint8_t control_buffer[128];
usbd_device *usb_device;

/*	pc to mcu protocol protoype
CMD_SELECT_DRIVE drive
CMD_CHECK_DISK
CMD_MOTOR on/off
CMD_READ head track
CMD_READ_MULTI head track times
*/
#define MSG_DONE			0xC0	// 1100 0000
#define MSG_OVERFLOW		0xC1	// 1100 0001
#define MSG_INDEX			0xC2	// 1100 0010

//	buffer and support variables for outgoing data.
uint8_t out_buffer[128];
uint8_t out_position;
uint8_t	out_count;

void tim6_isr(void)	// Timer overflow handler
{
	if(out_position >= 127)
	{
		out_position = 0;
	}
	else
	{
		out_position++;
	}
	out_buffer[out_position] = MSG_OVERFLOW;
	out_count++;
}

void out_buffer_poll()
{
	if(out_count > 64)
	{
		if(out_position < 64)
		{
			usbd_ep_write_packet(device, 0x82, out_buffer, 64);
		}
		else
		{
			usbd_ep_write_packet(device, 0x82, out_buffer + 64, 64);
		}
		out_count -= 64;
	}
}

int main(void)
{
	rcc_clock_setup_pll(&rcc_hse_8mhz_3v3[RCC_CLOCK_3V3_168MHZ]);
	rcc_periph_clock_enable(RCC_GPIOA);
	rcc_periph_clock_enable(RCC_OTGFS);
	gpio_mode_setup(GPIOA, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO11 | GPIO12);
	gpio_set_af(GPIOA, GPIO_AF10, GPIO11 | GPIO12);
	
	usb_device = usbd_init(&otgfs_usb_driver, &device_desc, &configuration_desc,
		usb_strings, 3, control_buffer, sizeof(control_buffer));
		
	usbd_register_set_config_callback(usb_device, cdcacm_set_config);

	while(1)
	{
		out_buffer_poll();
		usbd_poll(usb_device);
	}
}
