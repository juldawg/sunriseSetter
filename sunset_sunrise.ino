#include <LiquidCrystal.h>

#include <Toggle.h>


#include <RTClib.h>
#include <TimerOne.h>
#include <Wire.h>
#include <Optional.h>
#include <map>

// ARDUINO CONSTANT & VARIABLE DEFINITIONS
// -------------------------------------
//   Arduino output pin receiving brightness levels for a specific color
#define RED_PIN 3
#define GREEN_PIN 6
#define BLUE_PIN 5
#define ledPin 13
#define alarm1Button 4
#define alarm2Button 7
#define radioButton 8
#define lightsButton 9
#define sunsetTriggerButton 10
#define timeSettingButton 11
#define rotaryEncoderButton 12
#define rotaryEncoderClk 2
#define rotaryEncoderDt 13

RTC_DS3231 rtc;
RTC_Millis rtc_millis;

static const uint8_t CLOCK_INTERRUPT_PIN = 3;
static const int MAX_RED = 255;
static const int MAX_GREEN = 255;
static const int MAX_BLUE = 150;
static const int idle_threshold_setting_mode = 5; // in seconds
static const int idle_threshold_time_setting_mode = 10; // in seconds

// The time of the day when the dimming shall start increasing (24 hour clock)
uint8_t START_HOUR = 9;
uint8_t START_MINUTE = 0;
uint8_t START_SECOND = 0;

boolean ALARM_DAYS[7] = {false,  //Sunday
                           true,  //Monday
                           true,  //Tuesday
                           true,  //Wednesday
                           true,  //Thursday
                           true,  //Friday
                           false}; //Saturday

uint8_t START_HOUR_1 = 9;
uint8_t START_MINUTE_1 = 0;
uint8_t START_SECOND_1 = 0;

boolean ALARM_DAYS_1[7] = {false,  //Sunday
                           true,  //Monday
                           true,  //Tuesday
                           true,  //Wednesday
                           true,  //Thursday
                           true,  //Friday
                           false}; //Saturday

// The time of the day when the dimming shall start decreasing (24 hour clock)
uint8_t END_HOUR = 23;
uint8_t END_MINUTE = 51;
uint8_t END_SECOND = 0;
// The duration of the dimming process (both from start time to 100% and from end time to 0%)
uint8_t DURATION_HOURS = 0;
uint8_t DURATION_MINUTES = 30;
uint8_t DURATION_SECONDS = 0;

boolean SUNSET_ALARM_DAYS[7] = {true,  //Sunday
                           true,  //Monday
                           true,  //Tuesday
                           true,  //Wednesday
                           true,  //Thursday
                           true,  //Friday
                           true}; //Saturday 

enum class AlarmType: uint8_t {
  SUNRISE = 1,
  SUNSET = 2
};

struct AlarmSettings {
  uint8_t hour;
  uint8_t minute;
  uint8_t second;
  boolean alarmDays[7];
  bool isActive = true;

  AlarmSettings(uint8_t hour,uint8_t minute,uint8_t second, boolean alarmDays[7]) {
    this->hour = hour;
    this->minute = minute;
    this->second = second;
    this->alarmDays[7] = alarmDays;
  }

  void toggleIsActive() {
    isActive = !isActive;
  }

  void setToNow() {
    DateTime now = rtc.now();
    hour = now.hour();
    minute = now.minute();
    second = now.second();
  }

  Optional<DateTime> nextAlarmDateTime() {
    if(isActive) {
      TimeSpan oneDay = TimeSpan(1, 0, 0, 0);
      DateTime now = rtc.now();
      DateTime alarmTime = DateTime(now.year(), now.month(), now.day(), hour, minute, second);
      if (alarmTime < now) {
          alarmTime = alarmTime + oneDay;
      }
      int i = 0;
      bool dayMatches = false;
      while (!dayMatches && i < 7) {
        uint8_t dayIndex = now.dayOfTheWeek();
        if(!alarmDays[dayIndex]){
          dayMatches = true;
        } else {
          alarmTime = alarmTime + oneDay;
          i++;
        }
      }
      if (!dayMatches) {
        return {}; // alarm is not active on any day (should not happen)
      }
      return alarmTime;
    } else {
      return {};
    }
  }
};

// User alarm settings storage for alarm 1&2
AlarmSettings sunriseAlarm = AlarmSettings(START_HOUR, START_MINUTE, START_SECOND, ALARM_DAYS);
AlarmSettings sunriseAlarm1 = AlarmSettings(START_HOUR_1, START_MINUTE_1, START_SECOND_1, ALARM_DAYS_1);
AlarmSettings sunsetAlarm = AlarmSettings(END_HOUR, END_MINUTE, END_SECOND, SUNSET_ALARM_DAYS);

struct Alarm {
  AlarmType type;
  Alarm(AlarmType type) {
    this->type = type;
  }

  Optional<DateTime> dateTime() { 
    switch (type) {
      case AlarmType::SUNRISE: return nextWakeUpAlarmDateTime();
      case AlarmType::SUNSET: return sunsetAlarm.nextAlarmDateTime();
    }
  }

  bool setAlarm(const DateTime &dt) {
    switch (type) {
      case AlarmType::SUNRISE: return rtc.setAlarm1(dt, DS3231_A1_Day);
      case AlarmType::SUNSET:  return rtc.setAlarm2(dt, DS3231_A2_Hour);
    }
  }

  private: Optional<DateTime> nextWakeUpAlarmDateTime() {
    Optional<DateTime> firstAlarm = sunriseAlarm.nextAlarmDateTime();
    Optional<DateTime> secondAlarm = sunriseAlarm1.nextAlarmDateTime();
    if (firstAlarm.hasValue() && secondAlarm.hasValue()) {
      return min(firstAlarm.getValue(), secondAlarm.getValue());
    } else if (firstAlarm.hasValue()) {
      return firstAlarm;
    }
    return secondAlarm;
  }
};

// Brightness representation in RGB
struct Brightness {
  int red;
  int green;
  int blue;

  static const int minValue = 0;
  static const int maxValue = 255;

  Brightness(int red, int green, int blue) {
    this->red = constrain(red, minValue, maxValue);
    this->green = constrain(green, minValue, maxValue);
    this->blue = constrain(blue, minValue, maxValue);
  }
};

enum class SettingMode {
  BRIGHTNESS,
  TIME,
  FREQUENCY,
  IDLE
};

enum class TimeSettingMode {
  HOURS,
  MINUTES,
  DAYS
};

enum class ButtonId {
  ALARM1,
  ALARM2,
  RADIO,
  LIGHTS,
  SUNSET_TRIGGER,
  TIME_SETTING,
  SETTING_VALIDATE
};

struct Button {
    ButtonId id;
    bool willReleaseLongPress = false;
    Toggle button;
    Button(ButtonId id, int pin)
    : id(id), button(Toggle(pin)) {}

    void setup() {
      button.setInputMode(Toggle::inputMode::input_pulldown);
      button.setInputInvert(true);
    }

    void poll(void (*onShortPress)(ButtonId), void (*onLongPress)(ButtonId)) {
      button.poll();
      if(button.pressedFor(800)) {
        if (!willReleaseLongPress) {
          onLongPress(id);
          willReleaseLongPress = true;
        }
      }
      if(button.onRelease()) {
        if(willReleaseLongPress) {
          willReleaseLongPress = false;
        } else {
          onShortPress(id);
        }
      }
    }
};

enum class SettingType {
  case BRIGHTNESS,
  case FREQUENCY,
  case HOUR,
  case MINUTE
}
struct Setting {
  SettingType type;
  int minValue;
  int maxValue;
  int steps;
};

std::map<SettingType, Setting> settings = {
  { SettingType::BRIGHTNESS, { 0,   255, 20 } },
  { SettingType::FREQUENCY,      {87.6,   107.4, 0.1 } },
  { SettingType::HOUR,      { 0,  23, 1 } },
  { SettingType::MINUTE,      { 0,  59, 1 } }
};

const unsigned long sunriseDuration = 30UL * 60UL * 1000UL;
unsigned long startingTime;
unsigned long settingChangeTime;
bool dimming_up = false;
bool dimming_down = false;
long delayBetweenIncrements;
SettingMode settingMode = SettingMode::IDLE;
bool radioOn = false;
bool lightsOn = false;
bool shouldSunRise = true;
Button buttons[6] = {
  Button(ButtonId::ALARM1, alarm1Button),
  Button(ButtonId::ALARM2, alarm2Button),
  Button(ButtonId::LIGHTS, lightsButton),
  Button(ButtonId::RADIO, radioButton),
  Button(ButtonId::SUNSET_TRIGGER, sunsetTriggerButton),
  Button(ButtonId::TIME_SETTING, timeSettingButton),
  Button(ButtonId::SETTING_VALIDATE, rotaryEncoderButton)
 }; 
volatile bool lastCLK = HIGH;
  
// =====================================
// ARDUINO SETUP ROUTINE
// -------------------------------------
void setup() {
  Serial.begin(9600);
  pinMode(RED_PIN, OUTPUT);
  pinMode(GREEN_PIN, OUTPUT);
  pinMode(BLUE_PIN, OUTPUT);
  pinMode(ledPin, OUTPUT);
  pinMode(rotaryEncoderClk, INPUT_PULLUP);
  pinMode(rotaryEncoderDt, INPUT_PULLUP);

  attachInterrupt(digitalPinToInterrupt(rotaryEncoderClk), readEncoder, CHANGE);
  setLEDS(Brightness(0,0,0));
  delayBetweenIncrements = sunriseDuration / 256;
  for (Button& button : buttons) {
    button.setup();
  }
  //initRTC();
}

// =====================================
// ARDUINO MAIN LOOP ROUTINE
// -------------------------------------
void loop() {
  for (Button& button : buttons) {
    button.poll(onShortPress, onLongPress);
  }
  digitalWrite(ledPin, lightsOn ? HIGH : LOW);

  // displayDigits();
  // if (rtc.alarmFired(1))
  //   {
  //     Serial.println("Alarm 1 has gone off. Dimming UP!\n");
  //     //reset flag
  //     rtc.clearAlarm(1);
  //     //Start dimming up
  //     dimming_up = shouldSunRise;
  //     startingTime = millis();
  //     setAlarm(Alarm(AlarmType::SUNRISE));
  //   }
  // if (rtc.alarmFired(2))
  //   {
  //     Serial.println("Alarm 1 has gone off. Dimming UP!\n");
  //     //reset flag
  //     rtc.clearAlarm(2);
  //     //Start dimming up
  //     dimming_down = true;
  //     startingTime = millis();      
  //     setAlarm(Alarm(AlarmType::SUNSET));

  //   }
  //   if (dimming_up || dimming_down) {
  //     unsigned long currentTime = millis();
  //     unsigned long elapsedTime = currentTime - startingTime;
  //     setLEDS(getUpdatedBrightness(currentTime, dimming_down));
      
  //     if (elapsedTime > sunriseDuration) {
  //       dimming_up = false;
  //       dimming_down = false;
  //     }
  //   } else {
  //     if (rtc.getAlarm1() < rtc.getAlarm2()) {
  //       setLEDS(Brightness(0,0,0));
  //     } else {
  //       setLEDS(Brightness(255,255,79));
  //     }
  //   }
    // delay(delayBetweenIncrements); // this might mess up the digit display
}

void displayDigits() {
  if (settingMode == SettingMode::IDLE) {
    // Display time
  } else if (settingMode == SettingMode::BRIGHTNESS) {
    // Display Brightness level
    // Check if should default back to idle
  }
}

void toggleLights() {
  lightsOn = !lightsOn;
}
    
 // Send the LED levels to the Arduino pins
 void setLEDS(Brightness brightness) {
  bool max = brightness.red == MAX_RED && brightness.green == MAX_GREEN && brightness.blue == MAX_BLUE;
  shouldSunRise = !max; // Disable sunrise when lights are already at max
  analogWrite(RED_PIN, brightness.red);
  analogWrite(GREEN_PIN, brightness.green);
  analogWrite(BLUE_PIN, brightness.blue);
}

Brightness getUpdatedBrightness(long elapsedTime, bool dimming_down) {

  float t = (float)elapsedTime / (float)sunriseDuration;
  t = constrain(t, 0.0, 1.0);

  if (dimming_down) {
    // inversion pour coucher de soleil
    t = 1.0 - t;
  }

  // courbe douce
  float p = t * t * (3 - 2 * t);

  // RGB
  int r, g, b;

  if (p < 0.3) {
    // Noir → rouge
    float x = p / 0.3;
    r = 255 * x;
    g = 20 * x;
    b = 0;
  }
  else if (p < 0.6) {
    // Rouge → orange/jaune
    float x = (p - 0.3) / 0.3;
    r = 255;
    g = 20 + x * 160;   // monte vers jaune
    b = x * 30;         // très léger bleu
  }
  else {
    // Jaune → blanc chaud
    float x = (p - 0.6) / 0.4;
    r = 255;
    g = 180 + x * 75;
    b = 30 + x * 120;   // bleu arrive très tard
  }

  float gamma = 2.2;
  r = pow(r / 255.0, gamma) * 255;
  g = pow(g / 255.0, gamma) * 255;
  b = pow(b / 255.0, gamma) * 255;

  return Brightness(r, g, b);
}

void initRTC(){
  // initializing the rtc
  if (!rtc.begin())
  {
    Serial.println("Couldn't find RTC!");
    Serial.flush();
    abort();
  }


  if (rtc.lostPower())
  {
    // this will adjust to the date and time at compilation
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }
  rtc_millis.begin(rtc.now());

  //we don't need the 32K Pin, so disable it
  rtc.disable32K();

  // Making it so, that the alarm will trigger an interrupt
  pinMode(CLOCK_INTERRUPT_PIN, INPUT_PULLUP);

  // stop oscillating signals at SQW Pin
  // otherwise setAlarm1 will fail
  rtc.writeSqwPinMode(DS3231_OFF);
  char buf1[] = "RTC Time: DD MM YYYY-hh:mm:ss";
  Serial.println(rtc.now().toString(buf1));
  setAlarm(Alarm(AlarmType::SUNSET));
  setAlarm(Alarm(AlarmType::SUNRISE));
}

void setAlarm(Alarm alarm) {
  uint8_t alarmType = static_cast<uint8_t>(alarm.type);
  rtc.clearAlarm(alarmType);
  Optional<DateTime> alarmTime = alarm.dateTime();
  if(alarmTime.hasValue()) {
    alarm.setAlarm(alarmTime.getValue());
  }
}

void onShortPress(ButtonId buttonId) {
  if (settingMode == SettingMode::IDLE) {
    switch(buttonId) {
      case ButtonId::ALARM1: 
        sunriseAlarm.toggleIsActive();
        break;
      case ButtonId::ALARM2: 
        sunriseAlarm1.toggleIsActive();
        break;
      case ButtonId::RADIO: 
        radioOn = !radioOn;
        break;
      case ButtonId::LIGHTS: 
        toggleLights();
        break;
      case ButtonId::SUNSET_TRIGGER: 
        sunsetAlarm.setToNow();
        break;
      case ButtonId::TIME_SETTING: 
        settingMode = SettingMode::TIME;
        break;
    }
  }
}

void onLongPress(ButtonId buttonId) {
  if (settingMode == SettingMode::IDLE) {
    switch(buttonId) {
      case ButtonId::ALARM1: 
        sunriseAlarm.toggleIsActive();
        //TODO: Briefly show the hour of the alarm if it's active
        break;
      case ButtonId::ALARM2: 
        sunriseAlarm1.toggleIsActive();
        break;
      case ButtonId::RADIO: 
        radioOn = !radioOn;
        break;
      case ButtonId::LIGHTS: 
        toggleLights();
        break;
      case ButtonId::SUNSET_TRIGGER: 
        sunsetAlarm.setToNow();
        break;
      case ButtonId::TIME_SETTING: 
        settingMode = SettingMode::TIME;
        break;
    }
  }
}

void readEncoder() {
  bool currentCLK = digitalRead(PIN_CLK);
  if (currentCLK != lastCLK) {
    if (digitalRead(PIN_DT) != currentCLK) {
      encoderDelta++;
    } else {
      encoderDelta--;
    }
  }
  lastCLK = currentCLK;
}

template <typename Value>
Value applyDelta(Setting &setting, Value currentValue, int delta) {
  return constrain(currentValue + delta * (setting.maxValue - setting.minValue) / setting.steps, settinsetting.minValue, p.maxValue);
}