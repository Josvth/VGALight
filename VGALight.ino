
#include <SPI.h>
#include <Arduino.h>
//#include <FastLED.h>
#include <stdint.h>

// Sample timings
#define FRONT_PORCH 67
#define BACK_PORCH 0
#define SAMPLE_TIME 32

#define HSYNC_PULSE_WIDTH 28

// Delays due to slowness of arduino
#define HSYNC_INTERRUPT_DELAY 12
#define COMPARE_INTERRUPT_DELAY 21

// We only use Port D for external triggers and pin changes
#define VSYNC PORTD2
#define HSYNC PORTD3

#define LED_PIN PORTB0

#define RED_CS PORTD4
#define GREEN_CS PORTD5
#define BLUE_CS PORTD6
#define AVERAGE_S PORTD7

// State encoding
typedef enum { IDLE, MEASURE, RED_AQUIRE, GREEN_AQUIRE, BLUE_AQUIRE, REFRESH } device_state_t;
device_state_t device_state = IDLE;

// Sample positions
#define SAMPLE_LEFT 0
#define SAMPLE_DUMMY 1
#define SAMPLE_RIGHT 2

volatile uint8_t sample_position = SAMPLE_LEFT;

// Sample buffers
volatile uint16_t left_sample_buffer = 0;
volatile uint16_t right_sample_buffer = 0;

// ledstrip
#define NUM_LEDS 120
#define DATA_PIN 11
#define CLOCK_PIN 13
//CRGB leds[NUM_LEDS];

#define NUM_LINES_PER_LED 20 
long counterlinesperled = 0;  /// counts lines per led
long led = 0; //which led?  

volatile int sample_begin = 0;

void setup(void)
{
	
	// Setup SPI
	//SPCR = 0xFF;
	//SPSR = 0xFF;
	//SPCR |= (1 << SPIE) | (1 << SPE) | (1 << MSTR) | (1 << CPHA);	// Enable SPI as a master with sampling on the falling edge with interrupt
	//SPSR |= (1 << SPI2X);											// Set SPI clock to 8 MHz
	SPI.begin();
	SPI.setBitOrder(MSBFIRST);
	SPI.setClockDivider(SPI_CLOCK_DIV2);
	SPI.setDataMode(SPI_MODE1);

	// Setup ports
	DDRD &= ~((1 << VSYNC) | (1 << HSYNC));							// Set trigger ports to inputs
	
	DDRD |= (1 << RED_CS) | (1 << GREEN_CS) | (1 << BLUE_CS) | (1 << AVERAGE_S);		// Set select ports to outputs
	DDRB |= (1 << LED_PIN);											// Set led pin output

	// Set port initials
	PORTD |= (1 << RED_CS) | (1 << GREEN_CS) | (1 << BLUE_CS);		// Set all adc's into sample and hold mode

	// Setup HSYNC timer1
	TCCR1A = 0x00;
	TCCR1B = 0x00;
	TCCR1C = 0x00;
	TCNT1  = 0x00;

	TIMSK1 |= (1 << OCIE1B) | (1 << OCIE1A);						// Enable interrupt on compare A and B of timer 1

	OCR1A = FRONT_PORCH - COMPARE_INTERRUPT_DELAY + sample_begin;
	OCR1B = OCR1A + SAMPLE_TIME;

	// Setup sample timer2
	TCCR2A &= 0x00;
	TCCR2B &= 0x00;

	TIMSK2 |= (1 << OCIE2A) /*| (1 << OCIE2B)*/;					// Enable interrupt on compare B of timer 1
	OCR2B = SAMPLE_TIME;											// Set compare register B to the sample time

	// Setup external interrupts
	EICRA |= (1 << ISC11) | (1 << ISC01);		// External interrupt on falling edge of VSYNC and HSYNC

	//EIMSK |= (1 << INT0);						// Enable VSYNC external interrupt

	//FastLED.addLeds<NEOPIXEL, DATA_PIN>(leds, NUM_LEDS);   

	EIMSK |= (1 << INT1);						// Enable HSYNC external interrupt	
	
	//TCNT1 = 0;	// Set timer 1 to zero
	//TCCR1B |= (1 << CS10);						// Enable timer 1 with a clock of 16MHz

	sei();										// Turn on interrupts

}

void loop(void)
{
	//PORTB |= (1 << BLUE_CS);		// Put the red adc in sample mode
	//PORTB &= ~(1 << BLUE_CS);	// Put the red adc in convert mode
}

typedef enum { SPI_IDLE, FIRST_BYTE_TRANSFER, SECOND_BYTE_TRANSFER } spi_state_t;

volatile spi_state_t spi_state = SPI_IDLE;
volatile uint8_t *spi_data_buffer;
void(*volatile spi_callback)(void);

//// Reads the data from the selected ADC
void initateSPITransfer(unsigned char *buffer, void(*callback)(void))
{
	spi_data_buffer = buffer;
	spi_callback = callback;
	spi_state = FIRST_BYTE_TRANSFER;
	SPDR = 0xFF;					// Initiate transfer
	
}

// SPI transfer complete
ISR(SPI_STC_vect)
{
	
	PORTD |= (1 << GREEN_CS);
	
	if (spi_state == FIRST_BYTE_TRANSFER)
	{
		*spi_data_buffer = SPDR << 6;
		spi_state = SECOND_BYTE_TRANSFER;
		SPDR = 0xFF;					// Initiate transfer
		PORTD &= ~(1 << GREEN_CS);
		return;
	}

	if (spi_state == SECOND_BYTE_TRANSFER)
	{
		*spi_data_buffer |= SPDR >> 1;
		spi_state = SPI_IDLE;

		if (spi_callback != NULL)
		{
			(*spi_callback)();
		}
		PORTD &= ~(1 << GREEN_CS);
		return;
	}

}

// Falling edge VSYNC
ISR(INT0_vect) 
{	
	//switch (state)
	//{
	//case IDLE:

	//	state = RED_AQUIRE;

	//	//state = MEASURE;

	//	EIMSK |= (1 << INT1);						// Enable HSYNC external interrupt	

	//	break;
	//case MEASURE:
	//	// TODO THIS STATE
	//	break;
	//case RED_AQUIRE:

	//	// Get data for right side of last line
	//	
	//	PORTB &= ~(1 << RED_CS);	// Set red ADC in sample and hold mode	
	//	PORTB |= (1 << GREEN_CS);	// Set green ADC in convert mode

	//	state = GREEN_AQUIRE;
	//	break;
	//case GREEN_AQUIRE:

	//	// Get data for right side of last line

	//	PORTB &= ~(1 << GREEN_CS);	// Set green ADC in sample and hold mode	
	//	PORTB |= (1 << BLUE_CS);	// Set blue ADC in convert mode

	//	state = BLUE_AQUIRE;
	//	break;
	//case BLUE_AQUIRE:
	//	
	//	TCCR1B &= ~(1 << CS10);		// Disable timer 1
	//	EIMSK |= (1 << INT1);		// Disable HSYNC external interrupt	

	//	// Get data for right side of last line

	//	PORTB &= ~(1 << BLUE_CS);	// Set blue ADC in sample and hold mode	
	//	
	//	state = REFRESH;
	//	break;
	//case REFRESH:
	//	
	//	PORTB |= (1 << RED_CS);		// Set red ADC in convert mode

	//	state = RED_AQUIRE;
	//default:
	//	state = IDLE;
	//	break;
	//}
}

// Falling edge HSYNC
ISR(INT1_vect, ISR_NAKED) {
	
	char cSREG = SREG;	// We templorary store our status register

	PORTD |= (1 << BLUE_CS);			
	TCNT1 = HSYNC_PULSE_WIDTH + HSYNC_INTERRUPT_DELAY;	// Set timer 1 to zero plus our offset due to falling edge triggering and trigger delay
	TCCR1B |= (1 << CS10);								// Enable timer 1 with a clock of 16MHz
	PORTD &= ~(1 << BLUE_CS);			
	
	// TODO Start SPI TRANSFER

	SREG = cSREG;		// We restore our status register
	reti();				// We jump back
}

// Begin of averaging
ISR(TIMER1_COMPA_vect, ISR_NAKED)
{
	char cSREG = SREG;	// We templorary store our status register

	PORTD &= ~(1 << AVERAGE_S);	// We start averaging
	PORTD |= (1 << RED_CS);		// Put the adc in hold mode

	SREG = cSREG;		// We restore our status register
	reti();				// We jump back
}

// End of averaging start of sample
ISR(TIMER1_COMPB_vect, ISR_NAKED)
{
	char cSREG = SREG;	// We templorary store our status register

	PORTD &= ~(1 << RED_CS);		// Put the adc in sample mode
	PORTD |= (1 << AVERAGE_S);		// We end averaging

	SREG = cSREG;		// We restore our status register
	reti();				// We jump back
}

//void startSample(void) 
//{
//
//	if (state == RED_AQUIRE) 
//	{
//		PORTB |= (1 << RED_CS);		// Put the red adc in sample mode
//	} else if (state == GREEN_AQUIRE) 
//	{
//		PORTB |= (1 << GREEN_CS);	// Put the green adc in sample mode
//	} else if (state == BLUE_AQUIRE) 
//	{
//		PORTB |= (1 << BLUE_CS);	// Put the blue adc in sample mode
//	}
//
//	TCCR2B |= (1 << CS20);		// Start timer 2 with a clock of 16MHz
//
//}
//
//void stopSample(void) 
//{
//	
//	if (state == RED_AQUIRE) 
//	{
//		PORTB &= ~(1 << RED_CS);	// Put the red adc in convert mode
//	} else if (state == GREEN_AQUIRE) 
//	{
//		PORTB &= ~(1 << GREEN_CS);	// Put the green adc in convert mode
//	} else if (state == BLUE_AQUIRE) 
//	{
//		PORTB &= ~(1 << BLUE_CS);	// Put the blue adc in convert mode
//	}
//
//	TCCR2B &= ~(1 << CS20);			// Stop timer 2
//	TCNT2 = 0;						// Set timer 2 to 0
//
//}

//void someCallback() {
//	initateSPITransfer();
//}

