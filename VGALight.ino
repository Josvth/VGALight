
#include <SPI.h>
#include <Arduino.h>
#include <FastLED.h>
#include <stdint.h>

// All timing values are defined as:
// [duration in us] / ([timer scaler] / 16MHz)

// Timings	
#define COLOUR_FRONT_PORCH		75		
#define COLOUR_VISIBLE_AREA		256		

// Delays
#define TIMER1_INITIAL_VALUE	46		// = 2.860 / (1/16MHz)

#define COMPARE_INTERRUPT_DELAY 2		// = 960ns / (1/16MHz)

// Port definitions
#define VSYNC PORTD2
#define HSYNC PORTD3

#define LED_DATA_PIN 8

#define RED_CS PORTD4
#define GREEN_CS PORTD5
#define BLUE_CS PORTD6
#define MEASURE_S PORTD7

// State encoding
typedef enum device_state_t {
	IDLE,
	MEASURE,
	REFRESH
};

device_state_t device_state = IDLE;

typedef enum measure_state_t {
	RED_MEASURE = 1 << RED_CS,
	GREEN_MEASURE = 1 << GREEN_CS,
	BLUE_MEASURE = 1 << BLUE_CS
};
measure_state_t measure_state = BLUE_MEASURE;

// Led strip
#define NUM_LEDS 13

#define TOTAL_HSYNCS	800						// Total of HSYNCS between one VSYNC
#define OFFSET_HSYNCS	4						// Offset due to that the HSYNC's go on during VSYNC's
#define B_BLANK_HSYNCS	28						// HSYNC's before the visible lines start
#define F_BLANK_HSYNCS	3						// HYSNC's after the visible lines
#define VISIBLE_HSYNCS	768						// Total visible lines

#define LINES_PER_LED							VISIBLE_HSYNCS / NUM_LEDS
#define BRIGHTNESS_MULTIPLIER					2

uint16_t hsync_index = 0;						// Index for HSYNC

volatile uint8_t data_buffer[TOTAL_HSYNCS];		// Data from measurements

CRGB leds[NUM_LEDS];							// Led colors

void setup(void)
{

	Serial.begin(9600);

	// Setup SPI
	SPI.begin();
	SPI.setBitOrder(MSBFIRST);
	SPI.setClockDivider(SPI_CLOCK_DIV2);
	SPI.setDataMode(SPI_MODE0);

	// Setup ports
	DDRD &= ~((1 << VSYNC) | (1 << HSYNC));							// Set trigger ports to inputs

	DDRD |= (1 << RED_CS) | (1 << GREEN_CS) | (1 << BLUE_CS) | (1 << MEASURE_S);	// Set select ports to outputs

	// Set port initials
	PORTD |= (1 << RED_CS) | (1 << GREEN_CS) | (1 << BLUE_CS) | (1 << MEASURE_S);	// Set all adc's into sample and hold mode

	// Setup sample timer1
	TCCR1A = 0x00;
	TCCR1B = 0x00;
	TCCR1C = 0x00;
	TCNT1 = 0x00;

	TIMSK1 |= (1 << OCIE1B) | (1 << OCIE1A);						// Enable interrupt on compare A and B of timer 1

	OCR1A = COLOUR_FRONT_PORCH - COMPARE_INTERRUPT_DELAY;
	OCR1B = COLOUR_FRONT_PORCH + COLOUR_VISIBLE_AREA - COMPARE_INTERRUPT_DELAY;

	// Setup external interrupts
	EICRA |= (1 << ISC11) | (1 << ISC10) | (1 << ISC01);			// External interrupt on falling edge of VSYNC and HSYNC

	FastLED.addLeds<NEOPIXEL, LED_DATA_PIN>(leds, NUM_LEDS);		// Tell the FastLED library what leds we are controlling

	for (int i = 0; i < NUM_LEDS; i++) {							// Set all leds default to black
		leds[i] = CRGB::Black;
	}
		
	FastLED.show();													// Refresh leds

	EIMSK |= (1 << INT0);						// Enable VSYNC external interrupt

}

void loop(void){

}

// Falling edge VSYNC
ISR(INT0_vect)
{

	EIMSK &= ~(1 << INT1);						// Disable HSYNC external interrupt	
	TCCR1B = 0x00;								// Disable timer 1

	 if (device_state == REFRESH) {						// Previous state was refresh
		 
		device_state = MEASURE;							// Current state is measure

		if (measure_state == RED_MEASURE) {				// Previous measure state was measuring red
			measure_state = GREEN_MEASURE;				// Now we measure green
		}
		else if (measure_state == GREEN_MEASURE) {
			measure_state = BLUE_MEASURE;
		}
		else if (measure_state == BLUE_MEASURE) {
			measure_state = RED_MEASURE;
		}

		EIMSK |= (1 << INT1);							// Enable HSYNC external interrupt	
		
		hsync_index = 0;							

	} else if (device_state == MEASURE) {		// Previous state was a measure state

		device_state = REFRESH;					// Current state is refresh

		uint8_t lines_per_led_count = 0;
		uint8_t led = 0;
		uint16_t color = 0;

		// We do measure every line, even the not visible ones, but we only process the data from the 
		for (uint16_t i = OFFSET_HSYNCS + B_BLANK_HSYNCS; i < OFFSET_HSYNCS + B_BLANK_HSYNCS + VISIBLE_HSYNCS; i++) {

			color += data_buffer[i] * 2;

			lines_per_led_count++;

			if (lines_per_led_count > LINES_PER_LED) {
				lines_per_led_count = 0;

				color = color / (LINES_PER_LED);

				if (measure_state == RED_MEASURE) {
					leds[NUM_LEDS - 1 - led].r = color;
				}
				else if (measure_state == GREEN_MEASURE) {
					leds[NUM_LEDS - 1 - led].g = color;
				}
				else if (measure_state == BLUE_MEASURE) {
					leds[NUM_LEDS - 1 - led].b = color;
				}

				led++;
			}

		}

		// We set the last led
		color = color / (LINES_PER_LED);

		if (measure_state == RED_MEASURE) {
			leds[NUM_LEDS - 1 - led].r = color;
		}
		else if (measure_state == GREEN_MEASURE) {
			leds[NUM_LEDS - 1 - led].g = color;
		}
		else if (measure_state == BLUE_MEASURE) {
			leds[NUM_LEDS - 1 - led].b = color;
		}

		FastLED.setBrightness(analogRead(0) / 4);
		FastLED.show();

	}else if (device_state == IDLE) {
		device_state = REFRESH;					// We pretend as if the last state was refresh so we start measuring next vsync
	}
	
}

// Rising edge HSYNC
ISR(INT1_vect, ISR_NAKED)
{
	char cSREG = SREG;	// We templorary store our status register

	TCNT1 = TIMER1_INITIAL_VALUE;			// Set timer 1 to zero plus our offset due to falling edge triggering and trigger delay
	TCCR1B |= (1 << CS10);					// Enable timer 1 with a clock of 16MHz

	hsync_index++;

	SREG = cSREG;		// We restore our status register
	reti();				// We jump back
}

// Begin of averaging
ISR(TIMER1_COMPA_vect, ISR_NAKED)
{
	char cSREG = SREG;	// We templorary store our status register

	PORTD &= ~(1 << MEASURE_S);	// We start averaging

	data_buffer[hsync_index] = SPI.transfer(0xFF) << 3;
	data_buffer[hsync_index] |= SPI.transfer(0xFF) >> 5;

	SREG = cSREG;		// We restore our status register
	reti();				// We jump back
}

// End of averaging start of sample
ISR(TIMER1_COMPB_vect, ISR_NAKED)
{
	char cSREG = SREG;	// We templorary store our status register

	TCCR1B = 0x00;						// Disable timer 1

	PORTD |= measure_state;				// Put the current adc in sample and hold mode
	PORTD |= measure_state;				// Put the current adc in sample and hold mode
	PORTD |= measure_state;				// Put the current adc in sample and hold mode
	PORTD &= ~measure_state;			// Put the current adc in convert mode
	
	PORTD |= (1 << MEASURE_S);			// We end averaging

	SREG = cSREG;		// We restore our status register
	reti();				// We jump back
}

