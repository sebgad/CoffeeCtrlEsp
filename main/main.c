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
#define SDA_0 23
#define SCL_0 22
#define CONV_RDY_PIN 14

// PWM defines
#define P_SSR_PWM 21
#define P_RED_LED_PWM 13
#define P_GRN_LED_PWM 27
#define P_BLU_LED_PWM 12
#define P_STAT_LED 33 // green status LED

// File system definitions
#define FORMAT_SPIFFS_IF_FAILED true
#define JSON_MEMORY 1600


// define timer related channels for PWM signals
#define RwmRedChannel 13 //  PWM channel. There are 16 channels from 0 to 15. Channel 13 is now Red-LED
#define RwmGrnChannel 14 //  PWM channel. There are 16 channels from 0 to 15. Channel 14 is now Green-LED
#define RwmBluChannel 15 //  PWM channel. There are 16 channels from 0 to 15. Channel 15 is now Blue-LED
#define PwmSsrChannel 0  //  PWM channel. There are 16 channels from 0 to 15. Channel 0 is now SSR-Controll

#define WDT_Timeout 15 // WatchDog Timeout in seconds

#include <stdio.h>
#include <sys/stat.h>
#include "esp_littlefs.h"
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// File paths for measurement and calibration file
const char* strMeasFilePath = "/data.csv";
bool bMeasFileLocked = false;
const char* strParamFilePath = "/params.json";
bool bParamFileLocked = false;
const char* strRecentLogFilePath = "/logfile_recent.txt";
const char* strLastLogFilePath = "/logfile_last.txt";
static char bufPrintLog[512];
const char* strUserLogLabel = "USER";

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

void app_main(void)
{
  // initialize LittleFS and load configuration files
  ESP_LOGI("LittleFS", "Initializing LittleFS");
  
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
  }

  // Link logging output to function
  esp_log_set_vprintf(&vprintf_into_FS);

}
