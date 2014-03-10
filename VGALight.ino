
#include <stdint.h>
#include <Arduino.h>
#include <FastLED.h>

// Sample timings
#define FRONT_PORCH 0
#define BACK_PORCH 0
#define SAMPLE_TIME 0

// We only use Port D for external triggers and pin changes
#define VSYNC PORTD2
#define HSYNC PORTD3
#define RGB PORTD4

// And we use only Port B for our outputing and SPI
#define LED_PIN PORTB0

#define RED_CS PORTB1
#define GREEN_CS PORTB2
#define BLUE_CS PORTB3

// State encoding
#define IDLE 0
#define MEASURE 1
#define RED_AQUIRE 2
#define GREEN_AQUIRE 3
#define BLUE_AQUIRE 4
#define CONVERT 5
#define REFRESH 6

volatile uint8_t state = IDLE;

// SPI states
#define SPI_IDLE 0
#define SPI_FIRST_BYTE 1
#define SPI_SECOND_BYTE 2

volatile uint8_t spi_state = SPI_IDLE;
volatile uint16_t *spi_data_buffer;

// Sample positions
#define SAMPLE_LEFT 0
#define SAMPLE_DUMMY 1
#define SAMPLE_RIGHT 2

volatile uint8_t sample_position = SAMPLE_LEFT;

// Sample buffers
volatile uint16_t left_sample_buffer = 0;
volatile uint16_t right_sample_buffer = 0;

volatile CRGB leds[120];

void setup(void)
{
	
	// Setup ports
	DDRD &= ~((1 << VSYNC) | (1 << HSYNC) | (1 << RGB));			// Set trigger ports to inputs
	
	DDRB |= (1 << RED_CS) | (1 << GREEN_CS) | (1 << BLUE_CS);		// Set select ports to outputs
	DDRB |= (1 << LED_PIN);											// Set led pin output

	// Set port initials
	PORTB |= (1 << RED_CS) | (1 << GREEN_CS) | (1 << BLUE_CS);		// Set all adc's into sample and hold mode

	// Setup HSYNC timer1
	TIMSK1 |= (1 << OCIE1B) | (1 << OCIE1A);						// Enable interrupt on compare A and B of timer 1

	// Setup sample timer2
	TIMSK2 |= (1 << OCIE2B);										// Enable interrupt on compare B of timer 1
	OCR2B = SAMPLE_TIME;											// Set compare register B to the sample time

	// Setup SPI
	SPCR |= (1 << SPE) | (1 << MSTR) | (1 << CPHA);					// Enable SPI as a master with sampling on the falling edge
	SPSR |= (1 << SPI2X);											// Set SPI clock to 8 MHz

	// Setup external interrupts
	EICRA |= (1 << ISC11) | (1 << ISC01);		// External interrupt on falling edge of VSYNC and HSYNC

	EIMSK |= (1 << INT0);						// Enable VSYNC external interrupt

	sei();										// Turn on interrupts

}

void loop(void)
{

	switch (state) 
	{
	case RED_AQUIRE:
	case GREEN_AQUIRE:
	case BLUE_AQUIRE:
		// Crunch data from ADC's
		break;
	case REFRESH:
		// Refresh led strip
		break;
	}

}

// Falling edge VSYNC
ISR(INT0_vect) 
{	
	switch (state)
	{
	case IDLE:

		state = RED_AQUIRE;

		//state = MEASURE;

		EIMSK |= (1 << INT1);						// Enable HSYNC external interrupt	

		break;
	case MEASURE:
		// TODO THIS STATE
		break;
	case RED_AQUIRE:

		// Get data for right side of last line
		
		PORTB &= ~(1 << RED_CS);	// Set red ADC in sample and hold mode	
		PORTB |= (1 << GREEN_CS);	// Set green ADC in convert mode

		state = GREEN_AQUIRE;
		break;
	case GREEN_AQUIRE:

		// Get data for right side of last line

		PORTB &= ~(1 << GREEN_CS);	// Set green ADC in sample and hold mode	
		PORTB |= (1 << BLUE_CS);	// Set blue ADC in convert mode

		state = BLUE_AQUIRE;
		break;
	case BLUE_AQUIRE:
		
		TCCR1B &= ~(1 << CS10);		// Disable timer 1
		EIMSK |= (1 << INT1);		// Disable HSYNC external interrupt	

		// Get data for right side of last line

		PORTB &= ~(1 << BLUE_CS);	// Set blue ADC in sample and hold mode	
		
		state = REFRESH;
		break;
	case REFRESH:
		
		PORTB |= (1 << RED_CS);		// Set red ADC in convert mode

		state = RED_AQUIRE;
	default:
		state = IDLE;
		break;
	}
}

// Falling edge HSYNC
ISR(INT1_vect) {
	TCNT1	= 0;					// Set timer 1 to zero
	TCCR1B |= (1 << CS10);			// Enable timer 1 with a clock of 16MHz
}

// Begin of left sampling
ISR(TIMER1_COMPA_vect) 
{
	startSample();
}

// Begin of right sampling
ISR(TIMER1_COMPB_vect) 
{
	startSample();
}

// End of sampling time
ISR(TIMER2_COMPA_vect) 
{
	stopSample();

	// TODO Start SPI transfer

}

void startSample(void) {

	if (state == RED_AQUIRE) {
		PORTB |= (1 << RED_CS);		// Put the red adc in sample mode
	}

	if (state == GREEN_AQUIRE) {
		PORTB |= (1 << GREEN_CS);	// Put the green adc in sample mode
	}

	if (state == BLUE_AQUIRE) {
		PORTB |= (1 << BLUE_CS);	// Put the blue adc in sample mode
	}

	TCNT2 = 0;					// Set timer 2 to 0
	TCCR2B |= (1 << CS20);		// Start timer 2 with a clock of 16MHz

}

void stopSample(void) {
	
	if (state == RED_AQUIRE) {
		PORTB &= ~(1 << RED_CS);	// Put the red adc in convert mode
	}

	if (state == GREEN_AQUIRE) {
		PORTB &= ~(1 << GREEN_CS);	// Put the green adc in convert mode
	}

	if (state == BLUE_AQUIRE) {
		PORTB &= ~(1 << BLUE_CS);	// Put the blue adc in convert mode
	}

	TCCR2B &= ~(1 << CS20);			// Stop timer 2

}

// Reads the data from the selected ADC
void readADC(uint16_t *buffer) 
{
	spi_data_buffer = buffer;
	spi_state = SPI_FIRST_BYTE;
	SPDR = 0xFF;					// Initiate transfer
}

// SPI transfer complete
ISR(SPI_STC_vect)
{
	if (spi_state == SPI_FIRST_BYTE) 
	{
		*spi_data_buffer = SPDR << 6;
		spi_state = SPI_SECOND_BYTE;
		SPDR = 0xFF;					// Initiate transfer
		return;
	}

	if (spi_state == SPI_SECOND_BYTE) 
	{
		*spi_data_buffer &= SPDR >> 1;
		spi_state = IDLE;

		if (sample_position == SAMPLE_LEFT) // If we just did a left sample we initiate another sample to get the data
		{	 

		}

		return;
	}

	
}