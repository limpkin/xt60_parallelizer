/*
 * XT60_balancer.c
 *
 * Created: 19/03/2024 20:33:13
 * Author : limpkin
 * Notes: what follows is a testament to magic numbers and bad coding practices
 */ 
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <string.h>
#include <avr/io.h>
#include "ascii_lib.h"
#include "defines.h"
/* Defines */
#define RAW_ADC_VAL_BUFF_LGTH 32
/* Variables */
volatile buzzer_buzz_type_t buzzing = BUZZ_OFF; 
volatile uint16_t raw_adc_latest_values_buffer[5][RAW_ADC_VAL_BUFF_LGTH];
volatile uint16_t raw_adc_buffer_fill_index = 0;
volatile uint8_t conseq_bat_disc_detect[4];
volatile uint8_t raw_adc_buffer_filled = 0;
volatile uint8_t conseq_non_zero_readings[4];
volatile uint8_t interrupt_counter = 0;
volatile uint8_t bat_disc_detected = 0;
volatile uint8_t battery_detected[4];
volatile uint8_t current_readings[4];
volatile uint16_t center_readings[4];
volatile uint16_t voltage_reading;
volatile uint8_t adc_sm = 0;

/* Interrupt called every 10ms */
ISR(TCA0_OVF_vect)
{	
	/* Counter increment */
	interrupt_counter++;
	
	/* Buzzer blinking */
	if (buzzing == BUZZ_ON_OFF)
	{
		if ((interrupt_counter & 0x3F) == 0x00)
		{
			if ((PORTA.OUT & (1 << 4)) == 0)
				BUZZER_ON();
			else
				BUZZER_OFF();
		} 
	}
	
	/* Clear interrupt */
	TCA0.SINGLE.INTFLAGS = (1 << 0);
}

/* ADC end of conversion interrupt */
ISR(ADC0_RESRDY_vect)
{
	/* Clear interrupt */
	ADC0.INTFLAGS = 0x01;

	/* Get 64 vals accumulated ADC measurement */
	uint16_t current_reading = ADC0.RES;
	current_reading /= 64;

	/* Add it to our buffer, compute average */
	raw_adc_latest_values_buffer[adc_sm][raw_adc_buffer_fill_index] = current_reading;
	current_reading = 0;
	for (uint16_t i = 0; i < RAW_ADC_VAL_BUFF_LGTH; i++)
	{
		current_reading += raw_adc_latest_values_buffer[adc_sm][i];
	}
	current_reading /= RAW_ADC_VAL_BUFF_LGTH;

	/* Buffer filled? */
	if (raw_adc_buffer_filled != 0)
	{
		/* Current reading? */
		if (adc_sm < 4)
		{
			/* Empty center point reading? */
			if (center_readings[adc_sm] == 0)
				center_readings[adc_sm] = current_reading;

			/* Correct sign */
			if (current_reading > center_readings[adc_sm])
				current_reading -= center_readings[adc_sm];
			else
				current_reading = center_readings[adc_sm] - current_reading;

			/* 1LSB = 5000 / 1024 * 5 (5A/V) = 24mA */
			
			/* Battery presence detection check */
			if (current_reading > 5)
			{
				if (conseq_non_zero_readings[adc_sm] < 5)
					conseq_non_zero_readings[adc_sm]++;
				else
					battery_detected[adc_sm] = 1;
			}
			else
			{
				conseq_non_zero_readings[adc_sm] = 0;
			}

			/* Battery disconnection: once per conversion cycle */
			if (adc_sm == 0)
			{
				/* Check for battery disconnection: one battery having current while the other being 0... */
				uint8_t enough_cur_for_detect = 0;
				for (uint8_t i = 0; i < 4; i++)
				{
					/* Check for more than 0.5A on one of the batteries */
					if (battery_detected[i] && (current_readings[i] >= 5))
					{
						enough_cur_for_detect = 1;
					}
				}
				if (enough_cur_for_detect)
				{
					for (uint8_t i = 0; i < 4; i++)
					{
						if (battery_detected[i])
						{
                            if (current_readings[i] == 0)
                            {
                                conseq_bat_disc_detect[i]++;

                                /* Enough consecutive detections? */
                                if (conseq_bat_disc_detect[i] > 20)
                                {
                                    /* Disconnection! */
                                    bat_disc_detected = 1 + i;
                                    buzzing = BUZZ_ON;
                                    BUZZER_ON();
                                    
                                    /* Disconnect charge and discharge */
                                    if ((PORTC.IN & 0x08) == 0)
                                        ISOL1_SET();
                                    else
                                        ISOL1_CLR();
                                    if ((PORTC.IN & 0x04) == 0)
                                        ISOL2_SET();
                                    else
                                        ISOL2_CLR();
                                }
                            } 
                            else
                            {
                                conseq_bat_disc_detect[i] = 0;
                            }						
						}
					}
				}
                else
                {
                    for (uint8_t i = 0; i < 4; i++)
                    {
                        conseq_bat_disc_detect[i] = 0;
                    }
                }
			}

			/* Convert into A*10 reading */
			uint32_t maths = current_reading;
			maths = maths * 50 * 5 / 1024;
			current_readings[adc_sm] = (uint8_t)maths;
		}
		else
		{
			uint32_t temp_uint = current_reading;
			temp_uint = temp_uint * 11 * 5000 / 1024;
			voltage_reading = (uint16_t)temp_uint;
		}
	}
	
	/* State machine increase */
	if (adc_sm++ == 4)
	{
		/* In theory, a full round of 4 current + 1 voltage conversion should take 1 / 150Hz */
		adc_sm = 0;

		/* Buffer fill index increase */
		if (raw_adc_buffer_fill_index++ == (RAW_ADC_VAL_BUFF_LGTH - 1))
		{
			raw_adc_buffer_fill_index = 0;
			raw_adc_buffer_filled = 1;
		}
	}
	
	/* Trigger ADC on correct input channel */
	if (adc_sm == 0)
		ADC0.MUXPOS = 11;
	else if (adc_sm == 1)
		ADC0.MUXPOS = 5;
	else if (adc_sm == 2)
		ADC0.MUXPOS = 6;
	else if (adc_sm == 3)
		ADC0.MUXPOS = 7;
	else if (adc_sm == 4)
		ADC0.MUXPOS = 10;
		
	/* Start conversion */
	ADC_ST_CONV();
}

void issue_isolated_output(uint8_t output_id, uint8_t output_state)
{
	if (output_id == 0)
	{
		if (output_state == FALSE)
			ISOL1_CLR();
		else
			ISOL1_SET();
	} 
	else
	{
		if (output_state == FALSE)
			ISOL2_CLR();
		else
			ISOL2_SET();
	}
}

void send_data_to_disp_ctrl(uint8_t reg_addr, uint8_t data)
{
	/* Everyday I'm bit banging... */
	SPI_NCS_ASST();asm("nop");
	for (uint8_t i = 7; i != 0xFF; i--)
	{
		SPI_CLK_CLR();
		if ((reg_addr & (1 << i)) == 0)
			SPI_DIO_CLR();
		else
			SPI_DIO_SET();
		asm("nop");SPI_CLK_SET();asm("nop");		
	}
	SPI_CLK_CLR();
	for (uint8_t i = 7; i != 0xFF; i--)
	{
		SPI_CLK_CLR();
		if ((data & (1 << i)) == 0)
			SPI_DIO_CLR();
		else
			SPI_DIO_SET();
		asm("nop");SPI_CLK_SET();asm("nop");	
	}
	SPI_CLK_CLR();asm("nop");
	SPI_NCS_CLR();asm("nop");
}

void send_disp_data(uint8_t* data)
{
	uint8_t reg_addr = 0x04;
	/* Everyday I'm bit banging... */
	SPI_NCS_ASST();asm("nop");
	for (uint8_t i = 7; i != 0xFF; i--)
	{
		SPI_CLK_CLR();
		if ((reg_addr & (1 << i)) == 0)
			SPI_DIO_CLR();
		else
			SPI_DIO_SET();
		asm("nop");SPI_CLK_SET();asm("nop");
	}
	SPI_CLK_CLR();
	for (uint8_t j = 0; j < 12; j++)
	{
		for (uint8_t i = 7; i != 0xFF; i--)
		{
			SPI_CLK_CLR();
			if ((data[j] & (1 << i)) == 0)
				SPI_DIO_CLR();
			else
				SPI_DIO_SET();
			asm("nop");SPI_CLK_SET();asm("nop");
		}
	}
	SPI_CLK_CLR();asm("nop");
	SPI_NCS_CLR();asm("nop");	
}

void setup_hw(void)
{
	/* Slow down clock to 2.5MHz */
	CCP = 0xD8;
	CLKCTRL.MCLKCTRLB = (0x2 << 1) | (1 << 0);
	
	/* SPI outputs */
	PORTA.DIRSET = (1 << 1) | (1 << 2) | (1 << 3);
	SPI_NCS_CLR();SPI_CLK_CLR();
	
	/* Setup buzzer IO */ 	
	PORTA.DIRSET = (1 << 4);
	
	/* Isolated outputs */
	PORTB.DIRSET = (1 << 5) | (1 << 4);
	
	/* Input pull-ups */
	PORTC.PIN0CTRL = (1 << 3);
	PORTC.PIN1CTRL = (1 << 3);
	PORTC.PIN2CTRL = (1 << 3);
	PORTC.PIN3CTRL = (1 << 3);	
		
	/* Display init */
	send_data_to_disp_ctrl(0x00, 0b00101100);		// Reset controller
	send_data_to_disp_ctrl(0x00, 0x00);				// Reset controller
	send_data_to_disp_ctrl(0x02, 0x01);				// Configure the controller: 1/4 duty, 1/3 bias

	/* TCA setup to generate 10ms interrupt */
	TCA0.SINGLE.PER = (25000-1);	// 2.5MHz / 25k = 10ms period
	TCA0.SINGLE.INTCTRL = (1 << 0);	// Enable OVF interrupt
	TCA0.SINGLE.CTRLA = (1 << 0);	// Enable counter
	
	/* ADC pins configuration: disable input buffers */
	PORTC.PIN5CTRL = 0x04;
	PORTC.PIN6CTRL = 0x04;
	PORTC.PIN7CTRL = 0x04;
	PORTB.PIN0CTRL = 0x04;
	PORTB.PIN1CTRL = 0x04;
	
	/* ADC configuration */
	ADC0.CTRLA = 0;							// 10 bits resolution
	ADC0.CTRLB = 0x06;						// 64 results accumulated
	ADC0.CTRLC = (1 << 6) | (1 << 4) | 0x1;	// reduced sampling cap, VDD reference, CLK_PER/4 (so 625k, max is 1M5)
	ADC0.MUXPOS = 11;						// Start with first imon
	ADC0.CTRLA |= 0x01;						// enable ADC	
	ADC0.INTCTRL = 0x01;					// enable result ready interrupt
	
	/* Enable interrupts */
	sei();
}

void display_text(char* text, uint8_t* dot_array)
{
	uint8_t disp_chars[8] = {0,0,0,0,0,0,0,0};
	uint8_t com_reg_contents[4][3];

	/* memclear */
	memset(com_reg_contents, 0x00, sizeof(com_reg_contents));

	/* Convert test to abcdef... */
	for (uint16_t i = 0; i < 8; i++)
	{
		if (text[i] != 0)
		{
			disp_chars[i] = pgm_read_byte(&SevenSegmentASCII[text[i] - ' ']);
		}
	}

	/* Convert to abcdef to com register contents: code for readibility and hope for compiler optimization */
	
	/* First 4 digits */
	for (uint8_t i = 0; i < 4; i++)
	{
		com_reg_contents[1][0] |= (((disp_chars[i] >> 5) & 0x01) << (i*2+0));
		com_reg_contents[1][0] |= (((disp_chars[i] >> 0) & 0x01) << (i*2+1));
		com_reg_contents[0][0] |= (((disp_chars[i] >> 6) & 0x01) << (i*2+0));
		com_reg_contents[0][0] |= (((disp_chars[i] >> 1) & 0x01) << (i*2+1));
		com_reg_contents[3][0] |= (((disp_chars[i] >> 4) & 0x01) << (i*2+0));
		com_reg_contents[3][0] |= (((disp_chars[i] >> 2) & 0x01) << (i*2+1));
		com_reg_contents[2][0] |= (((disp_chars[i] >> 3) & 0x01) << (i*2+0));
	}
	/* Last 3 digits */
	for (uint8_t i = 0; i < 3; i++)
	{
		com_reg_contents[1][1] |= (((disp_chars[7-i] >> 5) & 0x01) << (i*2+2));
		com_reg_contents[1][1] |= (((disp_chars[7-i] >> 0) & 0x01) << (i*2+1));
		com_reg_contents[0][1] |= (((disp_chars[7-i] >> 6) & 0x01) << (i*2+2));
		com_reg_contents[0][1] |= (((disp_chars[7-i] >> 1) & 0x01) << (i*2+1));
		com_reg_contents[3][1] |= (((disp_chars[7-i] >> 4) & 0x01) << (i*2+2));
		com_reg_contents[3][1] |= (((disp_chars[7-i] >> 2) & 0x01) << (i*2+1));
		com_reg_contents[2][1] |= (((disp_chars[7-i] >> 3) & 0x01) << (i*2+2));
	}
	/* 4th digit */
	com_reg_contents[1][1] |= (((disp_chars[4] >> 0) & 0x01) << (3*2+1));
	com_reg_contents[0][1] |= (((disp_chars[4] >> 1) & 0x01) << (3*2+1));
	com_reg_contents[3][1] |= (((disp_chars[4] >> 2) & 0x01) << (3*2+1));
	com_reg_contents[1][2] |= (((disp_chars[4] >> 5) & 0x01) << 0);
	com_reg_contents[0][2] |= (((disp_chars[4] >> 6) & 0x01) << 0);
	com_reg_contents[3][2] |= (((disp_chars[4] >> 4) & 0x01) << 0);
	com_reg_contents[2][2] |= (((disp_chars[4] >> 3) & 0x01) << 0);
	
	/* dot array */
	if (dot_array[0])
		com_reg_contents[2][0] |= 0x02;
	if (dot_array[1])
		com_reg_contents[2][0] |= 0x20;
	if (dot_array[2])
		com_reg_contents[2][1] |= 0x80;
	if (dot_array[3])
		com_reg_contents[2][1] |= 0x08;
	
	/* Send it away! */
	send_disp_data((uint8_t*)com_reg_contents);
}

void current_value_to_text(uint8_t* currents, char* string, uint8_t* dot_array)
{
	for (uint8_t i = 0; i < 4; i++)
	{
		/* Currents are currents*10 */
		dot_array[i] = 1;
		if (currents[i] > 100)
		{
			dot_array[i] = 0;
			currents[i] /= 10;
		}
		
		uint8_t digit = currents[i] / 10;
		if (digit != 0)
			string[i*2] = '0' + digit;
		else
			string[i*2] = 0;
		
		digit = currents[i] % 10;
		string[i*2+1] = '0' + digit;
	}
}

int main(void)
{
	/* Current readings vars */
	char current_strings[8];
	uint8_t currents_dots[4];
	
	/* Setup low level hardware */
	setup_hw();
	
	/* Bring-up test */
	char test_string[8];
	uint8_t full_dot_array[] = {1,1,1,1};
	uint8_t empty_dot_array[] = {0,0,0,0};
	for (uint8_t i = 0; i < 10; i++)
	{
		while ((interrupt_counter & 0x3F) != 0x00);
		for (uint8_t j = 0; j < 8; j++)
			test_string[j] = i + '0';
		if (i & 0x01)
			display_text(test_string, empty_dot_array);
		else
			display_text(test_string, full_dot_array);
		while ((interrupt_counter & 0x3F) == 0x00);
	}
	display_text("BAT MON", empty_dot_array);
	while ((interrupt_counter & 0x3F) != 0x00);
	
	/* Start conversion */
	ADC_ST_CONV();

    /* Initial relay states: off to keep contact by default */
    if ((PORTC.IN & 0x08) == 0)
        ISOL1_CLR();
    else
        ISOL1_SET();
    if ((PORTC.IN & 0x04) == 0)
        ISOL2_CLR();
    else
        ISOL2_SET();
	
    /* Main loop */
	uint8_t sm = 0;
    while (1) 
    {
        /* Check for issues */
        if (bat_disc_detected)
        {
            memcpy(current_strings, "DISCON", 7);
            current_strings[7] = '0' + bat_disc_detected;
            display_text(current_strings, empty_dot_array);
        }
        else
        {
            /* Normal behavior */
		    if (sm == 0)
		    {
			    /* Display currents */
			    current_value_to_text((uint8_t*)current_readings, current_strings, currents_dots);
			    display_text(current_strings, currents_dots);
		    }
		    else if (sm == 1)
		    {
			    /* Display voltage */
			    uint16_t voltage_reading_copy = voltage_reading;
			    uint8_t dot_array[] = {0,1,0,0};
			    uint16_t digit;
			    for (uint16_t i = 0; i < 5; i++)
			    {
				    digit = voltage_reading_copy % 10;
				    current_strings[5-i] = digit + '0';
				    voltage_reading_copy /= 10;
			    }
			    current_strings[0] = 0;current_strings[6] = 'V';current_strings[7] = 0;
			    display_text(current_strings, dot_array);
		    }
        }
		
		/* State machine update */
		if (interrupt_counter == 0x00)
		{
			while (interrupt_counter == 0x00);
			if(sm++ == 1)
			{
				sm = 0;
			}
		}
    }
}

