#include <Arduino.h>
#include <Servo.h>
#include <gpio_expansion_board.h>
#include "matrix_keyboard_v3.h"
#include "DHT.h"
#include "Wire.h"
#include "digit_display.h"
#include "LiquidCrystal_I2C.h"

// Arduino pins / Arduino 引脚
const int BUZZER_PIN = 2;
const int FAN_PIN = 4;
const int GAS_SENSOR_PIN = 6;
const int GARAGE_SENSOR_PIN = 8;
const int GREEN_LED_PIN = 9;
const int YELLOW_LED_PIN = 10;
const int RED_LED_PIN = 11;
const int GARAGE_SERVO_PIN = 12;
const int FLAME_SENSOR_PIN = 13;
const int LIQUID_SENSOR_PIN = A1;
const int DHT_SENSOR_PIN = A0;

// Sensor thresholds / 传感器阈值
const int GAS_THRESHOLD = 1005;
const int LIQUID_DRY_THRESHOLD = 500;
const int LIGHT_OPEN_THRESHOLD = 100;
const float TEMP_FAN_THRESHOLD = 30.0;
const float HUMIDITY_FAN_THRESHOLD = 45.0;

// Servo angles / 舵机角度
const int GARAGE_OPEN_ANGLE = 90;
const int GARAGE_CLOSED_ANGLE = 5;
const int DOOR_OPEN_ANGLE = 20;
const int DOOR_CLOSED_ANGLE = 80;
const int WINDOW_OPEN_ANGLE = 0;
const int WINDOW_CLOSED_ANGLE = 90;

// Access control / 门禁控制
const String PASSWORD = "2468";

GpioExpansionBoard GEB;                   // GPIO expansion board / GPIO拓展板
DHT dhtA0(DHT_SENSOR_PIN, 11);            // DHT temperature and humidity sensor / DHT温湿度传感器模块
Servo servo_2;                            // Garage-door servo motor / 车库门舵机
MatrixKeyboard myMatrixKeyboardV3(0x65);  // Matrix keypad / 矩阵键盘
DigitDisplay myVK16K33(0x70);             // Four-digit display / 四位数码管显示器
LiquidCrystal_I2C mylcd(0x27, 16, 2);     // 16x2 I2C LCD display / 16x2 I2C LCD显示屏

// Entrance-door input storage / 大门输入参数储存
int index;
String key;
String keyStr;

// Gas sensor value / 气体传感器数值
int gas;

// Flame sensor value / 火焰传感器数值
int fire;

// Light sensor value / 光线传感器数值
int light;

// Liquid sensor value / 液体传感器数值
int liquid;

// Temperature value / 温度数值
float temperature;

// Humidity value / 湿度数值
float humidity;

void setup()
{
  // Initialize serial communication / 初始化串口通信
  Serial.begin(9600);

  pinMode(GAS_SENSOR_PIN, INPUT);      // Gas sensor / 气体传感器
  pinMode(GARAGE_SENSOR_PIN, INPUT);   // Infrared obstacle sensor / 红外避障传感器
  pinMode(FLAME_SENSOR_PIN, INPUT);    // Flame sensor / 火焰传感器

  pinMode(BUZZER_PIN, OUTPUT);         // Passive buzzer / 无源蜂鸣器
  pinMode(FAN_PIN, OUTPUT);            // DC fan module / 直流电动机风扇模块
  pinMode(GREEN_LED_PIN, OUTPUT);      // Green LED indicator / 绿色指示灯
  pinMode(YELLOW_LED_PIN, OUTPUT);     // Yellow LED indicator / 黄色指示灯
  pinMode(RED_LED_PIN, OUTPUT);        // Red LED indicator / 红色指示灯
  pinMode(LIQUID_SENSOR_PIN, INPUT);   // Liquid sensor / 液体传感器

  servo_2.attach(GARAGE_SERVO_PIN);    // Attach garage-door servo / 连接车库门舵机
  dhtA0.begin();                       // Initialize DHT sensor / 初始化温湿度传感器
  myVK16K33.Setup();                   // Initialize four-digit display / 初始化数码管显示器
  myVK16K33.ShowNumber(double(0), 3);  // Show initial value on display / 在数码管上显示初始值
  mylcd.init();                        // Initialize LCD / 初始化LCD屏幕
  mylcd.backlight();                   // Turn on LCD backlight / 打开LCD背光

  GEB.SetGpioMode(GpioExpansionBoard::kGpioPinE1, GpioExpansionBoard::kPwm);            // Entrance-door servo / 大门舵机
  GEB.SetGpioMode(GpioExpansionBoard::kGpioPinE2, GpioExpansionBoard::kPwm);            // Window servo / 窗户舵机
  GEB.SetGpioMode(GpioExpansionBoard::kGpioPinE3, GpioExpansionBoard::kInputPullDown);  // Touch sensor / 触摸传感器
  GEB.SetGpioMode(GpioExpansionBoard::kGpioPinE4, GpioExpansionBoard::kInputPullDown);  // PIR motion sensor / 人体感应传感器模块
  GEB.SetGpioMode(GpioExpansionBoard::kGpioPinE5, GpioExpansionBoard::kAdc);            // Photoresistor / 光敏电阻

  // Set up access-control keypad / 设置门禁键盘
  myMatrixKeyboardV3.Setup();
  index = 0;
  key = "";
  keyStr = "";

  // Set initial safe system state / 设置系统初始安全状态
  gas = 0;            // Initial gas sensor value / 气体传感器初始值
  fire = 0;           // Initial flame sensor value / 火焰传感器初始值
  light = 0;          // Initial light sensor value / 光线传感器初始值
  liquid = 0;         // Initial liquid sensor value / 液体传感器初始值
  temperature = 0.0;  // Initial temperature value / 温度传感器初始值
  humidity = 0.0;     // Initial humidity value / 湿度传感器初始值

  digitalWrite(FAN_PIN, LOW);         // Turn off fan / 关闭风扇
  digitalWrite(GREEN_LED_PIN, LOW);   // Turn off green LED / 关闭绿灯
  digitalWrite(YELLOW_LED_PIN, LOW);  // Turn off yellow LED / 关闭黄灯
  digitalWrite(RED_LED_PIN, LOW);     // Turn off red LED / 关闭红灯
  noTone(BUZZER_PIN);                 // Turn off buzzer / 关闭蜂鸣器

  servo_2.write(GARAGE_CLOSED_ANGLE);  // Close garage door / 关闭车库门
  GEB.SetServoAngle(GpioExpansionBoard::kGpioPinE1, DOOR_CLOSED_ANGLE);    // Close entrance door / 关闭主门
  GEB.SetServoAngle(GpioExpansionBoard::kGpioPinE2, WINDOW_CLOSED_ANGLE);  // Close window / 关闭窗户
}

void loop()
{
  // Motion-responsive light / 智能人体感应灯
  if (GEB.GetGpioLevel(GpioExpansionBoard::kGpioPinE4) == true)
  {
    digitalWrite(GREEN_LED_PIN, HIGH);  // Turn on light / 点亮灯
  }
  else
  {
    digitalWrite(GREEN_LED_PIN, LOW);   // Turn off light / 熄灭灯
  }

  // Touch-activated doorbell / 触摸式音乐门铃
  if (GEB.GetGpioLevel(GpioExpansionBoard::kGpioPinE3) == false)
  {
    tone(BUZZER_PIN, 131);
    delay(600);

    tone(BUZZER_PIN, 587);
    delay(500);

    tone(BUZZER_PIN, 587);
    delay(500);

    tone(BUZZER_PIN, 659);
    delay(500);

    tone(BUZZER_PIN, 784);
    delay(500);

    tone(BUZZER_PIN, 880);
    delay(500);

    tone(BUZZER_PIN, 880);
    delay(500);

    tone(BUZZER_PIN, 988);
    delay(500);

    tone(BUZZER_PIN, 988);
    delay(500);

    tone(BUZZER_PIN, 659);
    delay(500);
  }
  else
  {
    noTone(BUZZER_PIN);
  }

  // Automatic garage door / 自动车库门
  if (digitalRead(GARAGE_SENSOR_PIN) == 0)
  {
    servo_2.write(GARAGE_CLOSED_ANGLE);
    delay(1000);
  }
  else
  {
    servo_2.write(GARAGE_OPEN_ANGLE);
    digitalWrite(RED_LED_PIN, HIGH);
    delay(1000);
    digitalWrite(RED_LED_PIN, LOW);
  }

  // Password-controlled entrance door / 智能门禁
  key = myMatrixKeyboardV3.GetTouchedKey();

  if (key != "")
  {
    if (key == "#")
    {
      // Clear current input / 清空当前输入
      Serial.println("Clean");
      keyStr = "";
      index = 0;
    }
    else
    {
      keyStr = String(keyStr) + String(key);
      index = index + 1;
      Serial.println(keyStr);

      // Keypad feedback LED / 键盘输入反馈灯
      digitalWrite(YELLOW_LED_PIN, HIGH);
      delay(500);
      digitalWrite(YELLOW_LED_PIN, LOW);

      if (index == PASSWORD.length())
      {
        if (keyStr == PASSWORD)
        {
          Serial.println("hello");
          keyStr = "";

          // Open entrance door, wait, then close / 打开主门，等待后关闭
          GEB.SetServoAngle(GpioExpansionBoard::kGpioPinE1, DOOR_OPEN_ANGLE);
          delay(5000);

          GEB.SetServoAngle(GpioExpansionBoard::kGpioPinE1, DOOR_CLOSED_ANGLE);
          delay(1000);
        }
        else
        {
          // Keep entrance door closed after wrong password / 密码错误时保持主门关闭
          GEB.SetServoAngle(GpioExpansionBoard::kGpioPinE1, DOOR_CLOSED_ANGLE);
          delay(1000);
        }

        index = 0;
        keyStr = "";
      }
    }
  }

  // Rain-controlled and light-controlled automatic window / 智能雨控、光控自动窗户
  liquid = analogRead(LIQUID_SENSOR_PIN);
  Serial.print(String("liquid: ") + String(liquid));
  Serial.print(", ");
  light = GEB.GetGpioAdcValue(GpioExpansionBoard::kGpioPinE5);
  Serial.println(String("light: ") + String(light));
  delay(200);

  myVK16K33.ShowNumber(double(light), 3);
  delay(500);

  // Liquid sensor calibration: liquid > 500 means no rain; liquid <= 500 means rain or water detected.
  // 液体传感器校准：liquid > 500 表示未检测到雨水；liquid <= 500 表示检测到雨水。
  // Open the window only when no rain is detected and the light value is greater than 100.
  // 仅在无雨且光照值大于100时打开窗户；否则关闭窗户。
  if (liquid > LIQUID_DRY_THRESHOLD)
  {
    if (light > LIGHT_OPEN_THRESHOLD)
    {
      GEB.SetServoAngle(GpioExpansionBoard::kGpioPinE2, WINDOW_OPEN_ANGLE);
      delay(500);
    }
    else
    {
      GEB.SetServoAngle(GpioExpansionBoard::kGpioPinE2, WINDOW_CLOSED_ANGLE);
      delay(500);
    }
  }
  else
  {
    GEB.SetServoAngle(GpioExpansionBoard::kGpioPinE2, WINDOW_CLOSED_ANGLE);
    delay(500);
  }

  // Smart smoke alarm and air-quality monitoring system / 智能烟雾报警器 + 空气质量检测系统
  gas = analogRead(GAS_SENSOR_PIN);
  fire = digitalRead(FLAME_SENSOR_PIN);

  temperature = dhtA0.readTemperature();
  humidity = dhtA0.readHumidity();
  Serial.print(String("temperature: ") + String(temperature));
  Serial.print(", ");
  Serial.println(String("humidity: ") + String(humidity));
  delay(500);

  // Serial debugging output / 串口调试输出
  Serial.print(String("gas: ") + String(gas));
  Serial.print(", ");
  Serial.println(String("fire: ") + String(fire));
  Serial.println();
  delay(200);

  bool smokeDetected = gas > GAS_THRESHOLD;
  bool fireDetected = fire == 0;
  bool highTemperature = temperature > TEMP_FAN_THRESHOLD;
  bool highHumidity = humidity > HUMIDITY_FAN_THRESHOLD;

  bool shouldRunFan = smokeDetected || highTemperature || highHumidity;
  digitalWrite(FAN_PIN, shouldRunFan ? HIGH : LOW);

  // Trigger alarm when gas exceeds the threshold or flame is detected.
  // 当气体数值超过阈值，或火焰传感器检测到火焰时触发报警。
  if (smokeDetected)
  {
    // Activate buzzer alarm / 打开蜂鸣器发出警报
    tone(BUZZER_PIN, 131);
    delay(1000);

    tone(BUZZER_PIN, 220);
    delay(1000);
  }
  else if (fireDetected)
  {
    tone(BUZZER_PIN, 131);
    delay(1000);

    tone(BUZZER_PIN, 220);
    delay(1000);
  }
  else
  {
    // Turn off buzzer / 关闭蜂鸣器
    noTone(BUZZER_PIN);
  }

  // Display temperature and humidity data / 显示温湿度数据
  mylcd.setCursor(0, 0);
  mylcd.print(String("T:") + String(temperature));

  mylcd.setCursor(0, 1);
  mylcd.print(String("H:") + String(humidity));
}