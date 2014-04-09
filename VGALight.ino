
#include <SPI.h>
#include <Arduino.h>
#include <FastLED.h>
#include <stdint.h>

// Timings
#define HSYNC_BACK_PORCH		80

#define COLOUR_FRONT_PORCH		67
#define COLOUR_VISIBLE_AREA		256
#define COLOUR_BACK_PORCH		0

// Ideal measure timings
#define MEASURE_TIME	32
#define LEFT_MEASURE	COLOUR_FRONT_PORCH
#define RIGHT_MEASURE	COLOUR_FRONT_PORCH + COLOUR_VISIBLE_AREA - MEASURE_TIME

// Delays
#define VSYNC_PULSE_WIDTH		20
#define VSYNC_INTERRUPT_DELAY	20

#define HSYNC_PULSE_WIDTH		28
#define HSYNC_INTERRUPT_DELAY	12

#define COMPARE_INTERRUPT_DELAY 21

// We only use Port D for external triggers and pin changes
#define VSYNC PORTD2
#define HSYNC PORTD3

#define LED_DATA_PIN 8

#define RED_CS PORTD4
#define GREEN_CS PORTD5
#define BLUE_CS PORTD6
#define MEASURE_S PORTD7

// State encoding
typedef enum { IDLE, MEASURE, REFRESH } device_state_t;
device_state_t device_state = IDLE;

typedef enum { RED_MEASURE, GREEN_MEASURE, BLUE_MEASURE } measure_state_t;
measure_state_t measure_state = RED_MEASURE;

// Led strip
#define NUM_LEDS 120
#define NUM_LINES_PER_LED 20 

CRGB leds[NUM_LEDS];

uint8_t current_led = 0;	//which led?  
uint8_t current_lines = 0;  // counts lines per led

volatile uint8_t adc_data = 0;

void setup(void)
{
	
	// Setup SPI
	SPI.begin();
	SPI.setBitOrder(MSBFIRST);
	SPI.setClockDivider(SPI_CLOCK_DIV2);
	SPI.setDataMode(SPI_MODE1);

	// Setup ports
	DDRD &= ~((1 << VSYNC) | (1 << HSYNC));							// Set trigger ports to inputs
	
	DDRD |= (1 << RED_CS) | (1 << GREEN_CS) | (1 << BLUE_CS) | (1 << MEASURE_S);	// Set select ports to outputs

	// Set port initials
	PORTD |= (1 << RED_CS) | (1 << GREEN_CS) | (1 << BLUE_CS) | (1 << MEASURE_S);	// Set all adc's into sample and hold mode

	// Setup sample timer1
	TCCR1A	= 0x00;
	TCCR1B	= 0x00;
	TCCR1C	= 0x00;
	TCNT1	= 0x00;
	OCR1A	= 0x00;
	OCR1B	= 0x00;

	TIMSK1 |= (1 << OCIE1B) | (1 << OCIE1A);						// Enable interrupt on compare A and B of timer 1

	OCR1A = LEFT_MEASURE - COMPARE_INTERRUPT_DELAY;
	OCR1B = LEFT_MEASURE + COLOUR_VISIBLE_AREA - COMPARE_INTERRUPT_DELAY;

	// Setup HSYNC start timer2
	TCCR2A	= 0x00;
	TCCR2B	= 0x00;
	TCNT2	= 0x00;
	OCR2A	= 0x00;
	OCR2B	= 0x00;
	TIMSK2	= 0x00;
	TIFR2	= 0x00;
	ASSR	= 0x00;
	GTCCR	= 0x00;

	TIMSK2 |= (1 << OCIE2A);									// Enable interrupt on compare A of timer 2
	
	OCR2A = HSYNC_BACK_PORCH - COMPARE_INTERRUPT_DELAY;			// Set compare A value

	// Setup external interrupts
	EICRA |= (1 << ISC11) | (1 << ISC01);		// External interrupt on falling edge of VSYNC and HSYNC

	EIMSK |= (1 << INT0);						// Enable VSYNC external interrupt

	FastLED.addLeds<NEOPIXEL, LED_DATA_PIN>(leds, NUM_LEDS);

	sei();										// Turn on interrupts

}

void loop(void)
{
	if (device_state == MEASURE) {
		convertADCData();
		return;
	}

	if (device_state == REFRESH) {
		 
		return;
	}
}

void convertADCData() {
	uint8_t normalized_data = adc_data / NUM_LINES_PER_LED;
	if (measure_state == RED_MEASURE) {
		leds[current_led].r += normalized_data;
		leds[current_led + 60].r += normalized_data;
	}
	else if (measure_state == GREEN_MEASURE) {
		leds[current_led].g += normalized_data;
		leds[current_led + 60].g += normalized_data;
	}
	else if (measure_state == BLUE_MEASURE) {
		leds[current_led].b += normalized_data;
		leds[current_led + 60].b += normalized_data;
	}
	if (current_lines <= NUM_LINES_PER_LED) {
		current_lines + 1;
	}
	else {
		current_lines = 0;
		if (current_led + 1 < NUM_LEDS / 2) {
			current_led++;
		}
	}
	adc_data = 0;
}

// Falling edge VSYNC
ISR(INT0_vect) 
{	
	
	EIMSK &= ~(1 << INT1);						// Disable HSYNC external interrupt	
	TCCR1B = 0x00;								// Disable timer 1

	if (device_state == IDLE || device_state == REFRESH) {

		TCNT2 = VSYNC_PULSE_WIDTH + VSYNC_INTERRUPT_DELAY;	// Set timer 2 to zero plus our offset due to falling edge triggering and trigger delay
		TCCR2B |= (1 << CS22);								// Enable timer 2 with a clock of 16MHz / 64

		device_state = MEASURE;

	} else if (device_state == MEASURE) {
		
		convertADCData();
		current_lines = 0;
		current_led = 0;

		if (measure_state == RED_MEASURE) {
			measure_state = GREEN_MEASURE;

			TCNT2 = VSYNC_PULSE_WIDTH + VSYNC_INTERRUPT_DELAY;	// Set timer 2 to zero plus our offset due to falling edge triggering and trigger delay
			TCCR2B |= (1 << CS22);								// Enable timer 2 with a clock of 16MHz / 64

		}
		else if (measure_state == GREEN_MEASURE) {
			measure_state = BLUE_MEASURE;

			TCNT2 = VSYNC_PULSE_WIDTH + VSYNC_INTERRUPT_DELAY;	// Set timer 2 to zero plus our offset due to falling edge triggering and trigger delay
			TCCR2B |= (1 << CS22);								// Enable timer 2 with a clock of 16MHz / 64

		}
		else if (measure_state == BLUE_MEASURE) {
			measure_state = RED_MEASURE;

			device_state = REFRESH;
		}

	}

}

// Start of HSYNC 
ISR(TIMER2_COMPA_vect, ISR_NAKED)
{
	char cSREG = SREG;	// We templorary store our status register

	EIMSK |= (1 << INT1);	// Enable HSYNC external interrupt	
	TCCR2B = 0x00;			// Disable timer 2

	SREG = cSREG;		// We restore our status register
	reti();				// We jump back
}

// Falling edge HSYNC
ISR(INT1_vect, ISR_NAKED) 
{
	char cSREG = SREG;	// We templorary store our status register

	TCNT1 = HSYNC_PULSE_WIDTH + HSYNC_INTERRUPT_DELAY;	// Set timer 1 to zero plus our offset due to falling edge triggering and trigger delay
	TCCR1B |= (1 << CS10);								// Enable timer 1 with a clock of 16MHz
	
	SREG = cSREG;		// We restore our status register
	reti();				// We jump back
}

// Begin of averaging
ISR(TIMER1_COMPA_vect, ISR_NAKED)
{
	char cSREG = SREG;	// We templorary store our status register

	PORTD &= ~(1 << MEASURE_S);	// We start averaging
		
	adc_data &= SPI.transfer(0xFF) << 2;
	adc_data |= SPI.transfer(0xFF) >> 6;

	SREG = cSREG;		// We restore our status register
	reti();				// We jump back
}

// End of averaging start of sample
ISR(TIMER1_COMPB_vect, ISR_NAKED)
{
	char cSREG = SREG;	// We templorary store our status register

	if (measure_state == RED_MEASURE) {
		PORTD |= (1 << RED_CS);			// Put red the adc in hold mode
		PORTD |= (1 << RED_CS);			// Put red the adc in hold mode
		PORTD &= ~(1 << RED_CS);		// Put the red adc in sample mode
	}
	else if (measure_state == GREEN_MEASURE) {
		PORTD |= (1 << GREEN_CS);		// Put green the adc in hold mode
		PORTD |= (1 << GREEN_CS);		// Put green the adc in hold mode
		PORTD &= ~(1 << GREEN_CS);		// Put the green adc in sample mode
	}
	else if (measure_state == BLUE_MEASURE) {
		PORTD |= (1 << BLUE_CS);		// Put blue the adc in hold mode
		PORTD |= (1 << BLUE_CS);		// Put blue the adc in hold mode
		PORTD &= ~(1 << BLUE_CS);		// Put the blue adc in sample mode
	}

	PORTD |= (1 << MEASURE_S);			// We end averaging

	SREG = cSREG;		// We restore our status register
	reti();				// We jump back
}

