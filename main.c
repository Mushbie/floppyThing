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

#include <stdlib.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/timer.h>
#include <libopencm3/stm32/exti.h>
#include <libopencm3/cm3/nvic.h>
#include <libopencm3/cm3/systick.h>
#include <libopencm3/usb/usbd.h>
#include <libopencm3/usb/cdc.h>

#include "usb_consts.h"

//	pc to mcu protocol
#define CMD_HALT			0x00
#define CMD_SELECT_DRIVE	0x01	// cmd drive
#define CMD_CYLINDER		0x02	// cmd cylinder
#define CMD_HEAD			0x03	// cmd head
#define CMD_CHECK_DISK		0x04	// cmd
#define CMD_MOTOR			0x05	// cmd on/off
#define CMD_READ 			0x06	// cmd
#define CMD_READ_MULTI 		0x07	// cmd times
#define CMD_HANDSHAKE		0x69

//	mcu to pc protocol
#define MSG_INVALID_CMD		0xC0	// 1100 0000
#define MSG_DONE			0xC1	// 1100 0001
#define MSG_OVERFLOW		0xC2	// 1100 0010
#define MSG_INDEX_ON		0xC3	// 1100 0011
#define MSG_INDEX_OFF		0xC4	// 1100 0100
#define MSG_NO_DISK			0xC5	// 1100 0101
#define MSG_DISK_LOADED		0xC6
#define MSG_DISK_EJECTED	0xC7
#define MSG_INDEX_TIMEOUT	0xC8

#define EVENT_STEP_TICK		0x01
#define EVENT_STEP_TOCK		0x02
#define EVENT_STEP_DONE		0x03
#define EVENT_MOTOR_READY	0x04

#define STATE_DONE			0x00
#define STATE_STEP_TICK		0x01
#define STATE_STEP_TOCK		0x02
#define STATE_STEP_DONE		0x03
#define STATE_SPINUP		0x04
#define STATE_READ			0x05


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

// global variables go here
uint8_t control_buffer[128];
usbd_device *usb_device;
volatile uint32_t system_time = 0;
uint32_t temp_time = 0;

/*uint8_t next_event = 0;
uint8_t event_count = 0;
uint32_t event_times[16];
uint8_t events[16];*/

uint8_t state = STATE_DONE;
uint32_t state_time = 0;

//	buffer and support variables for outgoing data.
uint8_t out_buffer[128];
uint8_t out_position;
uint8_t	out_count;

//	track start with 0 being the outermost track on side 0, and track 1
//	being the outermost track on side 1
uint8_t current_cylinder = 255;
uint8_t	target_cylinder = 0;
uint8_t current_head = 0;
uint8_t current_dir = 0;	// 0 is down and 1 is up
uint8_t current_drive = 0;
uint8_t command = 0;
uint8_t parameter = 0;
uint8_t index_state = 0;
uint8_t index_count = 0;
uint8_t read_target = 0;

void sys_tick_handler(void)
{
	system_time++;
}

static inline uint32_t next_time(uint32_t delay)
{
	return system_time + delay;
}

static inline void message_add(uint8_t message)
{
	if(out_position >= 127)
	{
		out_position = 0;
	}
	else
	{
		out_position++;
	}
	out_buffer[out_position] = message;
	out_count++;
}

static inline void serial_send_byte(uint8_t byte)
{
	while(usbd_ep_write_packet(usb_device, 0x82, (char *)&byte, 1) == 0);
}

/*void event_add(uint8_t event, uint32_t delay)
{
	uint8_t pos = next_event + event_count;
	uint32_t target_time = system_time + delay;	// this might wrap around, but we want that
	
	if(pos > 15)
	{
		pos -= 16;
	}
	event_times[pos] = target_time;
	events[pos] = event;
	event_count++;
}*/

void drive(uint8_t drive)
{
	current_drive = drive;
	gpio_set(PORT_DRVSEL1, PIN_DRVSEL1);
	gpio_set(PORT_DRVSEL2, PIN_DRVSEL2);
	if(drive == 1)
	{
		gpio_clear(PORT_DRVSEL1, PIN_DRVSEL1);
	}
	else if(drive == 2)
	{
		gpio_clear(PORT_DRVSEL2, PIN_DRVSEL2);
	}
	serial_send_byte(MSG_DONE);
}

void cylinder(uint8_t cylinder)
{
	target_cylinder = cylinder;
	if(target_cylinder > 79)
		gpio_set(GPIOD, GPIO12);
	if(cylinder == 0)
	{
		if(gpio_get(PORT_TRACK0, PIN_TRACK0))
		{
			gpio_set(PORT_DIR, PIN_DIR);
			current_dir = 0;
			gpio_clear(PORT_STEP, PIN_STEP);
			//event_add(EVENT_STEP_TOCK, 60);
			state = STATE_STEP_TICK;
			state_time = next_time(60);
		}
		else
		{
			current_cylinder = 0;
		}
	}
	else
	{
		if(cylinder > current_cylinder)
		{
			gpio_clear(PORT_DIR, PIN_DIR);
			current_dir = 1;
		}
		else
		{
			gpio_set(PORT_DIR, PIN_DIR);
			current_dir = 0;
		}
		gpio_clear(PORT_STEP, PIN_STEP);
		//event_add(EVENT_STEP_TOCK, 60);
		state = STATE_STEP_TICK;
		state_time = next_time(60);
	}
}

void head(uint8_t head)
{
	if(head)
	{
		gpio_clear(PORT_SIDESEL, PIN_SIDESEL);
	}
	else
	{
		gpio_set(PORT_SIDESEL, PIN_SIDESEL);
	}
	serial_send_byte(MSG_DONE);
}

void check_disk()
{
	if(gpio_get(PORT_DISKCH, PIN_DISKCH) == 0)
	{
		serial_send_byte(MSG_DISK_LOADED);
	}
	else
	{
		serial_send_byte(MSG_NO_DISK);
	}
}

void motor(uint8_t motor_state)
{
	if(current_drive == 1)
	{
		if(motor_state)
		{
			gpio_clear(PORT_MOTOR1, PIN_MOTOR1);
			//event_add(EVENT_MOTOR_READY, 10000);
			state = STATE_SPINUP;
			state_time = next_time(10000);
		}
		else
		{
			gpio_set(PORT_MOTOR1, PIN_MOTOR1);
		}
	}
	if(current_drive == 2)
	{
		if(motor_state)
		{
			gpio_clear(PORT_MOTOR2, PIN_MOTOR2);
			//event_add(EVENT_MOTOR_READY, 10000);
			state = STATE_SPINUP;
			state_time = next_time(10000);
		}
		else
		{
			gpio_set(PORT_MOTOR2, PIN_MOTOR2);
		}
	}
}

void read(uint8_t count)
{
	state = STATE_READ;
	index_count = 0;
	index_state = 0;
	read_target = count;
	state_time = next_time(10000);	// 1s timeout on finding the index
	// TODO: enable interrupts on index pin
	exti_set_trigger(EXTI15, EXTI_TRIGGER_FALLING);
	exti_enable_request(EXTI15);
}

/*void event_poll()
{
	uint32_t time = system_time;
	
	if(event_count > 0)
	{
		if((time >= event_times[next_event]) && ((time - event_times[next_event]) < 1000))
		{
			switch(events[next_event])
			{
				case EVENT_STEP_TICK:
					gpio_clear(PORT_STEP, PIN_STEP);
					event_add(EVENT_STEP_TOCK, 60);
					break;
				case EVENT_STEP_TOCK:
					//gpio_set(GPIOD, GPIO12);
					gpio_set(PORT_STEP, PIN_STEP);
					if(target_cylinder == 0)
					{
						if(gpio_get(PORT_TRACK0, PIN_TRACK0) == 0)
						{
							current_cylinder = 0;
							event_add(EVENT_STEP_DONE, 200);
							break;
						}
					}
					if(current_dir)
					{
						current_cylinder++;
					}
					else
					{
						current_cylinder--;
					}
					if(current_cylinder == target_cylinder)
					{
						event_add(EVENT_STEP_DONE, 200);
					}
					else
					{
						event_add(EVENT_STEP_TICK, 60);
					}
					break;
				case EVENT_STEP_DONE:
					gpio_set(PORT_DIR, PIN_DIR);
					// send a done message to the host
					//while(usbd_ep_write_packet(usb_device, 0x82, (const char*)MSG_DONE, 1) == 0);
					break;
			}
			next_event++;
			if(next_event > 15)
			{
				next_event = 0;
			}
			event_count--;
		}
	}
}*/

void state_poll()
{
	uint32_t time = system_time;
	if(state != STATE_DONE)
	{
		if((time >= state_time) && ((time - state_time) < 1000))
		{
			switch(state)
			{
				case STATE_STEP_TICK:
					gpio_clear(PORT_STEP, PIN_STEP);
					state = STATE_STEP_TOCK;
					state_time = next_time(60);
					break;
				case STATE_STEP_TOCK:
					gpio_set(PORT_STEP, PIN_STEP);
					if(target_cylinder == 0)
					{
						if(gpio_get(PORT_TRACK0, PIN_TRACK0) == 0)
						{
							current_cylinder = 0;
							state = STATE_STEP_DONE;
							state_time = next_time(200);
							break;
						}
					}
					if(current_dir)
					{
						current_cylinder++;
					}
					else
					{
						current_cylinder--;
					}
					if(current_cylinder == target_cylinder)
					{
						state = STATE_STEP_DONE;
						state_time = next_time(200);
					}
					else
					{
						state = STATE_STEP_TICK;
						state_time = next_time(60);
					}
					break;
				case STATE_STEP_DONE:
					gpio_set(PORT_DIR, PIN_DIR);
					state = STATE_DONE;
					// Send a done message to the host
					serial_send_byte(MSG_DONE);
					break;
				case STATE_SPINUP:
					state = STATE_DONE;
					serial_send_byte(MSG_DONE);
					break;
			}
		}			
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
			case CMD_SELECT_DRIVE:
				drive(buffer_in[1]);
				break;
			case CMD_CYLINDER:
				cylinder(buffer_in[1]);
				break;
			case CMD_HEAD:
				head(buffer_in[1]);
				break;
			case CMD_CHECK_DISK:
				check_disk();
				break;
			case CMD_MOTOR:
				motor(buffer_in[1]);
				break;
			case CMD_READ:
				read(1);
				break;
			case CMD_READ_MULTI:
				read(buffer_in[1]);
				break;
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
				while(usbd_ep_write_packet(device, 0x82, buffer_out, length) == 0);
				break;
			default:
				break;
		}
	}
	
}

enum usbd_request_return_codes cdcacm_request_handler(usbd_device *device,
	struct usb_setup_data *request, uint8_t **buffer, uint16_t *length,
	void(**complete)(usbd_device *device, struct usb_setup_data *request))
{
	// tell the compiler these valriable are not used
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

void tim6_isr(void)	// Timer overflow handler
{
	message_add(MSG_OVERFLOW);
}

void exti15_10_isr(void)	// Index handler
{
	exti_reset_request(EXTI15);
	gpio_set(GPIOD, GPIO12);
	if(state == STATE_READ)
	{
		if(index_state == 0)
		{
			index_state = 1;
			index_count++;
			message_add(MSG_INDEX_ON);
			// TODO: reset timer
			if(index_count == 0)
			{
				// TODO: change index interrupt trigger mode
				exti_set_trigger(EXTI15, EXTI_TRIGGER_BOTH);
			}
		}
		else
		{
			index_state = 0;
			message_add(MSG_INDEX_OFF);
			// TODO: reset timer
			if(index_count > read_target)
			{
				state = STATE_DONE;
				// TODO: disable timer
				// TODO: disable index pin intterupt
				exti_disable_request(EXTI15);
				message_add(MSG_DONE);
			}
		}
	}
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

void setup_io()
{
	// start the gpio clocks
	rcc_periph_clock_enable(RCC_GPIOB);
	rcc_periph_clock_enable(RCC_GPIOC);
	rcc_periph_clock_enable(RCC_GPIOD);
	rcc_periph_clock_enable(RCC_GPIOE);
	rcc_periph_clock_enable(RCC_GPIOH);
	
	// setup the outputs
	gpio_clear(PORT_DENSEL, PIN_DENSEL);	// select HD as default
	gpio_mode_setup(PORT_DENSEL, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, PIN_DENSEL);
	gpio_set_output_options(PORT_DENSEL, GPIO_OTYPE_OD, GPIO_OSPEED_2MHZ, PIN_DENSEL);
	gpio_set(PORT_MOTOR1, PIN_MOTOR1);
	gpio_mode_setup(PORT_MOTOR1, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, PIN_MOTOR1);
	gpio_set_output_options(PORT_MOTOR1, GPIO_OTYPE_OD, GPIO_OSPEED_2MHZ, PIN_MOTOR1);
	gpio_set(PORT_MOTOR2, PIN_MOTOR2);
	gpio_mode_setup(PORT_MOTOR2, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, PIN_MOTOR2);
	gpio_set_output_options(PORT_MOTOR2, GPIO_OTYPE_OD, GPIO_OSPEED_2MHZ, PIN_MOTOR2);
	gpio_set(PORT_DRVSEL1, PIN_DRVSEL1);
	gpio_mode_setup(PORT_DRVSEL1, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, PIN_DRVSEL1);
	gpio_set_output_options(PORT_DRVSEL1, GPIO_OTYPE_OD, GPIO_OSPEED_2MHZ, PIN_DRVSEL1);
	gpio_set(PORT_DRVSEL2, PIN_DRVSEL2);
	gpio_mode_setup(PORT_DRVSEL2, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, PIN_DRVSEL2);
	gpio_set_output_options(PORT_DRVSEL2, GPIO_OTYPE_OD, GPIO_OSPEED_2MHZ, PIN_DRVSEL2);
	gpio_set(PORT_DIR, PIN_DIR);
	gpio_mode_setup(PORT_DIR, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, PIN_DIR);
	gpio_set_output_options(PORT_DIR, GPIO_OTYPE_OD, GPIO_OSPEED_2MHZ, PIN_DIR);
	gpio_set(PORT_STEP, PIN_STEP);
	gpio_mode_setup(PORT_STEP, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, PIN_STEP);
	gpio_set_output_options(PORT_STEP, GPIO_OTYPE_OD, GPIO_OSPEED_2MHZ, PIN_STEP);
	gpio_set(PORT_SIDESEL, PIN_SIDESEL);
	gpio_mode_setup(PORT_SIDESEL, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, PIN_SIDESEL);
	gpio_set_output_options(PORT_SIDESEL, GPIO_OTYPE_OD, GPIO_OSPEED_2MHZ, PIN_SIDESEL);
	
	// setup the inputs
	gpio_mode_setup(PORT_INDEX, GPIO_MODE_INPUT, GPIO_PUPD_PULLUP , PIN_INDEX);
	gpio_mode_setup(PORT_TRACK0, GPIO_MODE_INPUT, GPIO_PUPD_PULLUP, PIN_TRACK0);
	gpio_mode_setup(PORT_WRTPRO, GPIO_MODE_INPUT, GPIO_PUPD_PULLUP , PIN_WRTPRO);
	gpio_mode_setup(PORT_READDATA, GPIO_MODE_INPUT, GPIO_PUPD_PULLUP , PIN_READDATA);
	gpio_mode_setup(PORT_DISKCH, GPIO_MODE_INPUT, GPIO_PUPD_PULLUP , PIN_DISKCH);
}

int main(void)
{
	rcc_clock_setup_pll(&rcc_hse_8mhz_3v3[RCC_CLOCK_3V3_168MHZ]);
	rcc_periph_clock_enable(RCC_GPIOA);
	rcc_periph_clock_enable(RCC_OTGFS);
	rcc_periph_clock_enable(RCC_SYSCFG);
	gpio_mode_setup(GPIOA, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO11 | GPIO12);
	gpio_set_af(GPIOA, GPIO_AF10, GPIO11 | GPIO12);
	
	nvic_enable_irq(NVIC_EXTI15_10_IRQ);
	exti_select_source(EXTI15, PORT_INDEX);
	
	usb_device = usbd_init(&otgfs_usb_driver, &device_desc, &configuration_desc,
		usb_strings, 3, control_buffer, sizeof(control_buffer));
		
	usbd_register_set_config_callback(usb_device, cdcacm_set_config);
	
	setup_io();
	
	// setup systick
	systick_set_reload(16800);	// 0.1mS interval
	systick_set_clocksource(STK_CSR_CLKSOURCE_AHB);
	systick_counter_enable();
	systick_interrupt_enable();

	gpio_mode_setup(GPIOD, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO12);
	
	//drive(1);
	//motor(1);
	temp_time = 15000 + system_time;
	while(system_time < temp_time)
	{
	}
	/*read(1);
	while(1)
	{}
	if(gpio_get(PORT_TRACK0, PIN_TRACK0))
	{
		if(gpio_get(PORT_DISKCH, PIN_DISKCH))
		{
			gpio_set(GPIOD, GPIO12);
		}
		cylinder(0);
	}
	else
	{
		current_cylinder = 0;
		cylinder(79);
	}
	
	while(1)
	{
		state_poll();
	}*/

	while(1)
	{
		out_buffer_poll();
		usbd_poll(usb_device);
		state_poll();
	}
}
