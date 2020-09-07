#include "ql_stdlib.h" 
#include "ql_trace.h" // This for printing log info to debug program
#include "ql_error.h" //return values for API functions are defined here 
#include "ql_system.h"// Message looping procedures are present here
#include "ql_gpio.h" //All APIs related to GPIO are present here 
#include "ql_timer.h" // All timer related APIs are present here
// Define GPIO pin
static Enum_PinName m_gpioPin = PINNAME_NETLIGHT;
// Define a timer and the handler
static u32 m_myTimerId = 2014;
static u32 m_nInterval = 5000; // 5000ms
static void Callback_OnTimer(u32 timerId, void* param);
/************************************************************************/
/* The main code begins here*/
/************************************************************************/
void proc_main_task(s32 taskId)
{
s32 ret; // to take return values of API functions
ST_MSG msg; //to get 
Ql_Debug_Trace("OpenCPU: LED Blinking by NETLIGHT\r\n");
// Initialize GPIO
ret = Ql_GPIO_Init(m_gpioPin, PINDIRECTION_OUT, PINLEVEL_LOW,
PINPULLSEL_PULLUP);
if (QL_RET_OK == ret)
{
Ql_Debug_Trace("<-- Initialize GPIO successfully -->\r\n");
}else{
Ql_Debug_Trace("<-- Fail to initialize GPIO pin, cause=%d -->\r\n", ret);
}
// Register and start timer
Ql_Timer_Register(m_myTimerId, Callback_OnTimer, NULL);
Ql_Timer_Start(m_myTimerId, m_nInterval, TRUE);
// START MESSAGE LOOP OF THIS TASK
while(TRUE)
{
Ql_OS_GetMessage(&msg);
switch(msg.message)
{
default:
break;
}
}
}
static void Callback_OnTimer(u32 timerId, void* param)
{
s32 gpioLvl = Ql_GPIO_GetLevel(m_gpioPin); // getting the pin level of the pin m_gpioPin
if (PINLEVEL_LOW == gpioLvl)
{
// Set GPIO to high level, then LED is light
Ql_GPIO_SetLevel(m_gpioPin, PINLEVEL_HIGH);
Ql_Debug_Trace("<-- Set GPIO to high level -->\r\n");
}else{
// Set GPIO to low level, then LED is dark
Ql_GPIO_SetLevel(m_gpioPin, PINLEVEL_LOW);
Ql_Debug_Trace("<-- Set GPIO to low level -->\r\n");
}
}
