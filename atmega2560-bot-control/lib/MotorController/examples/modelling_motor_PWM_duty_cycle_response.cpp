#include <Arduino.h>

#include "MotorController.h"
#include "PhaseCorrect16BitPWM.h"

// This pin is attached to direction control
// input of motor driver
#define DIRECTION_PIN 23
// Encoder outputs attached to these
// interrupt pins
#define ENCODER_PIN_A 21
#define ENCODER_PIN_B 19

// Frequency at which control loop will be run
#define CONTROL_ROUTINE_CALL_FREQUENCY 50 // Hz

// Number of quadrature counts to complete
// one full rotation of the shaft
#define ENCODER_COUNTS_PER_ROTATION 840

// Time interval after which things are
// printed to serial monitor
#define SERIAL_PRINT_TIME_PERIOD 1E6 // us

// Motor controller object to 
// control motor angular velocity
MotorController motor_controller(
  DIRECTION_PIN,
  ENCODER_PIN_A,
  ENCODER_PIN_B,
  CONTROL_ROUTINE_CALL_FREQUENCY,
  ENCODER_COUNTS_PER_ROTATION,
  true
);

// Variable to fetch and store the current time
float current_time;

// Variable to store when the last serial print
// was performed in microsecond
float last_serial_print_time;

float iteration_num;
float target_angular_velocity;

float angular_velocity[5] = {0, 0, 0, 0, 0};
float pwm_duty_cycle[5] = {0, 0, 0, 0, 0};

float getMean(float array[5])
{
	return (array[0] + array[1] + array[2] + array[3] + array[4]) / 5.0;
}

float getStandardDeviation(float array[5])
{
	float mean = getMean(array);

	float mean_square = (
		array[0]*array[0] + 
		array[1]*array[1] + 
		array[2]*array[2] + 
		array[3]*array[3] + 
		array[4]*array[4]
	) / 5.0;

	return sqrt(mean_square - mean * mean);
}

void initializeTimer3();

void setup()
{
	// Initialize the Serial Monitor
	Serial.begin(115200);

	Timer1PhaseCorrectPWM::clearTimerSettings();
	Timer1PhaseCorrectPWM::setupTimer();
	Timer1PhaseCorrectPWM::setupChannelA();

	initializeTimer3();

	// Set max controller output according to bit resolution of PWM Duty cycle
	motor_controller.setMaxControllerOutput(65535);

	// motor_controller.set_polynomial_coefficients_positive_value_positive_acceleration(7548, 3739, -780, 93.8, -3.58);
	// motor_controller.set_polynomial_coefficients_positive_value_negative_acceleration(10664, 977, -71, 23.8, -1.22);
	// motor_controller.set_polynomial_coefficients_negative_value_negative_acceleration(-6889, 4173, 977, 119, 4.52);
	// motor_controller.set_polynomial_coefficients_negative_value_positive_acceleration(-10622, 1025, 144, 34.5, 1.62);

	// Initialize motor controller
	motor_controller.setPIDGains(2000, 2500, 0);

	motor_controller.enablePIDControl();

	// Initialize global variables	
	current_time = micros();
	last_serial_print_time	= current_time;

	iteration_num = 0;
	target_angular_velocity = 0;

	// Serial.println("Initialized successfully");
}

void loop()
{
	current_time = micros();

	if (current_time - last_serial_print_time > SERIAL_PRINT_TIME_PERIOD)
	{
		float mean_angular_velocity = getMean(angular_velocity);
		float std_dev_angular_velocity = getStandardDeviation(angular_velocity);

		if (	abs(mean_angular_velocity - target_angular_velocity) < 0.5 &&
				std_dev_angular_velocity < 0.5	) 
		{
			// Serial.print("Velocity :\tMean : ");
			// Serial.print(mean_angular_velocity);
			// Serial.print(" rad/s\tStd Dev : ");
			// Serial.print(std_dev_angular_velocity);
			// Serial.println(" rad/s");

			float mean_pwm_duty_cycle = getMean(pwm_duty_cycle);
			float std_dev_pwm_duty_cycle = getStandardDeviation(pwm_duty_cycle);

			// Serial.print("PWM Duty Cycle Output :\tMean : ");
			// Serial.print(mean_pwm_duty_cycle);
			// Serial.print("\tStd Dev : ");
			// Serial.println(std_dev_pwm_duty_cycle);

			Serial.println(
				String(mean_angular_velocity) + "," +
				String(std_dev_angular_velocity) + "," +
				String(mean_pwm_duty_cycle) + "," +
				String(std_dev_pwm_duty_cycle)
			);

			iteration_num += 1;
			target_angular_velocity = 12.0 * sin(2.0 * PI * iteration_num / 20.0);
			motor_controller.setMotorAngularVelocity(target_angular_velocity);

			// Serial.print("Next Target : ");
			// Serial.println(target_angular_velocity);
		}

		last_serial_print_time = current_time;
	}
}

void initializeTimer3()
{
	// stop interrupts
	cli();

	// Clear Timer/Counter Control Resgisters
	TCCR3A &= 0x00;
	TCCR3B &= 0x00;
	TCCR3C &= 0x00;
	// Clear Timer/Counter Register
	TCNT3 &= 0x0000;

	// Set Timer1 to interrupt at 50Hz

	// ATmega2560 clock frequency - 16MHz
	// Prescalers available for timers - 1, 8, 64, 256, 1024
	// Timer clock frequency = ATmega2560 clock frequency / Prescaler

	// To have interrupts with better frequency resolution timer
	// will be operated in Clear Timer on Compare Match (CTC) mode
	// TCNTx will count from 0x00 to the value in OCRnA register
	// Frequency of Interrupt = Timer clock frequency / Number of TCNTn Counts before it resets to 0x00

	// TCNTx will be compared to OCRnx in each clock cycle
	// Upon compare match, interrupt is fired
	// For 50Hz, TCNTx needs - 
	//   - 40000 counts at 8 prescaler
	//   - 5000 counts at 64 prescaler
	//   - 1250 counts at 256 prescaler
	//   - 312.5 counts at 1024 prescaler

	// Turn on CTC mode
	TCCR3B |= (0x01 << WGM32);
	// Set Prescaler to 256
	TCCR3B |= (0x01 << CS32);
	// Set compare match register (OCR3A) to 1250-1
	OCR3A = 0x04E1;
	// Enable interrupt upon compare match of OCR3A
	TIMSK3 |= (0x01 << OCIE3A);

	// allow interrupts
	sei();
}

ISR(TIMER3_COMPA_vect)
{
	motor_controller.updateAngularState();
	motor_controller.spinMotor();
	Timer1PhaseCorrectPWM::setDutyCyclePWMChannelA(motor_controller.getControllerOutput());

	angular_velocity[4] = angular_velocity[3];
	angular_velocity[3] = angular_velocity[2];
	angular_velocity[2] = angular_velocity[1];
	angular_velocity[1] = angular_velocity[0];
	angular_velocity[0] = motor_controller.getMotorAngularVelocity();
	
	pwm_duty_cycle[4] = pwm_duty_cycle[3];
	pwm_duty_cycle[3] = pwm_duty_cycle[2];
	pwm_duty_cycle[2] = pwm_duty_cycle[1];
	pwm_duty_cycle[1] = pwm_duty_cycle[0];
	pwm_duty_cycle[0] = motor_controller.getPIDOutput();
}