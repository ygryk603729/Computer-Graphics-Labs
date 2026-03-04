# Лабораторная работа 4: Текстурирование

## Описание
Трёхмерное приложение на DirectX 11 с текстурированным кубом и skybox. Куб вращается, камера управляется стрелками.

## Требования задания
- Добавить DDS текстуру на кубик
- Добавить skybox с cubemap текстурой

## Технологии
- Язык: C++
- IDE: Visual Studio 2022
- API: WinAPI, DirectX 11, DXGI, DirectXMath
- Платформа: x64
- Конфигурации: Debug (отладочный слой) и Release

## Структура папок
/проект/
Source.cpp
/Textures/
brick.dds
/Skybox/
posx.dds
negx.dds
posy.dds
negy.dds
posz.dds
negz.dds


## Управление
- Стрелки влево/вправо — вращение камеры по горизонтали
- Стрелки вверх/вниз — вращение камеры по вертикали

## Ключевые особенности
- Ручная загрузка DDS текстур (без DDSTextureLoader)
- Отдельные константные буферы для model и view-proj
- Cubemap для skybox
- Работа с depth/stencil (skybox не пишет в depth)