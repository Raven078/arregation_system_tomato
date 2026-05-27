# Arregation System Tomato

[![Language: C](https://img.shields.io/badge/Language-C-blue.svg)](https://en.wikipedia.org/wiki/C_(programming_language))
[![Framework: ESP-IDF](https://img.shields.io/badge/Framework-ESP--IDF-red.svg)](https://github.com/espressif/esp-idf)
[![Platform: ESP32-C3](https://img.shields.io/badge/Platform-ESP32--C3-green.svg)](https://www.espressif.com/en/products/socs/esp32-c3)

## 📝 О проекте

Данный проект является частью SCADA-системы автополива. Он реализован для теплицы "Помидорник" и предназначен для сбора данных с датчиков, управления периферией (насос, клапан) и передачи данных на головное устройство. Отправка данных осуществляется в формате JSON каждые 10 минут через Wi-Fi.

## ✨ Основные возможности

- **Сбор показателей**: опрос датчиков влажности почвы и уровня воды.
- **Измерение температуры**: получение данных с цифрового датчика.
- **Управление поливом**: автоматическое управление насосом и клапаном на основе показателей.
- **Передача данных**: отправка JSON-пакетов на сервер (головное устройство) по TCP.

## 🛠️ Технологический стек

*   **Язык программирования**: C (97.5%)
*   **Платформа**: Микроконтроллер ESP32-C3
*   **Фреймворк**: ESP-IDF v6.1
*   **Сборка проекта**: CMake

## 🚀 Начало работы

Эти инструкции помогут вам развернуть копию проекта на вашем устройстве для целей разработки и тестирования.

### Предварительные требования

*   Установленный [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/index.html) (версия 6.1 или совместимая).
*   Микроконтроллер ESP32-C3 Super Mini.
*   Набор поддерживаемых датчиков (см. раздел "Датчики").

### Установка и сборка

1.  **Клонируйте репозиторий:**
    ```bash
    git clone https://github.com/Raven078/arregation_system_tomato.git
    cd arregation_system_tomato
