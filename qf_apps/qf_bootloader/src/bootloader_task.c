/*==========================================================
 * Copyright 2020 QuickLogic Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *==========================================================*/

/*==========================================================
*                                                          
*    File   : bootloader_task.c 
*    Purpose: This file has task to execute flash Update functionality.
*             
*                                                          
*=========================================================*/
#include "Fw_global_config.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"
#include "spi_flash.h"
#include "eoss3_hal_gpio.h"
#include "dbg_uart.h"


extern int load_m4app(void);
extern int load_usb_flasher(void);

#define MAX_BOOTLOADER_WAIT_MSEC  (5*1000)
#define MIN_USER_BTN_PRESS_WAIT_MSEC  (200)
#define USER_BUTTON_GPIO_NUM      (0) //PAD 6, GPIO is connected to User Button
#define BLUE_LED_GPIO_NUM         (4) //PAD 18, GPIO is connected to Blue LED
#define GREEN_LED_GPIO_NUM        (5) //PAD 21, GPIO is connected to Green LED
#define RED_LED_GPIO_NUM          (6) //PAD 22, GPIO is connected to Red LED

TaskHandle_t 	BLTaskHandle;
static int user_button_pressed = 0;
//static int user_btn_on_count = 0;
static int user_btn_on_start = 0;
void check_user_button(void)
{
  uint8_t gpio_value = 1;
  HAL_GPIO_Read(USER_BUTTON_GPIO_NUM, &gpio_value);
  if(gpio_value == 0)
  {
    //if first time get the time stamp
    if(user_btn_on_start == 0)
      user_btn_on_start = xTaskGetTickCount();
    if((xTaskGetTickCount() - user_btn_on_start) > MIN_USER_BTN_PRESS_WAIT_MSEC)
    {
      user_button_pressed = 1;
    }
  }
  return;
}
/* 
* This will set the red LED state to ON or OFF
*/
void set_boot_error_led(uint8_t value)
{
  //Use the red LED to indicate fatal erro
  HAL_GPIO_Write(RED_LED_GPIO_NUM, value);
}
/* 
* This will set the green LED state to ON or OFF
*/
void set_downloading_led(uint8_t value)
{
  //we use green LED toggle to indicate waiting
  //So, set to constant ON during Flashing using USB  
  //as an Acknowledgement of User button press
  HAL_GPIO_Write(GREEN_LED_GPIO_NUM, value);
}
/* 
* This will toggle green LED with time passed. 
* The state is maintained internal.
* Uses Tickcount to change the ON-OFF states.
*/
void toggle_downloading_led(int toggle_time_msec)
{
  static uint8_t led_state = 1;
  static int led_state_count = 0;
  
  //set the led state first to last state
  HAL_GPIO_Write(GREEN_LED_GPIO_NUM, led_state);
  
  //if the time exceeds the toggle time, change state and note time
  if((xTaskGetTickCount() - led_state_count) > toggle_time_msec)
  {
    led_state_count = xTaskGetTickCount();
    if(led_state == 0)
      led_state = 1;
    else
      led_state = 0;
  }
  return;
}
void set_waiting_led(uint8_t value)
{
  //we use blue LED toggle to indicate waiting
  //So, set to constant ON during Flashing using USB  
  //as an Acknowledgement of User button press
  HAL_GPIO_Write(BLUE_LED_GPIO_NUM, value);
}
/* 
* This will toggle green LED with time passed. 
* The state is maintained internal.
* Uses Tickcount to change the ON-OFF states.
*/
void toggle_waiting_led(int toggle_time_msec)
{
  static uint8_t led_state = 1;
  static int led_state_count = 0;
  
  //set the led state first to last state
  HAL_GPIO_Write(BLUE_LED_GPIO_NUM, led_state);
  
  //if the time exceeds the toggle time, change state and note time
  if((xTaskGetTickCount() - led_state_count) > toggle_time_msec)
  {
    led_state_count = xTaskGetTickCount();
    if(led_state == 0)
      led_state = 1;
    else
      led_state = 0;
  }
  return;
}
/* 
* This will toggle red LED with time passed. 
* The state is maintained internal.
* Uses Tickcount to change the ON-OFF states.
*/
void toggle_red_led(int toggle_time_msec)
{
  static uint8_t led_state = 1;
  static int led_state_count = 0;
  
  //set the led state first to last state
  HAL_GPIO_Write(RED_LED_GPIO_NUM, led_state);
  
  //if the time exceeds the toggle time, change state and note time
  if((xTaskGetTickCount() - led_state_count) > toggle_time_msec)
  {
    led_state_count = xTaskGetTickCount();
    if(led_state == 0)
      led_state = 1;
    else
      led_state = 0;
  }
  return;
}

/*
* This BootLoader Task will do 2 things.
* 1. Wait for 5sec for the User Button to be pressed.
*    If pressed, loads USB FPGA image and waits forever
*    for the Reset Button to be pressed
* 2. If the User Button is not pressed, M4 App is loaded.
*    If it fails to load M4 App, waits forever for 
*    User Button to be pressed to re-flash.
*/
static void BLTaskHandler(void *pvParameters)
{
    int wait_time_msec = 0;

	while(1)
	{
      // green led indicates waiting for button press
      toggle_waiting_led(200);
      
      //if User button is pressed load USB FPGA image
      check_user_button();
      if(user_button_pressed)
      {
        //Acknowledge User Button press
        dbg_str("User button pressed: switch to download mode\n");
        set_waiting_led(0);
        set_downloading_led(1);
        int error = load_usb_flasher(); //this should never return
        //if can not load USB FPGA image, it is fatal error. wait indefinitely
        while(1)
        {
          //set red LED for error and turn off green LED
          set_boot_error_led(1);
          set_downloading_led(0);
          dbg_str("ERROR loading USB FPGA Image. Please re-flash USB FPGA Image .. \n");
          dbg_str("Press Reset, then User Button and start Flash script .. \n\n");
          vTaskDelay(5*1000);
        }
      }
      //wait for a maximum of 3 secs before loading M4 App
      vTaskDelay(1);
      wait_time_msec++;
      if(wait_time_msec > MAX_BOOTLOADER_WAIT_MSEC)
      {
        dbg_str("User button not pressed: proceeding to load application\n");
        set_waiting_led(0);
        int error = load_m4app(); //this should never return
        //if the M4 image is corrupted it needs to be re-flashed. wait indefinitely
        while(1)
        {
          //set red LED for error and turn off green LED
          set_boot_error_led(1);
          set_downloading_led(0);
          dbg_str("ERROR loading M4 APP. Waiting for re-flashing .. \n");
          dbg_str("Press Reset then User Button and start Flash script .. \n\n");
          vTaskDelay(5*1000);
        }
      }
	}
}
/*!
* \fn void BL_Task_Init()
* \brief  Init function to create BootloaderTask to be called from main()
*
*/
void BL_Task_Init(void)
{
	xTaskCreate(BLTaskHandler, "BL_Task", 256, NULL, tskIDLE_PRIORITY + 4, &BLTaskHandle);
	configASSERT(BLTaskHandle);
	return;
}
