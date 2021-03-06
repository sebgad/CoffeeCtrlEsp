/*********
 * 
 * coffee_ctrl_main
 * A script to control an espresso machine and display measurement values on a webserver of an ESP32
 * 
*********/
#define LOG_LEVEL ESP_LOG_VERBOSE
//#define LOG_LOCAL_LEVEL ESP_LOG_VERBOSE

// PIN definitions

// I2C pins
#define SDA_0 GPIO_NUM_23
#define SCL_0 GPIO_NUM_22
#define CONV_RDY_PIN GPIO_NUM_14

// PWM defines
#define P_SSR_PWM GPIO_NUM_21
#define P_RED_LED_PWM GPIO_NUM_13
#define P_GRN_LED_PWM GPIO_NUM_27
#define P_BLU_LED_PWM GPIO_NUM_12
#define P_STAT_LED GPIO_NUM_33 // green status LED
#define LEDC_DUTY_RES LEDC_TIMER_13_BIT // Set duty resolution to 13 bits

// File system definitions
#define FORMAT_SPIFFS_IF_FAILED true
#define JSON_MEMORY 1600


// define timer related channels for PWM signals
#define RwmRedChannel LEDC_CHANNEL_0 //  PWM channel. There are 16 channels from 0 to 15. Channel 13 is now Red-LED
#define RwmGrnChannel LEDC_CHANNEL_1 //  PWM channel. There are 16 channels from 0 to 15. Channel 14 is now Green-LED
#define RwmBluChannel LEDC_CHANNEL_2 //  PWM channel. There are 16 channels from 0 to 15. Channel 15 is now Blue-LED
#define PwmSsrChannel LEDC_CHANNEL_3  //  PWM channel. There are 16 channels from 0 to 15. Channel 0 is now SSR-Controll

#define WDT_Timeout 15 // WatchDog Timeout in seconds

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_CONNECT_BIT BIT1

#include <stdio.h>
#include <sys/stat.h>
#include <string>
#include "esp_littlefs.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "cJSON.h"
#include "driver/ledc.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include <time.h>
#include <sys/time.h>
#include "esp_sntp.h"
#include "mdns.h"

#include "lwip/err.h"
#include "lwip/sys.h"
#include "webserver.cpp"
#include "ADS111x.hpp"

static EventGroupHandle_t s_wifi_event_group;

// config structure for online calibration
struct config {
  std::string wifiSSID;
  std::string wifiPassword;
  float CtrlTarget;
  bool CtrlTimeFactor;
  bool CtrlPropActivate;
  float CtrlPropFactor;
  bool CtrlIntActivate;
  float CtrlIntFactor;
  bool CtrlDifActivate;
  float CtrlDifFactor;
  bool LowThresholdActivate;
  float LowThresholdValue;
  bool HighThresholdActivate;
  float HighTresholdValue;
  float LowLimitManipulation;
  float HighLimitManipulation;
  uint32_t SsrFreq;
  uint32_t PwmSsrResolution;
  uint32_t RwmRgbFreq;
  uint32_t RwmRgbResolution; 
  float RwmRgbGainFactorRed;
  float RwmRgbGainFactorGreen;
  float RwmRgbGainFactorBlue;
  float RwmRgbColorRedFactor;
  float RwmRgbColorGreenFactor;
  float RwmRgbColorBlueFactor;
  float RwmRgbColorOrangeFactor;
  float RwmRgbColorPurpleFactor;
  float RwmRgbColorWhiteFactor;
  bool SigFilterActive;
};

// File paths for measurement and calibration file
const char* strMeasFilePath = "/data.csv";
bool bMeasFileLocked = false;
const char* strParamFilePath = "/params.json";
bool bParamFileLocked = false;
const char* strRecentLogFilePath = "/logfile_recent.txt";
const char* strLastLogFilePath = "/logfile_last.txt";
static char bufPrintLog[512];
const char* strUserLogLabel = "USER";
static int iWifiRetryNum;

enum eLEDColor{
  LED_COLOR_RED,
  LED_COLOR_GREEN,
  LED_COLOR_BLUE,
  LED_COLOR_ORANGE,
  LED_COLOR_PURPLE,
  LED_COLOR_WHITE
};

// Initialize ADS1115 I2C connection
ADS1115 *objADS1115 = new ADS1115;

// define configuration struct
config objConfig;

int vprintf_into_FS(const char* szFormat, va_list args) {
	//write evaluated format string into buffer
	int i_ret = vsnprintf (bufPrintLog, sizeof(bufPrintLog), szFormat, args);

	//output is now in buffer. write to file.
	if(i_ret >= 0) {
    struct stat littlefs_stat_littlefs;

    if(stat(strRecentLogFilePath, &littlefs_stat_littlefs) == 0) {
      // Create logfile if it does not exist.
      FILE *obj_file = fopen(strRecentLogFilePath, "w");

      if(!obj_file) {
        ESP_LOGE("LittleFS", "Failed to create logfile for writing");
      }
      fclose(obj_file);
    }
    FILE *obj_file = fopen(strRecentLogFilePath, "a");
	  
    fprintf(obj_file, bufPrintLog);
    fflush(obj_file);
    fclose(obj_file);
	}
	return i_ret;
}


esp_err_t saveConfiguration(){
  /**
   * Save configuration to configuration file 
   */
  
  esp_err_t b_success;
  cJSON *json_doc  = NULL;
  cJSON *json_wifi = NULL;
  cJSON *json_pid = NULL;
  cJSON *json_ssr = NULL;
  cJSON *json_led = NULL;
  cJSON *json_signal = NULL;
  char *json_print = NULL;

  json_doc = cJSON_CreateObject();
  cJSON_AddItemToObject(json_doc, "Wifi", json_wifi = cJSON_CreateObject());
  cJSON_AddStringToObject(json_wifi, "wifiSSID", objConfig.wifiSSID.c_str());
  cJSON_AddStringToObject(json_wifi, "wifiPassword", objConfig.wifiPassword.c_str());
  cJSON_AddItemToObject(json_doc, "PID", json_pid = cJSON_CreateObject());
  cJSON_AddNumberToObject(json_pid, "CtrlTimeFactor", objConfig.CtrlTimeFactor);
  cJSON_AddNumberToObject(json_pid, "CtrlPropActivate", objConfig.CtrlPropActivate);
  cJSON_AddNumberToObject(json_pid, "CtrlPropFactor", objConfig.CtrlPropFactor);
  cJSON_AddNumberToObject(json_pid, "CtrlIntActivate", objConfig.CtrlIntActivate);
  cJSON_AddNumberToObject(json_pid, "CtrlIntFactor", objConfig.CtrlIntFactor);
  cJSON_AddNumberToObject(json_pid, "CtrlDifFactor", objConfig.CtrlDifFactor);
  cJSON_AddNumberToObject(json_pid, "CtrlTarget", objConfig.CtrlTarget);
  cJSON_AddNumberToObject(json_pid, "LowThresholdActivate", objConfig.LowThresholdActivate);
  cJSON_AddNumberToObject(json_pid, "LowThresholdValue", objConfig.LowThresholdValue);
  cJSON_AddNumberToObject(json_pid, "HighThresholdActivate", objConfig.HighThresholdActivate);
  cJSON_AddNumberToObject(json_pid, "HighTresholdValue", objConfig.HighTresholdValue);
  cJSON_AddNumberToObject(json_pid, "LowLimitManipulation", objConfig.LowLimitManipulation);
  cJSON_AddNumberToObject(json_pid, "HighLimitManipulation", objConfig.HighLimitManipulation);
  cJSON_AddItemToObject(json_doc, "SSR", json_ssr = cJSON_CreateObject());
  cJSON_AddNumberToObject(json_ssr, "SsrFreq", objConfig.SsrFreq);
  cJSON_AddNumberToObject(json_ssr, "PwmSsrResolution", objConfig.PwmSsrResolution);
  cJSON_AddItemToObject(json_doc, "LED", json_led = cJSON_CreateObject());
  cJSON_AddNumberToObject(json_led, "RwmRgbFreq", objConfig.RwmRgbFreq);
  cJSON_AddNumberToObject(json_led, "RwmRgbResolution", objConfig.RwmRgbResolution);
  cJSON_AddNumberToObject(json_led, "GainFactorRed", objConfig.RwmRgbGainFactorRed);
  cJSON_AddNumberToObject(json_led, "GainFactorGreen", objConfig.RwmRgbGainFactorGreen);
  cJSON_AddNumberToObject(json_led, "GainFactorBlue", objConfig.RwmRgbGainFactorBlue);
  cJSON_AddNumberToObject(json_led, "GainFactorColorRed", objConfig.RwmRgbColorRedFactor);
  cJSON_AddNumberToObject(json_led, "GainFactorColorGreen", objConfig.RwmRgbColorGreenFactor);
  cJSON_AddNumberToObject(json_led, "GainFactorColorBlue", objConfig.RwmRgbColorBlueFactor);
  cJSON_AddNumberToObject(json_led, "GainFactorColorOrange", objConfig.RwmRgbColorOrangeFactor);
  cJSON_AddNumberToObject(json_led, "GainFactorColorPurple", objConfig.RwmRgbColorPurpleFactor);
  cJSON_AddNumberToObject(json_led, "GainFactorColorWhite", objConfig.RwmRgbColorWhiteFactor);
  cJSON_AddItemToObject(json_doc, "Signal", json_signal = cJSON_CreateObject());
  cJSON_AddNumberToObject(json_signal, "SigFilterActive", objConfig.SigFilterActive);

  if (!bParamFileLocked){
    bParamFileLocked = true;
    FILE *obj_file = fopen(strParamFilePath, "w");
    json_print = cJSON_Print(json_doc);

    fprintf(obj_file, "\n%s\n", json_print);

    ESP_LOGI("LittleFS", "Configuration file updated successfully\n");
    b_success = ESP_OK;
    fclose(obj_file);
    bParamFileLocked = false;
    cJSON_free(json_doc);
    cJSON_free(json_print);
  } else {
    b_success = ESP_FAIL;
  }
  return b_success;
}


void resetConfiguration(bool b_safe_to_json){
  /**
   * init configuration to configuration file 
   * 
   * @param b_safe_to_json: Safe initial configuration to json file
   */
  
  objConfig.wifiSSID = "";
  objConfig.wifiPassword = "";
  objConfig.CtrlTimeFactor = true;
  objConfig.CtrlPropActivate = true;
  objConfig.CtrlPropFactor = 10.0;
  objConfig.CtrlIntActivate = true;
  objConfig.CtrlIntFactor = 350.0;
  objConfig.CtrlDifActivate = false;
  objConfig.CtrlDifFactor = 0.0;
  objConfig.CtrlTarget = 91.0;
  objConfig.LowThresholdActivate = false;
  objConfig.LowThresholdValue = 0.0;
  objConfig.HighThresholdActivate = false;
  objConfig.HighTresholdValue = 0.0;
  objConfig.LowLimitManipulation = 0;
  objConfig.HighLimitManipulation = 255;
  objConfig.SsrFreq = 15;
  objConfig.PwmSsrResolution = 8;
  objConfig.RwmRgbFreq = 500; // Hz - PWM frequency
  objConfig.RwmRgbResolution = 8; //  resulution of the DC; 0 => 0%; 255 = (2**8) => 100%.
  objConfig.RwmRgbGainFactorRed = 1.0;
  objConfig.RwmRgbGainFactorGreen = 1.0;
  objConfig.RwmRgbGainFactorBlue = 1.0;
  objConfig.RwmRgbColorRedFactor = 1.0;
  objConfig.RwmRgbColorGreenFactor = 1.0;
  objConfig.RwmRgbColorBlueFactor = 1.0;
  objConfig.RwmRgbColorOrangeFactor = 1.0;
  objConfig.RwmRgbColorPurpleFactor = 1.0;
  objConfig.RwmRgbColorWhiteFactor = 1.0;
  objConfig.SigFilterActive = true;

  if (b_safe_to_json){
    saveConfiguration();
  }
}

esp_err_t WriteJsonItem(cJSON * json_item, std::string * str_element){
  if (cJSON_IsString(json_item) && (json_item->valuestring != NULL)){
    *str_element=json_item->valuestring;
    return ESP_OK;
  } else {
    return ESP_FAIL;
  }
}

esp_err_t WriteJsonItem(cJSON * json_item, float * f_element){
  if (cJSON_IsNumber(json_item) && (json_item->valuedouble)){
    *f_element=json_item->valuedouble;
    return ESP_OK;
  } else {
    return ESP_FAIL;
  }
}


esp_err_t WriteJsonItem(cJSON * json_item, uint32_t * i_element){
  if (cJSON_IsNumber(json_item) && (json_item->valuedouble)){
    *i_element=json_item->valuedouble;
    return ESP_OK;
  } else {
    return ESP_FAIL;
  }
}


esp_err_t WriteJsonItem(cJSON * json_item, bool * b_element){
  if (cJSON_IsBool(json_item)){
    if (cJSON_IsTrue(json_item)) {
      *b_element=true;
      return ESP_OK;
    } else if (cJSON_IsFalse(json_item)){
      *b_element=false;
      return ESP_OK;
    } else {
      return ESP_FAIL;
    }
  } else {
    return ESP_FAIL;
  }
}


esp_err_t loadConfiguration(){
  /**
   * Load configuration from configuration file 
   */
  
  esp_err_t esp_res;
  size_t file_read_res;
  long i_file_size;
  char * char_file_buf;

  if (!bParamFileLocked){
    // file is not locked by another process
    bParamFileLocked = true;
    FILE *obj_param_file = fopen(strParamFilePath, "r");
    
    if (obj_param_file != NULL){
      // file access is successful
      
      // get file size
      fseek (obj_param_file , 0 , SEEK_END);
      i_file_size = ftell(obj_param_file);
      rewind(obj_param_file);

      // allocate memory for buffer
      char_file_buf = (char *)malloc(sizeof(char)*i_file_size);
      // read parameter file from file system
      file_read_res = fread(char_file_buf, 1, i_file_size, obj_param_file);

      // close file and release file lock
      fclose(obj_param_file);
      bParamFileLocked = false;

      if(file_read_res != i_file_size){
        // cannot read file content to buffer
        ESP_LOGE("LittleFS", "unable to read file to buffer, reset configuration and write to file.");
        resetConfiguration(true);
      } else {
        // file read to buffer successful
        cJSON *json_doc = cJSON_Parse(char_file_buf);

        if (json_doc){
          // Json file could be parsed successfully

          // Initialize configuration with default values
          resetConfiguration(false);
          bool b_set_default_values = false; 
          
          // get Wifi entries
          cJSON * json_wifi = cJSON_GetObjectItemCaseSensitive(json_doc, "Wifi");
          (WriteJsonItem(cJSON_GetObjectItemCaseSensitive(json_wifi,"wifiSSID"), &objConfig.wifiSSID)==ESP_FAIL)?(b_set_default_values=true): 0;
          (WriteJsonItem(cJSON_GetObjectItemCaseSensitive(json_wifi,"wifiPassword"), &objConfig.wifiPassword)==ESP_FAIL)?(b_set_default_values=true): 0;

          // get PID entries
          cJSON * json_pid = cJSON_GetObjectItemCaseSensitive(json_doc, "PID");
          (WriteJsonItem(cJSON_GetObjectItemCaseSensitive(json_pid,"CtrlTimeFactor"), &objConfig.CtrlTimeFactor)==ESP_FAIL)?(b_set_default_values=true): 0;
          (WriteJsonItem(cJSON_GetObjectItemCaseSensitive(json_pid,"CtrlPropActivate"), &objConfig.CtrlPropActivate)==ESP_FAIL)?(b_set_default_values=true): 0;
          (WriteJsonItem(cJSON_GetObjectItemCaseSensitive(json_pid,"CtrlIntActivate"), &objConfig.CtrlIntActivate)==ESP_FAIL)?(b_set_default_values=true): 0;
          (WriteJsonItem(cJSON_GetObjectItemCaseSensitive(json_pid,"CtrlIntFactor"), &objConfig.CtrlIntFactor)==ESP_FAIL)?(b_set_default_values=true): 0;
          (WriteJsonItem(cJSON_GetObjectItemCaseSensitive(json_pid,"CtrlDifActivate"), &objConfig.CtrlDifActivate)==ESP_FAIL)?(b_set_default_values=true): 0;
          (WriteJsonItem(cJSON_GetObjectItemCaseSensitive(json_pid,"CtrlDifFactor"), &objConfig.CtrlDifFactor)==ESP_FAIL)?(b_set_default_values=true): 0;
          (WriteJsonItem(cJSON_GetObjectItemCaseSensitive(json_pid,"CtrlTarget"), &objConfig.CtrlTarget)==ESP_FAIL)?(b_set_default_values=true): 0;
          (WriteJsonItem(cJSON_GetObjectItemCaseSensitive(json_pid,"LowThresholdActivate"), &objConfig.LowThresholdActivate)==ESP_FAIL)?(b_set_default_values=true): 0;
          (WriteJsonItem(cJSON_GetObjectItemCaseSensitive(json_pid,"LowThresholdValue"), &objConfig.LowThresholdValue)==ESP_FAIL)?(b_set_default_values=true): 0;
          (WriteJsonItem(cJSON_GetObjectItemCaseSensitive(json_pid,"HighThresholdActivate"), &objConfig.HighThresholdActivate)==ESP_FAIL)?(b_set_default_values=true): 0;
          (WriteJsonItem(cJSON_GetObjectItemCaseSensitive(json_pid,"HighTresholdValue"), &objConfig.HighTresholdValue)==ESP_FAIL)?(b_set_default_values=true): 0;
          (WriteJsonItem(cJSON_GetObjectItemCaseSensitive(json_pid,"LowLimitManipulation"), &objConfig.LowLimitManipulation)==ESP_FAIL)?(b_set_default_values=true): 0;
          (WriteJsonItem(cJSON_GetObjectItemCaseSensitive(json_pid,"HighLimitManipulation"), &objConfig.HighLimitManipulation)==ESP_FAIL)?(b_set_default_values=true): 0;

          // get SSR entries
          cJSON * json_ssr = cJSON_GetObjectItemCaseSensitive(json_doc, "SSR");
          (WriteJsonItem(cJSON_GetObjectItemCaseSensitive(json_ssr,"SsrFreq"), &objConfig.SsrFreq)==ESP_FAIL)?(b_set_default_values=true): 0;
          (WriteJsonItem(cJSON_GetObjectItemCaseSensitive(json_ssr,"PwmSsrResolution"), &objConfig.PwmSsrResolution)==ESP_FAIL)?(b_set_default_values=true): 0;

          // get LED entries
          cJSON * json_led = cJSON_GetObjectItemCaseSensitive(json_doc, "LED");
          (WriteJsonItem(cJSON_GetObjectItemCaseSensitive(json_led,"RwmRgbFreq"), &objConfig.RwmRgbFreq)==ESP_FAIL)?(b_set_default_values=true): 0;
          (WriteJsonItem(cJSON_GetObjectItemCaseSensitive(json_led,"RwmRgbResolution"), &objConfig.RwmRgbResolution)==ESP_FAIL)?(b_set_default_values=true): 0;
          (WriteJsonItem(cJSON_GetObjectItemCaseSensitive(json_led,"GainFactorRed"), &objConfig.RwmRgbGainFactorRed)==ESP_FAIL)?(b_set_default_values=true): 0;
          (WriteJsonItem(cJSON_GetObjectItemCaseSensitive(json_led,"GainFactorGreen"), &objConfig.RwmRgbGainFactorGreen)==ESP_FAIL)?(b_set_default_values=true): 0;
          (WriteJsonItem(cJSON_GetObjectItemCaseSensitive(json_led,"GainFactorBlue"), &objConfig.RwmRgbGainFactorBlue)==ESP_FAIL)?(b_set_default_values=true): 0;
          (WriteJsonItem(cJSON_GetObjectItemCaseSensitive(json_led,"GainFactorColorRed"), &objConfig.RwmRgbColorRedFactor)==ESP_FAIL)?(b_set_default_values=true): 0;
          (WriteJsonItem(cJSON_GetObjectItemCaseSensitive(json_led,"GainFactorColorGreen"), &objConfig.RwmRgbColorGreenFactor)==ESP_FAIL)?(b_set_default_values=true): 0;
          (WriteJsonItem(cJSON_GetObjectItemCaseSensitive(json_led,"GainFactorColorBlue"), &objConfig.RwmRgbColorBlueFactor)==ESP_FAIL)?(b_set_default_values=true): 0;
          (WriteJsonItem(cJSON_GetObjectItemCaseSensitive(json_led,"GainFactorColorOrange"), &objConfig.RwmRgbColorOrangeFactor)==ESP_FAIL)?(b_set_default_values=true): 0;
          (WriteJsonItem(cJSON_GetObjectItemCaseSensitive(json_led,"GainFactorColorPurple"), &objConfig.RwmRgbColorPurpleFactor)==ESP_FAIL)?(b_set_default_values=true): 0;
          (WriteJsonItem(cJSON_GetObjectItemCaseSensitive(json_led,"GainFactorColorWhite"), &objConfig.RwmRgbColorWhiteFactor)==ESP_FAIL)?(b_set_default_values=true): 0;

          // get signal entries
          cJSON * json_signal = cJSON_GetObjectItemCaseSensitive(json_doc, "Signal");
          (WriteJsonItem(cJSON_GetObjectItemCaseSensitive(json_signal,"SigFilterActive"), &objConfig.SigFilterActive)==ESP_FAIL)?(b_set_default_values=true): 0;

          // release memory of hole JSON object
          cJSON_free(json_doc);

          if (b_set_default_values){
            // default values are set to Json object -> write it back to file.
            if (!saveConfiguration()){
              ESP_LOGW("JSON", "Cannot write back to JSON file.\n");
            }
          }
        } else {
          // parse error in JSON file
          const char *error_ptr = cJSON_GetErrorPtr();
          if (error_ptr != NULL) {
            ESP_LOGE("JSON", "JSON deserializion error of paramter file before: %s\n", error_ptr);
          }
          ESP_LOGI("JSON", "Reset configuration and write to JSON file.");
          resetConfiguration(true);
        }
      }
      // release memory of buffer
      free(char_file_buf);
    } else {
      // file open not possible
      ESP_LOGE("LittleFS", "Cannot open parameter file.\n");
      resetConfiguration(true);
    }
  } else {
    // file is locked by another process
    ESP_LOGE("JSON", "Parameter file is locked by another process. Reset configuration without writing to file.");
    resetConfiguration(false);
    esp_res = ESP_FAIL;
  }

  return esp_res;
}


void setColor(int i_color, bool b_gain_active) {
  /** Function to output a RGB value to the LED
   * examples: (from https://www.rapidtables.com/web/color/RGB_Color.html)
   * setColor(255, 0, 0);     // Red 
   * setColor(0, 255, 0);     // Green 
   * setColor(0, 0, 255);     // Blue 
   * setColor(255, 255, 255); // White 
   * setColor(170, 0, 255);   // Purple 
   * setColor(255,10,0);      // Orange 
   * setColor(255,20,0);      // Yellow 
   *
   * @param i_color        color to set for the RGB LED
   * @param b_gain_active  gain factor is used for displaying LED
   */

  float f_red_value = 0.0F;
  float f_green_value = 0.0F;
  float f_blue_value = 0.0F;
  float f_max_resolution = (float)(1<<13)-1.0F;

  if (i_color == LED_COLOR_RED){
    f_red_value = 255.F * objConfig.RwmRgbColorRedFactor;
  } else if (i_color == LED_COLOR_BLUE) {
    f_blue_value = 255.F * objConfig.RwmRgbColorBlueFactor;
  } else if (i_color == LED_COLOR_GREEN) {
    f_green_value = 255.F * objConfig.RwmRgbColorGreenFactor;
  } else if (i_color == LED_COLOR_ORANGE) {
    f_red_value = 255.F * objConfig.RwmRgbColorOrangeFactor;
    f_green_value = 10.F * objConfig.RwmRgbColorOrangeFactor;
  } else if (i_color == LED_COLOR_PURPLE) {
    f_red_value = 170.F * objConfig.RwmRgbColorPurpleFactor;
    f_blue_value = 255.F * objConfig.RwmRgbColorPurpleFactor;
  } else if (i_color == LED_COLOR_WHITE) {
    f_red_value = 100.F * objConfig.RwmRgbColorWhiteFactor;
    f_green_value = 100.F * objConfig.RwmRgbColorWhiteFactor;
    f_blue_value = 100.F * objConfig.RwmRgbColorWhiteFactor;
  }

  if (b_gain_active){
    // gain is active
    f_red_value *= objConfig.RwmRgbGainFactorRed;
    f_green_value *= objConfig.RwmRgbGainFactorGreen;
    f_blue_value *= objConfig.RwmRgbGainFactorBlue;
  }

  // Value saturation check
  f_red_value = (f_red_value>f_max_resolution) ? f_max_resolution : f_red_value;
  f_red_value = (f_red_value<0.F) ? 0.F: f_red_value;

  f_green_value = (f_green_value>f_max_resolution) ? f_max_resolution : f_green_value;
  f_green_value = (f_green_value<0.F) ? 0.F : f_green_value;

  f_blue_value = (f_blue_value>f_max_resolution) ? f_max_resolution : f_blue_value;
  f_blue_value = (f_blue_value<0.F) ? 0.F : f_blue_value;

  ledc_set_fade_with_time(LEDC_HIGH_SPEED_MODE, RwmRedChannel, (int)f_red_value, 1000);
  ledc_set_fade_with_time(LEDC_HIGH_SPEED_MODE, RwmGrnChannel, (int)f_green_value, 1000);
  ledc_set_fade_with_time(LEDC_HIGH_SPEED_MODE, RwmBluChannel, (int)f_blue_value, 1000);

  ledc_fade_start(LEDC_HIGH_SPEED_MODE, RwmRedChannel, LEDC_FADE_NO_WAIT);
  ledc_fade_start(LEDC_HIGH_SPEED_MODE, RwmGrnChannel, LEDC_FADE_NO_WAIT);
  ledc_fade_start(LEDC_HIGH_SPEED_MODE, RwmBluChannel, LEDC_FADE_NO_WAIT);

}// setColor


void configLED(){
  /**
   * @brief Method to configure LED functionality
   * 
   */
  
  // Configure LED timer
  ledc_timer_config_t conf_ledc_timer;
  conf_ledc_timer.speed_mode       = LEDC_HIGH_SPEED_MODE;
  conf_ledc_timer.timer_num        = LEDC_TIMER_0;
  conf_ledc_timer.duty_resolution  = LEDC_DUTY_RES;
  conf_ledc_timer.freq_hz          = objConfig.RwmRgbFreq;
  conf_ledc_timer.clk_cfg          = LEDC_AUTO_CLK;

  ledc_timer_config(&conf_ledc_timer);

  // configure RGB LED
  ledc_channel_config_t ledc_channel[3];

  // define config for red LED
  ledc_channel[0].channel  = RwmRedChannel;
  ledc_channel[0].duty = 0;
  ledc_channel[0].gpio_num = P_RED_LED_PWM;
  ledc_channel[0].speed_mode = LEDC_HIGH_SPEED_MODE;
  ledc_channel[0].hpoint = 0;
  ledc_channel[0].timer_sel = LEDC_TIMER_0;
  ledc_channel[0].flags.output_invert = 0;

  // define config for green led
  ledc_channel[1].channel  = RwmGrnChannel;
  ledc_channel[1].duty = 0;
  ledc_channel[1].gpio_num = P_GRN_LED_PWM;
  ledc_channel[1].speed_mode = LEDC_HIGH_SPEED_MODE;
  ledc_channel[1].hpoint = 0;
  ledc_channel[1].timer_sel = LEDC_TIMER_0;
  ledc_channel[1].flags.output_invert = 0;
  
  // define config for blue led
  ledc_channel[2].channel  = RwmBluChannel;
  ledc_channel[2].duty = 0;
  ledc_channel[2].gpio_num = P_BLU_LED_PWM;
  ledc_channel[2].speed_mode = LEDC_HIGH_SPEED_MODE;
  ledc_channel[2].hpoint = 0;
  ledc_channel[2].timer_sel = LEDC_TIMER_0;
  ledc_channel[2].flags.output_invert = 0;

  // Set LED Controller with previously prepared configuration
  for (int i_ch = 0; i_ch < 3; i_ch++) {
    ledc_channel_config(&ledc_channel[i_ch]);
  }

  // initialize fade service
   ledc_fade_func_install(0);

  // set green status LED to on
  gpio_set_direction(P_STAT_LED, GPIO_MODE_OUTPUT);
  gpio_set_level(P_STAT_LED, 1);
}


static void event_handler(void * arg, esp_event_base_t event_base, int32_t event_id, void * event_data){
  if (event_base == WIFI_EVENT){
    if (event_id == WIFI_EVENT_STA_START){
      esp_wifi_connect();
    } else if (event_id == WIFI_EVENT_STA_DISCONNECTED){
      if (iWifiRetryNum < 3){
        esp_wifi_connect();
        iWifiRetryNum++;
        ESP_LOGI("Wifi", "retry to connect to the AP");
      } else {
        xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_CONNECT_BIT);
      }
      ESP_LOGI("Wifi", "connect to AP fail.");
    } else if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI("Wifi", "station " MACSTR " join, AID=%d", MAC2STR(event->mac), event->aid);
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI("Wifi", "station " MACSTR " leave, AID=%d", MAC2STR(event->mac), event->aid);
    }
  } else if (event_base == IP_EVENT ) {
    if(event_id == IP_EVENT_STA_GOT_IP){
      ip_event_got_ip_t * event = (ip_event_got_ip_t*) event_data;
      ESP_LOGI("Wifi", "Device got IP address: " IPSTR, IP2STR(&event->ip_info.ip));
      iWifiRetryNum = 0;
      xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
  }
}


esp_err_t connectWiFi(const int i_total_fail = 3, const int i_timout_attemp = 1000){
  /**
   * Try to connect to WiFi Accesspoint based on the given information in the header file WiFiAccess.h.
   * A defined number of connection is performed.
   * @param 
   *    i_timout_attemp:     Total amount of connection attemps
   *    i_waiting_time:   Waiting time between connection attemps
   * 
   * @return 
   *    b_status:         true if connection is successfull
   */
  
  esp_err_t b_successful = ESP_FAIL;
    
  s_wifi_event_group = xEventGroupCreate();

  ESP_ERROR_CHECK(esp_netif_init());

  ESP_ERROR_CHECK(esp_event_loop_create_default());
  esp_netif_create_default_wifi_sta();

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  esp_event_handler_instance_t instance_any_id;
  esp_event_handler_instance_t instance_got_ip;
  ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, &instance_any_id));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, &instance_got_ip));

  wifi_config_t wifi_config;
  // initialize wifi_config with zeros
  memset(&wifi_config, 0, sizeof(wifi_config));
  strcpy((char*)wifi_config.sta.ssid, objConfig.wifiSSID.c_str());
  strcpy((char*)wifi_config.sta.password, objConfig.wifiPassword.c_str());
  wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_WPA3_PSK;
    
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    
  ESP_ERROR_CHECK(esp_wifi_start());

  /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
  * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
  EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_CONNECT_BIT, pdFALSE, pdFALSE, portMAX_DELAY);

  /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
  * happened. */
  if (bits & WIFI_CONNECTED_BIT) {
    ESP_LOGI("Wifi",  "Connection successful");
    b_successful = ESP_OK;
    wifi_ap_record_t ap;
    esp_wifi_sta_get_ap_info(&ap);
    int i_db_m = ap.rssi;
    int i_db_perc = 0;

    if (i_db_m>=-50) {
      i_db_perc = 100;
    } else if (i_db_m<=-100) {
      i_db_perc = 0;
    } else {
      i_db_perc = 2*(i_db_m+100);
    }

    ESP_LOGI("Wifi", "Signal strength: %d dB -> %d %%\n", i_db_m, i_db_perc);
  } else if (bits & WIFI_FAIL_CONNECT_BIT) {
      ESP_ERROR_CHECK(esp_wifi_stop());
      ESP_LOGI("Wifi",  "Connection unsuccessful. Creating Soft AP instead.");
      wifi_config_t wifi_config;
      memset(&wifi_config, 0, sizeof(wifi_config));
      strcpy((char*)wifi_config.ap.ssid, "CoffeeCtrl");
      wifi_config.ap.ssid_len = strlen("CoffeeCtrl");
      wifi_config.ap.channel = 10;
      strcpy((char*)wifi_config.ap.password, "");
      wifi_config.ap.max_connection = 5;
      //wifi_config.ap.pmf_cfg.required = false;
      wifi_config.ap.authmode = WIFI_AUTH_OPEN;

      ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
      ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
      ESP_ERROR_CHECK(esp_wifi_start());
  } else {
      ESP_LOGE("Wifi", "Unexpected event during WiFi configuration apears");
  }

  return b_successful;
} // connectWiFi


esp_err_t configADS1115(){
  /**
   * Configure Analog digital converter ADS1115
   */
  
  // Initialize I2c on defined pins with default adress
  if (!objADS1115->begin(SDA_0, SCL_0, ADS1115_I2CADD_DEFAULT)){
    esp_log_write(ESP_LOG_ERROR, strUserLogLabel, "Failed to initialize I2C sensor connection, stop working.\n");
    return ESP_FAIL;
  }

  // Set Signal Filter Status
  if(objConfig.SigFilterActive){
    objADS1115->activateFilter();
  }

  // set Comparator Polarity to active high
  objADS1115->setCompPolarity(ADS1115_CMP_POL_ACTIVE_HIGH);

  // set differential voltage: A0-A1
  objADS1115->setMux(ADS1115_MUX_AIN0_AIN1);

  // set data rate (samples per second)
  objADS1115->setRate(ADS1115_RATE_8);


  #ifdef Pt1000_CONV_LINEAR
    objADS1115->setPhysConv(fPt1000LinCoeffX1, fPt1000LinCoeffX0);
    esp_log_write(ESP_LOG_INFO, strUserLogLabel, "Applying linear regression function for Pt1000 conversion: %.4f * Umess + %.4f\n", fPt1000LinCoeffX1, fPt1000LinCoeffX0);
  #endif
  #ifdef Pt1000_CONV_SQUARE
    objADS1115->setPhysConv(fPt1000SquareCoeffX2, fPt1000SquareCoeffX1, fPt1000SquareCoeffX0);
    esp_log_write(ESP_LOG_INFO, strUserLogLabel, "Applying square regression function for Pt1000 conversion: \
                               %.4f * Umess^2 + %.4f * Umess + %.4f\n", fPt1000SquareCoeffX2, fPt1000SquareCoeffX1, fPt1000SquareCoeffX0)
  #endif
  #ifdef Pt1000_CONV_LOOK_UP_TABLE
    const size_t size_1d_map = sizeof(arrPt1000LookUpTbl) / sizeof(arrPt1000LookUpTbl[0]);
    objADS1115->setPhysConv(arrPt1000LookUpTbl, size_1d_map);
    esp_log_write(ESP_LOG_INFO, strUserLogLabel, "Applying lookup table for Pt1000 conversion:\n");
    for(int i_row=0; i_row<size_1d_map; i_row++){
      esp_log_write(ESP_LOG_INFO, strUserLogLabel,  "%.4f    %.4f\n", arrPt1000LookUpTbl[i_row][0], arrPt1000LookUpTbl[i_row][1]);
    }
  #endif

  // set gain amplifier
  objADS1115->setPGA(ADS1115_PGA_0P256);

  // set latching mode
  objADS1115->setCompLatchingMode(ADS1115_CMP_LAT_ACTIVE);

  // assert after one conversion
  objADS1115->setPinRdyMode(ADS1115_CONV_READY_ACTIVE, ADS1115_CMP_QUE_ASSERT_1_CONV);

  // set to continues conversion method
  objADS1115->setOpMode(ADS1115_MODE_CONTINUOUS);
  
  objADS1115->printConfigReg();
  return ESP_OK;
}


extern "C" {
  void app_main();
}

void app_main(void)
{
  // initialize LittleFS and load configuration files
  ESP_LOGI("LittleFS", "Initializing LittleFS and routing output to file.");
  
  // Configure little fs driver
  esp_vfs_littlefs_conf_t esp_little_fs_conf;
  esp_little_fs_conf.base_path = "/littlefs";
  esp_little_fs_conf.partition_label = "littlefs";
  esp_little_fs_conf.format_if_mount_failed = true;
  esp_little_fs_conf.dont_mount = false;
  
  esp_err_t esp_ret = esp_vfs_littlefs_register(&esp_little_fs_conf);

  if (!(esp_ret==ESP_OK)){
    // Initialisation of Little fs does not succeed.
    if (esp_ret == ESP_FAIL) {
      ESP_LOGE("LittleFS", "Failed to mount or format filesystem");
    } else if (esp_ret == ESP_ERR_NOT_FOUND) {
      ESP_LOGE("LittleFS", "Failed to find LittleFS partition");
    } else {
      ESP_LOGE("LittleFS", "Failed to initialize LittleFS (%s)", esp_err_to_name(esp_ret));
    }
    // restart ESP on filesystem error
    esp_restart();
  } else {
    // Mount of LittleFS file system successfully

    // Link logging output to function
    esp_log_set_vprintf(&vprintf_into_FS);
    // Initialization successfull, create csv file

    ESP_LOGI("LittleFS",  "\n\n-----------------------------------Starting Logging.\n");
    ESP_LOGI("LittleFS", "LittleFS mount successfully.\n");
    
    const char * char_part_label = "littlefs";
    size_t i_total_bytes;
    size_t i_used_bytes;
    esp_littlefs_info(char_part_label, &i_total_bytes, &i_used_bytes);

    ESP_LOGI("LittleFS", "File system info:\n");
    ESP_LOGI("LittleFS", "Total space on LittleFS: %d bytes\n", i_total_bytes);
    ESP_LOGI("LittleFS", "Total space used on LittleFS: %d bytes\n", i_used_bytes);
  }
  
  unsigned int i_reset_reason = esp_reset_reason();
  ESP_LOGI("ESP", "Last reset reason: %d\n", i_reset_reason);

  // initialize configuration before load json file
  resetConfiguration(false);

  // load configuration from file in eeprom
  if (!loadConfiguration()) {
    ESP_LOGW("LittleFS", "Parameter file is locked on startup. Please reset to factory settings.\n");
  }

  configLED();
  setColor(LED_COLOR_WHITE, true); // White

  char char_timestamp[64];

  // Connect to wifi and create time stamp if device is Online
  if (connectWiFi(3, 3000) == ESP_OK){
    // Device is online, connecting to ntp server

    // set time zone to western europe / berlin
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();

    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    // accept ntp server of dhcp if available
    sntp_servermode_dhcp(1);

    sntp_init();

    struct tm obj_timeinfo = {};
    time_t obj_now = 0;
    int i_retry_ntp = 0;
    const int i_max_retry_ntp = 15;

    while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && ++i_retry_ntp < i_max_retry_ntp) {
      // try to connect to ntp server
      ESP_LOGI("time", "Waiting for system time to be set... (%d/%d)", i_retry_ntp, i_max_retry_ntp);
      vTaskDelay(2000 / portTICK_PERIOD_MS);
    }

    if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED){
      // time is synchronized
      time(&obj_now);
      localtime_r(&obj_now, &obj_timeinfo);
      
      strftime(char_timestamp, sizeof(char_timestamp), "%c", &obj_timeinfo);
      ESP_LOGI("time", "The current time is %s", char_timestamp);
    } else {
      // no time sync possible
      ESP_LOGI("time", "Failed to obtain time stamp online");
    }

    setColor(LED_COLOR_PURPLE, false);

    //initialize mDNS service
    esp_err_t err = mdns_init();
    if (err) {
        printf("MDNS Init failed: %d\n", err);
    }
    
    //set hostname
    mdns_hostname_set("coffeectrl");
    //set default instance
    mdns_instance_name_set("Coffee Ctrl for Rancilio Silvia");

    start_web_server("/littlefs");
  }

  // configure ADS1115
  if(configADS1115() == ESP_FAIL) {
    // TODO add diagnosis when ADS1115 is not connected
    ESP_LOGE("ADS1115", "ADS1115 configuration not successful.\n");
  }

  // Create measurement file header
  FILE *obj_file = fopen(strMeasFilePath, "w");  
  uint16_t i_config_reg = objADS1115->getRegisterValue(ADS1115_CONFIG_REG);
  uint16_t i_low_reg = objADS1115->getRegisterValue(ADS1115_LOW_THRESH_REG);
  uint16_t i_high_reg = objADS1115->getRegisterValue(ADS1115_HIGH_THRESH_REG);
	  
  fprintf(obj_file, "Measurement File created on %s\n", char_timestamp);
  fprintf(obj_file, "ADS1115 register settings\n");
  fprintf(obj_file, "ADS1115 register settings\n");
  fprintf(obj_file, "Config register: %d\n", i_config_reg);
  fprintf(obj_file, "Low threshold register: %d\n", i_low_reg);
  fprintf(obj_file, "High threshold register: %d\n\n", i_high_reg);
  fprintf(obj_file, "Time,Temperature,TargetPWM,Buffer,InterruptCountAlertReady\n");

  fflush(obj_file);
  fclose(obj_file);
};
