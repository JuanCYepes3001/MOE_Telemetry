#include "display_utils.h"
#include "config.h"

// Definición del objeto display
SSD1306Wire oled_display(0x3c, 500000, SDA_OLED, SCL_OLED, GEOMETRY_128_64, RST_OLED);

// Enciende pantalla OLED
void VextON() 
{
  pinMode(Vext, OUTPUT);
  digitalWrite(Vext, LOW);
  delay(20);
}

// Apaga pantalla OLED para ahorro energético
void VextOFF() 
{
  pinMode(Vext, OUTPUT);
  digitalWrite(Vext, HIGH);  // Apaga la pantalla OLED
  delay(20);
}

// Inicializa la pantalla OLED
void init_display()
{
  VextON();
  oled_display.init();
}

//  Función que me permite imprimir mensajes a 3 lineas en la pantalla OLED integrada
void display_oled_message_3_line(String line_1, String line_2, String line_3) 
{
  oled_display.clear();
  oled_display.setFont(ArialMT_Plain_16);
  oled_display.setTextAlignment(TEXT_ALIGN_CENTER);
  oled_display.drawString(64, 5,  line_1);
  oled_display.drawString(64, 25, line_2);
  oled_display.drawString(64, 45, line_3);
  oled_display.display();
}

// Mostrar información de AP: SSID, IP y MAC con fuente más pequeña y alineado a la izquierda
void display_oled_ap_info(const String &ssid, const String &ip, const String &mac)
{
  oled_display.clear();
  oled_display.setFont(ArialMT_Plain_10);
  oled_display.setTextAlignment(TEXT_ALIGN_LEFT);

  // Truncar si es necesario para que encaje en 128px de ancho
  String s1 = ssid;
  if (s1.length() > 20) s1 = s1.substring(0, 20) + "...";
  String s2 = "IP: " + ip;
  String s3 = "MAC: " + mac;

  oled_display.drawString(2, 6, s1);
  oled_display.drawString(2, 26, s2);
  oled_display.drawString(2, 46, s3);
  oled_display.display();
}

//  Función que me permite imprimir mensajes a 2 líneas en la pantalla OLED integrada
void display_oled_message_2_line(String line_1, String line_2) 
{
  oled_display.clear();
  oled_display.setFont(ArialMT_Plain_24);
  oled_display.setTextAlignment(TEXT_ALIGN_CENTER);

  // Calcular coordenadas Y para centrar 2 líneas (cada una de 24px)
  int total_text_height = 2 * 24;
  int top_margin = (64 - total_text_height) / 2;

  oled_display.drawString(64, top_margin, line_1);
  oled_display.drawString(64, top_margin + 24, line_2);

  oled_display.display();
}

// Mostrar un mensaje largo dividiendo por palabras en hasta 3 líneas y adaptando fuente
void display_oled_wrap_message(const String &msg)
{
  String s = msg;
  String lines[3];
  int lineIdx = 0;

  while (s.length() > 0 && lineIdx < 3) {
    int sp = s.indexOf(' ');
    String word;
    if (sp < 0) { word = s; s = ""; }
    else { word = s.substring(0, sp); s = s.substring(sp + 1); }

    if (lines[lineIdx].length() == 0) {
      lines[lineIdx] = word;
    } else {
      // Try to append to current line if reasonable (max ~18 chars)
      if (lines[lineIdx].length() + 1 + word.length() <= 18) {
        lines[lineIdx] += " ";
        lines[lineIdx] += word;
      } else {
        lineIdx++;
        if (lineIdx < 3) lines[lineIdx] = word;
      }
    }
  }

  oled_display.clear();
  oled_display.setFont(ArialMT_Plain_16);
  oled_display.setTextAlignment(TEXT_ALIGN_CENTER);
  int y0 = 6;
  if (lines[0].length() > 0) oled_display.drawString(64, y0, lines[0]);
  if (lines[1].length() > 0) oled_display.drawString(64, y0 + 20, lines[1]);
  if (lines[2].length() > 0) oled_display.drawString(64, y0 + 40, lines[2]);
  oled_display.display();
}