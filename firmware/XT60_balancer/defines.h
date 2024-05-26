/*
 * defines.h
 *
 * Created: 19/03/2024 21:41:11
 *  Author: limpkin
 */ 


#ifndef DEFINES_H_
#define DEFINES_H_

/* defines */
#define FALSE	0
#define TRUE	(!FALSE)

/* enums */
typedef enum	{BUZZ_OFF = 0, BUZZ_ON = 1, BUZZ_ON_OFF = 2} buzzer_buzz_type_t;

/* macros */
#define BUZZER_ON()		(PORTA.OUTSET = (1 << 4))
#define BUZZER_OFF()	(PORTA.OUTCLR = (1 << 4))
#define ISOL1_SET()		(PORTB.OUTSET = (1 << 5))
#define ISOL1_CLR()		(PORTB.OUTCLR = (1 << 5))
#define ISOL2_SET()		(PORTB.OUTSET = (1 << 4))
#define ISOL2_CLR()		(PORTB.OUTCLR = (1 << 4))
#define SPI_NCS_ASST()	(PORTA.OUTCLR = (1 << 2))
#define SPI_NCS_CLR()	(PORTA.OUTSET = (1 << 2))
#define SPI_CLK_SET()	(PORTA.OUTSET = (1 << 3))
#define SPI_CLK_CLR()	(PORTA.OUTCLR = (1 << 3))
#define SPI_DIO_SET()	(PORTA.OUTSET = (1 << 1))
#define SPI_DIO_CLR()	(PORTA.OUTCLR = (1 << 1))
#define ADC_ST_CONV()	(ADC0.COMMAND = 0x01)


#endif /* DEFINES_H_ */