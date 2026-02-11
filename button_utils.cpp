#include "button_utils.h"
#include "config.h"

// Variables globales para detector no bloqueante
unsigned long btn_last_press_time = 0;
int btn_press_count = 0;
bool btn_was_pressed = false;

void init_button_detector()
{
  btn_last_press_time = 0;
  btn_press_count = 0;
  btn_was_pressed = false;
}

// Detector no bloqueante de doble click. Debe llamarse con frecuencia (cada <100ms idealmente).
bool nonBlockingDoubleClickDetected(unsigned long windowMs)
{
  unsigned long now = millis();
  bool pressed = (digitalRead(PRG_BUTTON_PIN) == LOW);

  // detectar flanco de bajada (press)
  if (pressed && !btn_was_pressed)
  {
    // Si la última pulsación fue hace más tiempo que la ventana, resetear conteo
    if (now - btn_last_press_time > windowMs)
    {
      btn_press_count = 0;
    }

    btn_press_count++;
    btn_last_press_time = now;
    btn_was_pressed = true;
  }

  // detectar flanco de subida (release)
  if (!pressed && btn_was_pressed)
  {
    btn_was_pressed = false;
  }

  // si se acumularon 2 pulsaciones dentro de la ventana -> doble click
  if (btn_press_count >= 2 && (now - btn_last_press_time) <= windowMs)
  {
    btn_press_count = 0; // reset
    return true;
  }

  // reset si se excede la ventana
  if ((now - btn_last_press_time) > windowMs)
  {
    btn_press_count = 0;
  }

  return false;
}

// Detector no bloqueante de triple click (3 pulsaciones en ventana)
bool nonBlockingTripleClickDetected(unsigned long windowMs)
{
  unsigned long now = millis();
  bool pressed = (digitalRead(PRG_BUTTON_PIN) == LOW);

  // detectar flanco de bajada (press)
  if (pressed && !btn_was_pressed)
  {
    // Si la última pulsación fue hace más tiempo que la ventana, resetear conteo
    if (now - btn_last_press_time > windowMs)
    {
      btn_press_count = 0;
    }

    btn_press_count++;
    btn_last_press_time = now;
    btn_was_pressed = true;
  }

  // detectar flanco de subida (release)
  if (!pressed && btn_was_pressed)
  {
    btn_was_pressed = false;
  }

  // si se acumularon 3 pulsaciones dentro de la ventana -> triple click
  if (btn_press_count >= 3 && (now - btn_last_press_time) <= windowMs)
  {
    btn_press_count = 0; // reset
    return true;
  }

  // reset si se excede la ventana
  if ((now - btn_last_press_time) > windowMs)
  {
    btn_press_count = 0;
  }

  return false;
}

// Detector bloqueante: cuenta cuántas pulsaciones (press->release) se detectan dentro de una ventana de tiempo (ms)
int countButtonPressesWithinWindow(unsigned long windowMs)
{
  unsigned long start = millis();
  int count = 0;
  bool pressed = false;

  // Si el botón está presionado al inicio, esperar a que suelte para evitar contar pulsación sostenida
  if (digitalRead(PRG_BUTTON_PIN) == LOW)
  {
    while (digitalRead(PRG_BUTTON_PIN) == LOW && (millis() - start) < windowMs)
    {
      delay(10);
    }
  }

  while ((millis() - start) < windowMs)
  {
    bool now = digitalRead(PRG_BUTTON_PIN) == LOW;
    if (now && !pressed)
    {
      // debounce
      delay(50);
      if (digitalRead(PRG_BUTTON_PIN) == LOW)
      {
        count++;
        pressed = true;
      }
    }
    else if (!now && pressed)
    {
      pressed = false;
    }
    delay(10);
  }

  return count;
}
