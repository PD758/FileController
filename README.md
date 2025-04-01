# FileController
[![Build and Test](https://github.com/PD758/FileController/actions/workflows/main.yml/badge.svg)](https://github.com/PD758/FileController/actions/workflows/main.yml)


## Описание
**FileController** — это инструмент для контроля доступа к файлам с использованием драйвера-фильтра файловой системы Windows.
Проект состоит из двух частей:
- **FileController.exe** — графическое приложение для управления доступом к файлам.
- **FileControllerDriver** — драйвер-фильтр файловой системы (KMDF Minifilter).

## Установка из релиза

- Перед установкой отключите Secure Boot в BIOS/UEFI и выполните команду `bcdedit /set testsigning on`. Это необходимо для установки драйвера, подписанного тестовой подписью.
- Скачайте драйвер из одного из доступных релизов: <br>
 [![Stable Release](https://img.shields.io/badge/Release-Stable-green)](https://github.com/PD758/FileController/releases/tag/stable) <br>
[![Unstable Release](https://img.shields.io/badge/Pre--Release-Unstable-orange)](https://github.com/PD758/FileController/releases/tag/latest) 
- Разархивируйте FileControllerDriver.zip
- Установите драйвер, нажав ПКМ на *FileControllerDriver.inf* и выбрав "установить", либо можете использовать команду `pnputil /add-driver FileControllerDriver.inf /install`
- Для запуска драйвера выполните команду `sc start FileControllerDriver` из командной строки с правами администратора, либо перезапустите компьютер
- Для запуска графического интерфейса запустите файл *FileController.exe*

## Сборка из исходных файлов

### Требования
- Visual Studio 2022
- Windows SDK и Windows Driver Kit (WDK)
- Windows 10/11 (x64)

Установить зависимости можно с помощью Chocolatey (если есть):
```sh
choco install windows-sdk-10-version-1903-all windowsdriverkit10 -y
```

### Компиляция
Собрать проект можно с помощью MSBuild:
```sh
msbuild FileController.sln /p:Configuration=Release /p:Platform=x64
```
После успешной компиляции необходимые файлы будут находиться в папке `x64/Release`:
- `FileController.exe` — графическое приложение.
- `x64/Release/FileControllerDriver/FileControllerDriver.inf` — INF-файл для установки драйвера.
- `x64/Release/FileControllerDriver/FileControllerDriver.cat` — файл подписи драйвера.
- `x64/Release/FileControllerDriver/FileControllerDriver.sys` — исполняемый файл драйвера.

### Установка драйвера
1. Разрешить тестовые подписи и отключить Secure Boot:
   ```sh
   bcdedit /set testsigning on
   ```
   После выполнения перезагрузите компьютер.

2. Установить драйвер:
   - Перейдите в папку `x64/Release/FileControllerDriver`.
   - Нажмите ПКМ на `FileControllerDriver.inf` и выберите **"Установить"**. Также можно использовать команду `pnputil /add-driver FileControllerDriver.inf /install`.
   - Подтвердите установку неподписанного драйвера.
   - Дождитесь сообщения об успешной установке.

3. Запустить драйвер:
   ```sh
   sc start FileControllerDriver
   ```
   **Важно!** Запускать команду необходимо в **командной строке (cmd)** с правами администратора (PowerShell не подойдет). Также можно перезагрузить компьютер, тогда драйвер также запустится при запуске.

4. Запуск графического интерфейса:
   - Перейдите в папку `x64/Release`.
   - Запустите `FileController.exe`.

