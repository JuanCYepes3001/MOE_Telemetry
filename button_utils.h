#ifndef BUTTON_UTILS_H
#define BUTTON_UTILS_H

#include <Arduino.h>

// Inicializa variables globales del detector de botones
void init_button_detector();

// Detector no bloqueante de doble click (2 pulsaciones en ventana)
bool nonBlockingDoubleClickDetected(unsigned long windowMs);

// Detector no bloqueante de triple click (3 pulsaciones en ventana)
bool nonBlockingTripleClickDetected(unsigned long windowMs);

// Detector bloqueante (legacy): cuenta pulsaciones dentro de una ventana de tiempo
int countButtonPressesWithinWindow(unsigned long windowMs);

#endif
