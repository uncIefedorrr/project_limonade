// Подключаем библиотеку HX711 для тензодатчиков
#include "HX711.h"

// Библиотека для экрана
#include <LiquidCrystal_I2C.h> 

#include <GyverStepper.h>

// экран
LiquidCrystal_I2C lcd(0x27,16,2); 

// шаговый  двигатель https://alexgyver.ru/gyverstepper/
GStepper<STEPPER4WIRE> stepper(2048, 3, 5, 4, 6);
int current_position = 0;

// Создаем объект my_scale для работы с датчиком
HX711 tenzors[4];
String lcd_line1 = String();
String lcd_line2 = String();
float units[4];
float stakan_weight = 11.25;


float scale_coeff[] = {0.64, 2.07, 0.67, 0.71}; // столько реальных граммов в 1 "грамме" весов

// long offsets[] = {39699, 185392, -376463, 174120}; // оффсеты, они же average при пустых платформах (без стаканов)
long offsets[] = {37918, 189289, -379650, 173820}; // оффсеты, они же average при пустых платформах (без стаканов)

// Определяем пины для подключения датчиков давления
const uint8_t T_DATA_PINS[] = {25,29,33,37};
const uint8_t T_CLOCK_PINS[] = {27,31,35,39};

// допустимая погрешность показаний датчиков, в граммах
float threshold = 1.5; 

// сюда сохраняем текущий вес
float current_weight[] = {0, 0, 0, 0};


// Данные, полученные при калибровке весв
const uint32_t scale_factor = 1000;

// кнопки
const uint8_t CANCEL_BTN_PIN = 2;
const uint8_t OK_BTN_PIN = 52;

const uint8_t RECEIPT_PINS[] = {
  40, // 00X
  42, // 0X0
  44, // 0XX
  46, // X00
  48, // X0X
  50 // XX0
};


// помпы
const uint8_t PUMP_PINS[] {10, 11, 12};

// шаговый двигатель. угловые координаты парковочного места (0) и стаканов (1 - 4)
const int PARKING_SLOTS[] = {0, 800, 590, 450, 260};
int empty_glasses[] = {-1, -1, -1, -1};

String buttons_pressed = "";


void setup() {

  Serial.begin(9600); 

  // инициализация экрана
  lcd.init(); 
  lcd.backlight(); 
  

  // attachInterrupt(digitalPinToInterrupt(CANCEL_BTN_PIN), interrupt_btn_pressed, RISING);

  // инициализация тензодатчиков
  for (int i=0; i<4; i++) {
    tenzors[i].begin(T_DATA_PINS[i], T_CLOCK_PINS[i]);
    tenzors[i].set_scale(scale_factor); 
    while (!tenzors[i].is_ready()) {
      delay(10);
    }
    // delay(3000);
    tenzors[i].set_offset(offsets[i]);
    tenzors[i].tare(20);
    offsets[i] = tenzors[i].get_offset();
  }

    
    // инициализация кнопок
  pinMode(CANCEL_BTN_PIN, INPUT);
  pinMode(OK_BTN_PIN, INPUT);

  for (int i=0; i<6; i++) {
    pinMode(RECEIPT_PINS[i], INPUT);
  }
  
  // инициализация пинов трёх помп
  for (int i=0; i<3; i++) {
    pinMode(PUMP_PINS[i], OUTPUT);
    digitalWrite(PUMP_PINS[i], LOW);
  }

  // шаговый двигаетль, всё взято из примера
  stepper.setRunMode(KEEP_SPEED);

  // можно установить скорость
  stepper.setSpeed(120);    // в шагах/сек
  stepper.setSpeedDeg(80);  // в градусах/сек

  // режим следования к целевй позиции
  stepper.setRunMode(FOLLOW_POS);

  // можно установить позицию
  // stepper.setTarget(-2024);    // в шагах
  // stepper.setTargetDeg(-360);  // в градусах

  // установка макс. скорости в градусах/сек
  stepper.setMaxSpeedDeg(400);
  
  // установка макс. скорости в шагах/сек
  stepper.setMaxSpeed(400);

  // установка ускорения в градусах/сек/сек
  stepper.setAccelerationDeg(300);

  // установка ускорения в шагах/сек/сек
  stepper.setAcceleration(300);

  // отключать мотор при достижении цели
  stepper.autoPower(true);

  // включить мотор (если указан пин en)
  stepper.enable();



}
// средняя нулевая, ближе к парковке первая, дальняя от парковки вторая

int * choose_receipt(int receipt_btn, int pump) {
  int receipt[] = {
    0, // сироп
    0, // сок
    0  // газировка
  };

  switch (receipt_btn) {
    case 0: // a. Газированная вода (50 мл.) 
      receipt[0] = 0; 
      receipt[1] = 0;
      receipt[2] = 50;
      break;
    case 1: // b. Мятный сироп (10 мл.) 
      receipt[0] = 10;
      receipt[1] = 0; 
      receipt[2] = 0;
      break;
    case 2: // c. Апельсиновый сок (40 мл.) 
      receipt[0] = 0;
      receipt[1] = 40;
      receipt[2] = 0;
      break;
    case 3: // d. Лимонад “Мятный” (80 мл. газированной воды + 20 мл. мятного сиропа). 
      receipt[0] = 20;
      receipt[1] = 0;
      receipt[2] = 80;
      break;
    case 4: // e. Лимонад “Заводной апельсин” (30 мл. газированной воды + 50 мл. апельсинового сока). 
      receipt[0] = 0;
      receipt[1] = 50;
      receipt[2] = 30;
      break;
    case 5: // f. Лимонад ‘Тройной” (35 мл. газированной воды + 45 мл. апельсинового сока + 10 мл. мятного сиропа)
      receipt[0] = 10;
      receipt[1] = 45;
      receipt[2] = 35;
      break;
  }
  return(receipt[pump]);
}


// функция поворота после подключения шагового мотора
void move_to(int position) {
  Serial.println("move_to: " + String(position));
  // if (!stepper.tick()) {
  //   stepper.setTarget(where_to_move, RELATIVE);
  // }
  Serial.println("move_to: wait tikcker before move");
  while (stepper.tick()) {
    delay(10);
  }
  Serial.println("move_to: started moving to " + String(position));

  if (!stepper.tick()) {
    stepper.setTarget(PARKING_SLOTS[position]);
  }
  Serial.println("move_to: wait tikcker after move");

  while (stepper.tick()) {
    delay(10);
  }  
  delay(1000);
}


void fill_glass(int tenzor, int receipt_index) {
  Serial.println(tenzor);
  Serial.println("Nalivaem stakan (tenzor)" + String(tenzor) + " napitkom " + String(receipt_index));
  // return;
  int receipt[] = {0, 0, 0};
  for (int i=0;  i<3; i++) {
    receipt[i] = choose_receipt(receipt_index, i);
  }

  Serial.println("sostav_napitka");
  for (int i=0; i<3; i++){
    Serial.println("PUMP " + String(i) + " = " + String(receipt[i]));
  }
  tenzors[tenzor].tare(); // хак обнулить показания датчиков на пустом стакане
  float weight_done = 0; // вес уже налитых частей
  float receipt_done = 0; // вес уже налитых частей
  for (uint8_t pump = 0; pump < 3; pump++) {
    // сколько максимально надо лить следующей жидкости
    float limit_weight = receipt_done + receipt[pump];
    Serial.println("Pump " + String(pump) + ", nado nalit " + String(receipt[pump]) + ", uzhe nalito " + String(weight_done));
    unsigned long time = millis();
    float check_weight = 0;
    float prev_weight = 0;
    if(receipt[pump] == 0) {
      Serial.println("Pump " + String(pump) + " is not used");
      continue;
    }
    check_weight = tenzors[tenzor].get_units(5) / scale_coeff[tenzor];
    Serial.println("check_weight " + String(check_weight));
    while (check_weight < limit_weight - threshold) {
      digitalWrite(PUMP_PINS[pump], HIGH);
      Serial.println("Pump " + String(pump) + " is ON");
      int cancel_btn_pressed = digitalRead(CANCEL_BTN_PIN);
      if (cancel_btn_pressed == HIGH) {
        digitalWrite(PUMP_PINS[pump], LOW);
        return;
      }
      
      check_weight = tenzors[tenzor].get_units(1) / scale_coeff[tenzor];
      Serial.println("check_weight = " + String(check_weight) + ", prev_weight = " + String(prev_weight) + ", limit_weight = " + String(limit_weight) + ", weight_done = " + String(weight_done) + "receipt_done = " + String(receipt_done));
      // если вес уменьшился, т.е. стакан взяли или льём слишком долго, то экстранно стопаем процесс
      if (check_weight < prev_weight - threshold ) {
        Serial.println("Weight decreased. Probably glass is taken out. Stop");
        digitalWrite(PUMP_PINS[pump], LOW);
        return;
      }
      if (millis() - time > 20) {
        time = millis();
        prev_weight = check_weight;
      } 
      // // Если льём слишком долго, то остановка
      // if (millis() - time > 20000) { // 20 секунд - тут можно вычислить среднее время наливания и учитывать оставшийся объём < (receipt[pump] - current_weight[stakan])/avg_filling_speed >
      //   Serial.println("Liquid falling too long. Stop");
      //   digitalWrite(PUMP_PINS[pump], LOW);
      //   return;
      // }        

    }
    digitalWrite(PUMP_PINS[pump], LOW);
    Serial.println("Pump " + String(pump) + " is OFF");
    weight_done = weight_done + check_weight;
    receipt_done = receipt_done + receipt[pump];
    // }
  }
}


void show_tenzors() {

  for (int i=0; i<4; i++) {
    units[i] = tenzors[i].get_units(5) / scale_coeff[i];
    // units[i] = tenzors[i].get_units(5) * scale_coeff[i];
    Serial.println("Tenzor" + String(i) + " = " + String(units[i]));

  }
  lcd_line1 = String(units[0]) + "        ";
  lcd_line1 = lcd_line1.substring(0,7);
  lcd_line1 += String(units[1]) + "        ";
  lcd_line1 = lcd_line1.substring(0,15);

  lcd_line2 = String(units[2]) + "        ";
  lcd_line2 = lcd_line2.substring(0,7);
  lcd_line2 += String(units[3]) + "        ";
  lcd_line2 = lcd_line2.substring(0,15);

  lcd.setCursor(0, 0);
  lcd.print(lcd_line1);
  lcd.setCursor(0, 1);
  lcd.print(lcd_line2);

}

void fill_glass_test() {
unsigned long time;
for (uint8_t pump = 0; pump < 3; pump++) {
    // сколько максимально надо лить следующей жидкости
    Serial.println("Pump " + String(pump) + " started (pin + " + String(PUMP_PINS[pump]) + ")");


    unsigned long time = millis();
    while (millis() - time < 3000) {
      digitalWrite(PUMP_PINS[pump], HIGH);

    }
    digitalWrite(PUMP_PINS[pump], LOW);
      Serial.println("Pump " + String(pump) + " stopped (pin + " + String(PUMP_PINS[pump]) + ")");

  }

}

int prev_btn = -1;
long time = millis();
bool ready = false;
int buttons_pressed_ar[] = {-1, -1, -1, -1};
int k = 0;


void my_init() {
  Serial.println("initializing to defaults");  
  ready = false;
  buttons_pressed = "";
  for (int i=0; i<4; i++) {
    buttons_pressed_ar[i] = -1;
    empty_glasses[i] = -1;
  }
  k = 0;
  for (int i=0; i<4; i++) {
    tenzors[i].set_offset(offsets[i]);
  }
}

int find_empty_glasses() {
  Serial.println("Trying to find empty glasses");
  int found = 0;
  for (int i=0; i<4; i++) {
    Serial.println("------ TENSOR " + String(i));
    float units = tenzors[i].get_units() / scale_coeff[i];
    // Serial.println("checking tenzor " + String(i) + ": units (gramms) = " + String(units) + ": average = " + String(tenzors[i].read_average()) + ", offset = " + String(tenzors[i].get_offset()) + ", stakan_weight = " + String(stakan_weight)); 
    // Serial.println("abs(units - stakan_weight) = " + String(abs(units - stakan_weight)));
    // Serial.println("threshold = " + String(threshold));
    if (abs(units - stakan_weight) < threshold * 4) { 
      found += 1;
      Serial.println("TENSOR " + String(i) + " good");
      empty_glasses[i] = i+1; // индексы стаканов с 1 до 4
      tenzors[i].tare();
    } else {
      Serial.println("TENSOR " + String(i) + " bad");
    }
  }
  Serial.println("Found " + String(found) + " empty glasses");
  return found;
}
bool cancel_pressed_twice = false;


void loop() {
  //show_tenzors();
  //return;
  if (!ready) {
    lcd.setCursor(0, 0);
    lcd.print("READY.          ");
    lcd.setCursor(0, 1);
    lcd.print("                ");
    Serial.println("READY");
    ready = true;
  }
  
  // тут написать ожидание нажатия кнопки с рецептом
  
  for (int i=0; i<6; i++) {
    int btn_pressed = digitalRead(RECEIPT_PINS[i]);

    // if (RECEIPT_PINS[i] == OK_BTN_PIN) {
    //   Serial.println("OK button pressed, buttons_pressed=" + buttons_pressed + " pin=" + RECEIPT_PINS[i]);  
    //   buttons_pressed = "";
    //   break;
    // }
    if (btn_pressed == HIGH) {
      // Serial.println(("buttons_pressed=" + buttons_pressed + " pin=" + RECEIPT_PINS[i]));  
      if (prev_btn == i and millis() - time < 1000 or buttons_pressed.length() == 8) {
        return;
      } 
      prev_btn = i;
      time = millis();

      buttons_pressed += String(i) + " ";
      buttons_pressed_ar[k] = i;
      k++;
      lcd.setCursor(0, 0);
      lcd.print("Receipts chosen:");
      lcd.setCursor(0, 1);
      lcd.print(buttons_pressed + "                ");
      Serial.println(("buttons_pressed=" + buttons_pressed + " pin=" + RECEIPT_PINS[i]));  
      break;
    }
    // Serial.println("buttons_pressed=" + buttons_pressed + " pin=" + RECEIPT_PINS[i]);  
  }  
  int cancel_btn_pressed = digitalRead(CANCEL_BTN_PIN);

  if (cancel_btn_pressed == HIGH) {
    Serial.println("Cancel pressed"); 
    if (cancel_pressed_twice) {
      cancel_pressed_twice = false;
      Serial.println("Reload tenzors"); 
      for (int i=0; i<4; i++){
        tenzors[i].tare();
        offsets[i] = tenzors[i].get_offset();
      }
    }
    cancel_pressed_twice = true;
    show_tenzors();
    my_init();
    return;
  }

  int ok_btn_pressed = digitalRead(OK_BTN_PIN);
  
  if (ok_btn_pressed == HIGH) {
    Serial.println("OK pressed");  
    cancel_pressed_twice = false;
    if (buttons_pressed.length() == 0) {
      my_init();
      return;
    }
    int empty_glasses_found = find_empty_glasses();
    int cnt = 0; // количесво напитков
    Serial.println("Parsing receipts");  
    for (int i = 0; i < 4; i ++) {
      if (buttons_pressed_ar[i]>=0) {
        cnt +=1;
      }
    }
    if (cnt == 0) {
        my_init();
        return;
    }
    if (cnt > empty_glasses_found) {
      Serial.println("Not enough empty glasses, cnt = " + String(cnt) + ", empty_glasses_found " + String(empty_glasses_found));
      lcd.setCursor(0, 0);
      lcd.print("Not enough      ");
      lcd.setCursor(0, 1);
      lcd.print("   empty glasses");
      delay(5000);
      my_init();
      return;
    };

    String sss = "";
    for (int i=0; i<4; i++) {
      sss += String(empty_glasses[i]);
    }
    Serial.println(sss);
    // cnt = количество нажатых кнопок
    // empty_glasses - массив пустых стаканов. матчится на PARKING_SLOTS[] = {0, 800, 590, 450, 260}; т.е. стаканы начинаются с индекса 1 и до 4
    for (int i=0; i<cnt; i++) {
      Serial.println("Starting receipt " + String(buttons_pressed_ar[i]));
      int j = 0;
      while (empty_glasses[j] < 0) {
        j += 1;
      }
      move_to(empty_glasses[j]);

      //тут код наливайки
      fill_glass(empty_glasses[j] - 1, buttons_pressed_ar[i]);

      // закончили наливать
      empty_glasses[j] = -1;
    }
     

    move_to(0);  
    my_init();
    return;
  }
  
  // while (imdone == 0) {
  //   //
  //   imdone = 1;
  // }


}