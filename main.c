
#include <stdlib.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/timer.h>
#include <libopencm3/usb/usbd.h>
#include <libopencm3/usb/cdc.h>

//	pc to mcu protocol
#define CMD_HALT			0x00
#define CMD_SELECT_DRIVE	0x01	//cmd drive
#define CMD_CHECK_DISK		0x02
#define CMD_MOTOR			0x03	//cmd on/off
#define CMD_READ 			0x04	//cmd track
#define CMD_READ_MULTI 		0x05	//cmd track times
#define CMD_HANDSHAKE		0x69

//	mcu to pc protocol
#define MSG_INVALID_CMD		0xC0	// 1100 0000
#define MSG_DONE			0xC1	// 1100 0001
#define MSG_OVERFLOW		0xC2	// 1100 0010
#define MSG_INDEX_ON		0xC3	// 1100 0011
#define MSG_INDEX_OFF		0xC4	// 1100 0100
#define MSG_NO_DISK			0xC5	// 1100 0101
#define MSG_DISK_EJECTED	0xC6	// 1100 0110

//	pin and port definitions
#define PORT_DENSEL		GPIOH
#define PIN_DENSEL		GPIO1
#define PORT_INDEX		GPIOC
#define PIN_INDEX		GPIO15
#define PORT_MOTOR1		GPIOC
#define PIN_MOTOR1		GPIO13
#define PORT_DRVSEL2	GPIOE
#define PIN_DRVSEL2		GPIO5
#define PORT_DRVSEL1	GPIOE
#define PIN_DRVSEL1		GPIO3
#define PORT_MOTOR2		GPIOE
#define PIN_MOTOR2		GPIO1
#define PORT_DIR		GPIOB
#define PIN_DIR			GPIO9
#define PORT_STEP		GPIOB
#define PIN_STEP		GPIO7
#define PORT_TRACK0		GPIOB
#define PIN_TRACK0		GPIO5
#define PORT_WRTPRO		GPIOB
#define PIN_WRTPRO		GPIO3
#define PORT_READDATA	GPIOD
#define PIN_READDATA	GPIO6
#define PORT_SIDESEL	GPIOD
#define PIN_SIDESEL		GPIO4
#define PORT_DISKCH		GPIOD
#define PIN_DISKCH		GPIO2

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
	
	char buffer_in[64];
	char buffer_out[64];
	int length = usbd_ep_read_packet(device, 0x01, buffer_in, 64);
	
	if(length)
	{
		switch(buffer_in[0])
		{
			case CMD_HANDSHAKE:
				buffer_out[0] = 'F';
				buffer_out[1] = 'L';
				buffer_out[2] = 'O';
				buffer_out[3] = 'P';
				buffer_out[4] = 'P';
				buffer_out[5] = 'Y';
				buffer_out[6] = 'T';
				buffer_out[7] = 'H';
				buffer_out[8] = 'I';
				buffer_out[9] = 'N';
				buffer_out[10] = 'G';
				length = 11;
				break;
			default:
				break;
		}
		while(usbd_ep_write_packet(device, 0x82, buffer_out, length) == 0);
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



//	buffer and support variables for outgoing data.
uint8_t out_buffer[128];
uint8_t out_position;
uint8_t	out_count;

//	track start with 0 being the outermost track on side 0, and track 1
//	being the outermost track on side 1
uint8_t current_track;

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
			usbd_ep_write_packet(usb_device, 0x82, out_buffer, 64);
		}
		else
		{
			usbd_ep_write_packet(usb_device, 0x82, out_buffer + 64, 64);
		}
		out_count -= 64;
	}
}

/*void setup_io()
{
	
}*/

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
