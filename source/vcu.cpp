#include "vcu.h"
#include "mc.h"
#include "io.h"

// averages throttles and converts to percentage
static uint8_t throttle_average(uint32_t positive, uint32_t negative) {
    int32_t throttle[2];
    int32_t average;

    throttle[0] = (100 * (positive - THROTTLE_POS_MIN)) / (THROTTLE_POS_MAX - THROTTLE_POS_MIN);
    throttle[1] = (100 * (negative - THROTTLE_NEG_MAX)) / (THROTTLE_NEG_MIN - THROTTLE_NEG_MAX);
    average = (throttle[0] + throttle[1]) / 2;

    if(average > 100) {
        return(100);
    } else if(average < 0) {
        return(0);
    } else {
        return(average);
    }
}

// checks if brakes are active
static bool brakes_active(uint32_t front, uint32_t rear) {
    if((front > BFA) || (rear > BRA)) {
        return(true);
    } else {
        return(false);
    }
}

// checks if brakes are valid
static bool brakes_valid(uint32_t front, uint32_t rear) {
    if((front > BRAKE_MIN) && (front < BRAKE_MAX) && (rear > BRAKE_MIN) && (rear < BRAKE_MAX)) {
        return(true);
    } else {
        return(false);
    }
}

// VCU class constructor
VCU::VCU() {
    input.MC_EN = DIGITAL_LOW;
    input.MC_POST_FAULT = 0;
    input.MC_RUN_FAULT = 0;
    input.MC_CURRENT = 0;
    input.MC_VOLTAGE = 0;
    input.MC_SPEED = 0;
    input.THROTTLE_1 = 0;
    input.THROTTLE_2 = 0;
    input.LATCH_SENSE = DIGITAL_LOW;
    input.TS_READY_SENSE = DIGITAL_LOW;
    input.TS_RDY = DIGITAL_LOW;
    input.TS_LIVE = DIGITAL_LOW;
    input.CHARGER_CONNECTED = DIGITAL_LOW;
    input.BMS_VOLTAGE = 0;
    input.BMS_TEMPERATURE = 0;
    input.BMS_OK = DIGITAL_LOW;
    input.IMD_OK = DIGITAL_LOW;
    input.BSPD_OK = DIGITAL_LOW;
    input.CURRENT_SENSE = 0;
    input.BRAKE_FRONT = 0;
    input.BRAKE_REAR = 0;
    input.WHEEL_SPEED_FR = 0;
    input.WHEEL_SPEED_FL = DIGITAL_LOW;
    input.WHEEL_SPEED_RR = DIGITAL_LOW;
    input.WHEEL_SPEED_RL = DIGITAL_LOW;
 
    output.MC_TORQUE = 0;
    output.RTDS = DIGITAL_LOW;
    output.BRAKE_LIGHT = DIGITAL_LOW;
    output.AIR_POS = DIGITAL_LOW;
    output.AIR_NEG = DIGITAL_LOW;
    output.PUMP_EN = DIGITAL_LOW;
    output.DCDC_DISABLE = DIGITAL_LOW;
    output.PRECHARGE = DIGITAL_LOW;
    output.DISCHARGE = DIGITAL_LOW;
    output.PRECHARGE_FAILED = DIGITAL_LOW;
    output.REDUNDANT_1 = DIGITAL_LOW;
    output.REDUNDANT_2 = DIGITAL_LOW;
    output.FAN_EN = DIGITAL_LOW;
    output.FAN_PWM = DIGITAL_LOW;
    output.GENERAL_PURPOSE_1 = DIGITAL_LOW;
    output.GENERAL_PURPOSE_2 = DIGITAL_LOW;

    flag = false;
}

// VCU motor loop
void VCU::motor_loop() {
    static state_t state = STATE_STANDBY;
    static uint32_t timer = 0;
    uint8_t THROTTLE_AVG;
    uint8_t MC_POWER;

    THROTTLE_AVG = throttle_average(input.THROTTLE_1, input.THROTTLE_2);

    // TODO - rules T6.2.3 and EV2.4
    // TODO - calculate wheel speeds
    
    switch(state) {
        case STATE_STANDBY:
            output.MC_TORQUE = TORQUE_DIS;

            if((input.MC_EN == DIGITAL_HIGH) 
               && !input.MC_POST_FAULT 
               && !input.MC_RUN_FAULT 
               && (THROTTLE_AVG < THROTTLE_LOW_LIMIT) 
               && (output.AIR_POS == DIGITAL_HIGH) 
               && (output.AIR_NEG == DIGITAL_HIGH) 
               && brakes_active(input.BRAKE_FRONT, input.BRAKE_REAR) 
               && brakes_valid(input.BRAKE_FRONT, input.BRAKE_REAR)) {
                mc_clear_faults();
                state = STATE_DRIVING;
                timer = 0;
            }

            break;

        case STATE_DRIVING:
            if(timer > RTDS_TIME) {
                output.RTDS = DIGITAL_LOW;
                output.MC_TORQUE = THROTTLE_AVG / 10;
                MC_POWER = (input.MC_VOLTAGE * input.MC_CURRENT) / 1000;

                if(MC_POWER > POWER_LIMIT) {
                    output.MC_TORQUE -= (MC_POWER - POWER_LIMIT) * (MC_POWER - POWER_LIMIT);
                }
                
                if(output.MC_TORQUE > TORQUE_MAX) {
                    output.MC_TORQUE = TORQUE_MAX;
                } else if (output.MC_TORQUE < TORQUE_MIN) {
                    output.MC_TORQUE = TORQUE_MIN;
                }
            
                if((input.MC_EN == DIGITAL_LOW) 
                   || input.MC_POST_FAULT 
                   || input.MC_RUN_FAULT 
                   || (output.AIR_POS == DIGITAL_LOW) 
                   || (output.AIR_NEG == DIGITAL_LOW) 
                   || ((THROTTLE_AVG > THROTTLE_HIGH_LIMIT) && brakes_active(input.BRAKE_FRONT, input.BRAKE_REAR)) 
                   || !brakes_valid(input.BRAKE_FRONT, input.BRAKE_REAR)) {
                    state = STATE_STANDBY;
                }
            } else {
                output.RTDS = DIGITAL_HIGH;
                output.MC_TORQUE = TORQUE_MIN;
                timer++;
            }

            break;

        default:
            break;
    }

    if(brakes_active(input.BRAKE_FRONT, input.BRAKE_REAR)) {
        output.BRAKE_LIGHT = DIGITAL_HIGH;
    } else {
        output.BRAKE_LIGHT = DIGITAL_LOW;
    }
} 

// VCU shutdown loop
void VCU::shutdown_loop() {
    static state_t state = STATE_AIR_OFF;
    static uint32_t timer = 0;

    switch(state) {
        case STATE_AIR_OFF:
            output.AIR_POS = DIGITAL_LOW;
            output.AIR_NEG = DIGITAL_LOW;
            output.PUMP_EN = DIGITAL_LOW;
            output.DCDC_DISABLE = DIGITAL_HIGH;
            output.PRECHARGE = DIGITAL_LOW;
            output.DISCHARGE = DIGITAL_LOW;
            output.FAN_EN = DIGITAL_LOW;
            output.FAN_PWM = PWM_MIN;

            if(input.TS_READY_SENSE == DIGITAL_HIGH) {
                output.PRECHARGE_FAILED = DIGITAL_LOW;
                state = STATE_PRECHARGE;
                timer = 0;
            }

            break;

        case STATE_PRECHARGE:
            output.AIR_POS = DIGITAL_LOW;
            output.AIR_NEG = DIGITAL_HIGH;
            output.PUMP_EN = DIGITAL_LOW;
            output.DCDC_DISABLE = DIGITAL_HIGH;
            output.PRECHARGE = DIGITAL_HIGH;
            output.DISCHARGE = DIGITAL_HIGH;
            output.FAN_EN = DIGITAL_LOW;
            output.FAN_PWM = PWM_MIN;

            if(((timer > ALLOWED_PRECHARGE_TIME) && (input.MC_VOLTAGE < ((input.BMS_VOLTAGE * BATTERY_LIMIT) / 100)) && (input.CHARGER_CONNECTED == DIGITAL_LOW)) 
               || (input.TS_READY_SENSE == DIGITAL_LOW)) {
                output.PRECHARGE_FAILED = DIGITAL_HIGH;
                state = STATE_AIR_OFF;
            } else if((((timer > ALLOWED_PRECHARGE_TIME) && (input.MC_VOLTAGE > ((input.BMS_VOLTAGE * BATTERY_LIMIT) / 100))) || (input.CHARGER_CONNECTED == DIGITAL_HIGH)) 
                      && (input.TS_READY_SENSE == DIGITAL_HIGH)) {
                state = STATE_AIR_ON;
            } else {
                timer++;
            }

            break;

        case STATE_AIR_ON:
            output.AIR_POS = DIGITAL_HIGH;
            output.AIR_NEG = DIGITAL_HIGH;
            output.PUMP_EN = DIGITAL_LOW;
            output.DCDC_DISABLE = DIGITAL_HIGH;
            output.PRECHARGE = DIGITAL_LOW;
            output.DISCHARGE = DIGITAL_HIGH;
            output.FAN_EN = DIGITAL_LOW;
            output.FAN_PWM = PWM_MIN;

            if(input.TS_READY_SENSE == DIGITAL_LOW) {
                state = STATE_AIR_OFF;
            } else if((input.CHARGER_CONNECTED == DIGITAL_HIGH) 
                      && (input.TS_READY_SENSE == DIGITAL_HIGH)) {
                state = STATE_READY_TO_CHARGE;
            } else if((input.CHARGER_CONNECTED == DIGITAL_LOW) 
                      && (input.TS_READY_SENSE == DIGITAL_HIGH)) {
                state = STATE_READY_TO_DRIVE;
                timer = 0;
            }

            break;

        case STATE_READY_TO_CHARGE:
            output.AIR_POS = DIGITAL_HIGH;
            output.AIR_NEG = DIGITAL_HIGH;
            output.PUMP_EN = DIGITAL_LOW;
            output.DCDC_DISABLE = DIGITAL_HIGH;
            output.PRECHARGE = DIGITAL_LOW;
            output.DISCHARGE = DIGITAL_HIGH;
            output.FAN_EN = DIGITAL_LOW;
            output.FAN_PWM = PWM_MIN;

            if(input.TS_READY_SENSE == DIGITAL_LOW) {
                state = STATE_AIR_OFF;
            }

            break;

        case STATE_READY_TO_DRIVE:
            if(timer > MC_CHARGE_TIME) {
                output.AIR_POS = DIGITAL_HIGH;
                output.AIR_NEG = DIGITAL_HIGH;
                output.PUMP_EN = DIGITAL_HIGH;
                output.DCDC_DISABLE = DIGITAL_LOW;
                output.PRECHARGE = DIGITAL_LOW;
                output.DISCHARGE = DIGITAL_HIGH;
                output.FAN_EN = DIGITAL_HIGH;
                output.FAN_PWM = (FAN_GAIN * input.BMS_TEMPERATURE) + FAN_OFFSET;
            } else {
                timer++;
            }

            if(input.TS_READY_SENSE == DIGITAL_LOW) {
                state = STATE_AIR_OFF;
            }

            break;

        default:
            break;
    }
}

// VCU redundancy loop
void VCU::redundancy_loop() {
    uint8_t BSPD_OK;

    if(!((input.CURRENT_SENSE > CA) && brakes_active(input.BRAKE_FRONT, input.BRAKE_REAR)) 
       && brakes_valid(input.BRAKE_FRONT, input.BRAKE_REAR)) {
        BSPD_OK = DIGITAL_HIGH;
    } else {
        BSPD_OK = DIGITAL_LOW;
    }

    if((BSPD_OK == DIGITAL_HIGH) 
       && (input.IMD_OK == DIGITAL_HIGH) 
       && (input.BMS_OK == DIGITAL_HIGH)) {
        output.REDUNDANT_1 = DIGITAL_HIGH;
        output.REDUNDANT_2 = DIGITAL_HIGH;
    } else {
        output.REDUNDANT_1 = DIGITAL_LOW;
        output.REDUNDANT_2 = DIGITAL_LOW;
    }
}
