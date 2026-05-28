
#include <RTClib.h>
#include <TimerOne.h>
#include <Wire.h>
#include <Optional.h>

// ARDUINO CONSTANT & VARIABLE DEFINITIONS
// -------------------------------------
//   Arduino output pin receiving brightness levels for a specific color
#define RED_PIN 3
#define GREEN_PIN 6
#define BLUE_PIN 5

RTC_DS3231 rtc;
RTC_Millis rtc_millis;

static const uint8_t CLOCK_INTERRUPT_PIN = 3;
static const int MAX_RED = 255;
static const int MAX_GREEN = 255;
static const int MAX_BLUE = 150;

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

  AlarmSettings(uint8_t hour,uint8_t minute,uint8_t second, boolean alarmDays[7]) {
    this->hour = hour;
    this->minute = minute;
    this->second = second;
    this->alarmDays[7] = alarmDays;
  }

  Optional<DateTime> nextAlarmDateTime() {
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
  }
};

struct Alarm {
  AlarmType type;
  Alarm(AlarmType type) {
    this->type = type;
  }

  Optional<DateTime> dateTime() { 
    switch (type) {
      case AlarmType::SUNRISE: return nextWakeUpAlarmDateTime();
      case AlarmType::SUNSET: return AlarmSettings(END_HOUR, END_MINUTE, END_SECOND, SUNSET_ALARM_DAYS).nextAlarmDateTime();
    }
  }

  bool setAlarm(const DateTime &dt) {
    switch (type) {
      case AlarmType::SUNRISE: return rtc.setAlarm1(dt, DS3231_A1_Day);
      case AlarmType::SUNSET:  return rtc.setAlarm2(dt, DS3231_A2_Hour);
    }
  }

  private: Optional<DateTime> nextWakeUpAlarmDateTime() {
    AlarmSettings alarmSettings = AlarmSettings(START_HOUR, START_MINUTE, START_SECOND, ALARM_DAYS);
    AlarmSettings alarmSettings1 = AlarmSettings(START_HOUR_1, START_MINUTE_1, START_SECOND_1, ALARM_DAYS_1);
    Optional<DateTime> firstAlarm = alarmSettings.nextAlarmDateTime();
    Optional<DateTime> secondAlarm = alarmSettings1.nextAlarmDateTime();
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
  FREQUENCY
};

const unsigned long sunriseDuration = 30UL * 60UL * 1000UL;
unsigned long startingTime;
bool dimming_up = false;
bool dimming_down = false;
long delayBetweenIncrements;
SettingMode settingMode = SettingMode::BRIGHTNESS;
bool radioOn = false;
bool lightsOn = false;
bool shouldSunRise = true;
  
// =====================================
// ARDUINO SETUP ROUTINE
// -------------------------------------
void setup() {
  Serial.begin(9600);
  pinMode(RED_PIN, OUTPUT);
  pinMode(GREEN_PIN, OUTPUT);
  pinMode(BLUE_PIN, OUTPUT);
  setLEDS(Brightness(0,0,0));
  delayBetweenIncrements = sunriseDuration / 256;
  initRTC();
}

// =====================================
// ARDUINO MAIN LOOP ROUTINE
// -------------------------------------
void loop() {
  if (rtc.alarmFired(1))
    {
      Serial.println("Alarm 1 has gone off. Dimming UP!\n");
      //reset flag
      rtc.clearAlarm(1);
      //Start dimming up
      dimming_up = shouldSunRise;
      startingTime = millis();
      setAlarm(Alarm(AlarmType::SUNRISE));
    }
  if (rtc.alarmFired(2))
    {
      Serial.println("Alarm 1 has gone off. Dimming UP!\n");
      //reset flag
      rtc.clearAlarm(2);
      //Start dimming up
      dimming_down = true;
      startingTime = millis();      
      setAlarm(Alarm(AlarmType::SUNSET));

    }
    if (dimming_up || dimming_down) {
      unsigned long currentTime = millis();
      unsigned long elapsedTime = currentTime - startingTime;
      setLEDS(getUpdatedBrightness(currentTime, dimming_down));
      
      if (elapsedTime > sunriseDuration) {
        dimming_up = false;
        dimming_down = false;
      }
    } else {
      if (rtc.getAlarm1() < rtc.getAlarm2()) {
        setLEDS(Brightness(0,0,0));
      } else {
        setLEDS(Brightness(255,255,79));
      }
    }
    delay(delayBetweenIncrements);
}

void toogleLights(bool On) {
  lightsOn = On;
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
  attachInterrupt(digitalPinToInterrupt(CLOCK_INTERRUPT_PIN), onAlarmIsr, FALLING);

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


void onAlarmIsr()
{
  //This typical content of this ISR is basically covered by the RTCLib library.
}