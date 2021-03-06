// Code based on Retian (version 1.0), Implementation changed and comments translated into english
// https://arduino-projekte.webnode.at/meine-libraries/adc-ads1115/

#ifndef ADS1115_h
#define ADS1115_h

#include "driver/i2c.h"

#define ADS1115_I2C_PORT_NUM I2C_NUM_1 // I2C port number

#define ADS1115_I2CADD_DEFAULT  0x48 //ADDR-Pin on GND
#define ADS1115_I2CADD_ADDR_VDD 0x49 //ADDR-Pin on VDD
#define ADS1115_I2CADD_ADDR_SDA 0x4A //ADDR-Pin on SDA
#define ADS1115_I2CADD_ADDR_SCL 0x4B //ADDR-Pin on SCL

//ADS1115 register
#define ADS1115_CONVERSION_REG 0x00 // measurement is stored in this register
#define ADS1115_CONFIG_REG 0x01 // configuration register of the ADS1115
#define ADS1115_LOW_THRESH_REG 0x02 // Low threshold register of the ADS1115
#define ADS1115_HIGH_THRESH_REG 0x03 // High threshold register of the ADS1115

// bit number of ADS1115_CONFIG_REG 
#define ADS1115_CMP_QUE0 0x00
#define ADS1115_CMP_QUE1 0x01
#define ADS1115_CMP_LAT 0x02
#define ADS1115_CMP_POL 0x03
#define ADS1115_CMP_MDE 0x04
#define ADS1115_DR0  0x05
#define ADS1115_DR1  0x06
#define ADS1115_DR2  0x07
#define ADS1115_MODE 0x08
#define ADS1115_PGA0 0x09
#define ADS1115_PGA1 0x0A
#define ADS1115_PGA2 0x0B
#define ADS1115_MUX0 0x0C
#define ADS1115_MUX1 0x0D
#define ADS1115_MUX2 0x0E
#define ADS1115_OS   0x0F

//ADS1115 input multiplexer settings
#define ADS1115_MUX_AIN0_AIN1 0b000 // default
#define ADS1115_MUX_AIN0_AIN3 0b001
#define ADS1115_MUX_AIN1_AIN3 0b010
#define ADS1115_MUX_AIN2_AIN3 0b011
#define ADS1115_MUX_AIN0_GND  0b100
#define ADS1115_MUX_AIN1_GND  0b101
#define ADS1115_MUX_AIN2_GND  0b110
#define ADS1115_MUX_AIN3_GND  0b111

// Programmable Gain Amplifier
#define ADS1115_PGA_6P144 0b000
#define ADS1115_PGA_4P096 0b001
#define ADS1115_PGA_2P048 0b010 // default
#define ADS1115_PGA_1P024 0b011
#define ADS1115_PGA_0P512 0b100
#define ADS1115_PGA_0P256 0b101

// measurement mode
#define ADS1115_MODE_CONTINUOUS 0
#define ADS1115_MODE_SINGLESHOT 1 // default

// data rate (SPS - samples per second)
#define ADS1115_RATE_8   0b000 
#define ADS1115_RATE_16  0b001
#define ADS1115_RATE_32  0b010
#define ADS1115_RATE_64  0b011
#define ADS1115_RATE_128 0b100 // default
#define ADS1115_RATE_250 0b101
#define ADS1115_RATE_475 0b110
#define ADS1115_RATE_860 0b111

// bit significance in mV depending on chosen PGA 
#define ADS1115_LSB_6P144  0.000187500
#define ADS1115_LSB_4P096  0.000125000
#define ADS1115_LSB_2P048  0.000062500 // default
#define ADS1115_LSB_1P024  0.000031250
#define ADS1115_LSB_0P512  0.000015625
#define ADS1115_LSB_0P256  0.000007812

// Comparator mode
#define ADS1115_CMP_MODE_TRADITIONAL  0b000 // default
#define ADS1115_CMP_MODE_WINDOW       0b001 

// Comparator polarity
#define ADS1115_CMP_POL_ACTIVE_LOW  0b000 // default
#define ADS1115_CMP_POL_ACTIVE_HIGH 0b001

// Latching comparator
#define ADS1115_CMP_LAT_NOT_ACTIVE 0b000 // default
#define ADS1115_CMP_LAT_ACTIVE 0b001

// Comparator QUEUE
#define ADS1115_CMP_QUE_ASSERT_1_CONV 0b000
#define ADS1115_CMP_QUE_ASSERT_2_CONV 0b001
#define ADS1115_CMP_QUE_ASSERT_4_CONV 0b010
#define ADS1115_CMP_DISABLE 0b011 // default

// Conversion Ready PIN
#define ADS1115_CONV_READY_NOT_ACTIVE 0b000
#define ADS1115_CONV_READY_ACTIVE 0b001

#define ADS1115_CONV_BUF_SIZE 12

#define ADS1115_DELAY_AFTER_MUX_CHANGE 5 //5 ms

#include <stdio.h>
#include "freertos/FreeRTOS.h"

class ADS1115
{
  public:
    ADS1115();
    bool begin(void);
    bool begin(uint8_t);
    bool begin(int, int);
    bool begin(int, int, uint8_t);
    esp_err_t stop(void);
    void setDefault(void);
    void startSingleShotMeas(bool);
    bool getOpStatus(void);
    void setMux(uint8_t);
    uint8_t getMux(void);
    void setPGA(uint8_t);
    uint8_t getPGA(void);
    void setOpMode(bool);
    uint8_t getOpMode(void);
    void setRate(uint8_t);
    uint8_t getRate(void);
    void setCompMode(bool);
    uint8_t getCompMode(void);
    void setCompPolarity(bool);
    uint8_t getCompPolarity(void);
    void setCompLatchingMode(bool);
    uint8_t getCompLatchingMode(void);
    void setCompQueueMode(uint8_t);
    uint8_t getCompQueueMode(void);
    void setCompLowThreshBit(bool, int);
    uint8_t getCompLowThreshBit(int);
    void setCompHighThreshBit(bool, int);
    uint8_t getCompHighThreshBit(int);
    void setPinRdyMode(bool, uint8_t);
    bool getPinRdyMode(void);
    bool conversionReady(void);
    uint16_t readConversionRegister(void);
    bool isValueFrozen(void);
    float getConvVal(void);
    float getVoltVal(void);
    float getPhysVal(void);
    int getLatestBufVal(void);
    void printConfigReg(void);
    uint16_t getRegisterValue(uint8_t);
    void setRegisterValue(uint8_t, uint16_t);
    void setPhysConv(const float, const float);
    void setPhysConv(const float, const float, const float);
    void setPhysConv(const float[][2], size_t);
    void activateFilter();
    void deactivateFilter();
    bool getFilterStatus(void);
    int getAbsBufSize(void);
    int16_t* getBuffer(void);
    bool getConnectionStatus(void);
    uint16_t iConfigReg;
    void bitWrite(uint16_t *, int, bool);

  private:
    int _iSdaPin;
    int _iSclPin;
    uint8_t _iI2cAddress;
    uint8_t _iI2cRegPointer;
    float ** _ptrConvTable;
    size_t _iSizeConvTable;
    int _iConvMethod;
    float bitNumbering;
    uint16_t iLowThreshReg;
    uint16_t iHighThreshReg;
    esp_err_t i2c_master_init();
    void i2c_write(uint8_t, uint8_t*, size_t);
    void i2c_read(uint8_t, uint8_t*, size_t);
    void initConvTable(size_t);
    void writeBit(uint16_t &, int, bool);
    bool readBit(uint16_t, int);
    int _iBuffCnt;
    int _iBuffMaxFillIndex;
    int16_t * _ptrConvBuff;
    float * _ptrFilterCoeff;
    float _fFilterNormCoeff;
    bool _bFilterActive;
    bool _bSavGolFilterActive;
    bool _bConnectStatus;
    float _getAvgFilterVal();
    float _getSavGolFilterVal();
    
};

#endif
