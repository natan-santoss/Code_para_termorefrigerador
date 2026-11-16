#include <Arduino.h>

// FreeRTOS
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"
#include <Keypad.h>


// DHT22 sensor pins
const uint8_t DHT22_INPUT1_PIN = 34;
const uint8_t DHT22_INPUT2_PIN = 35;
const uint8_t DHT22_INPUT3_PIN = 14;
const uint8_t DHT22_INPUT4_PIN = 12;
const uint8_t DHT22_INPUT5_PIN = 13;

// LED pins
const uint8_t LED_PIN_1 = 27;
const uint8_t LED_PIN_2 = 26;
const uint8_t LED_PIN_3 = 25;
const uint8_t LED_PIN_4 = 33;
const uint8_t LED_PIN_5 = 32;

//Motor PWM pins
const uint8_t MOTOR_PWM_PIN_1 = 15;

// Keyboard 
const uint8_t KEYBOARD_ROWS = 4;
const uint8_t KEYBOARD_COLS = 4;

char keys[KEYBOARD_ROWS][KEYBOARD_COLS] = {
    {'1', '2', '3', 'A'},
    {'4', '5', '6', 'B'},
    {'7', '8', '9', 'C'},
    {'*', '0', '#', 'D'}
};

// Keyboard pin definitions
byte pinoLinhas[KEYBOARD_ROWS] = {2, 0, 4, 16}; 
byte pinoColunas[KEYBOARD_COLS] = {17, 5, 18, 19}; 
Keypad customKeypad = Keypad(makeKeymap(keys), pinoLinhas, pinoColunas, KEYBOARD_ROWS, KEYBOARD_COLS);

//Auxiliary variables
char teclasPressionada[3];
int teclas_armazenadas = 0;

// LCD pins
const uint8_t LCD_SCL = 22;
const uint8_t LCD_SDA = 21;

uint8_t temp_goal = 0;

void keypadTask(void *pvParameters);

void setup() {
    Serial.begin(9600);

    pinMode(DHT22_INPUT1_PIN, INPUT);
    pinMode(DHT22_INPUT2_PIN, INPUT);
    pinMode(DHT22_INPUT3_PIN, INPUT);
    pinMode(DHT22_INPUT4_PIN, INPUT);
    pinMode(DHT22_INPUT5_PIN, INPUT);

    pinMode(LED_PIN_1, OUTPUT);
    pinMode(LED_PIN_2, OUTPUT);  
    pinMode(LED_PIN_3, OUTPUT);
    pinMode(LED_PIN_4, OUTPUT);
    pinMode(LED_PIN_5, OUTPUT);

    pinMode(MOTOR_PWM_PIN_1, OUTPUT);

    pinMode(LCD_SCL, OUTPUT);
    pinMode(LCD_SDA, OUTPUT);



   // Used to pass out the created task's handle.
}

void loop() {



}


void keypadTask(void *pvParameters){

    while(true){
        char key = customKeypad.getKey();
            if (key != NO_KEY && teclas_armazenadas < 3) {
                teclasPressionada[teclas_armazenadas] = key;
                teclas_armazenadas++;

            }else{
                teclas_armazenadas = 0;
                    xTaskCreate(
                        updateGoalTempTask,
                        "updateGoalTempTask",
                        2048,
                        (void *)teclasPressionada,
                        1,
                        NULL
                        );

                    vTaskDelete(NULL);
            }

    }


}

void updateGoalTempTask(void *pvParameters){

    // checagem de valores

    char *teclas = (char *)pvParameters;
    int new_temp_goal = 0;

    if (std::is_integral<decltype(teclas[0] - '0')>::value && 
    std::is_integral<decltype(teclas[1] - '0')>::value && 
    teclas[2] == '#')
    {
        int new_temp_goal = (teclas[0] - '0') * 10 + (teclas[1] - '0');
        temp_goal =  new_temp_goal;
    }


    

}