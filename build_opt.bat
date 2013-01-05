@echo off

for %%a in (*.c) do cl /Ox /c /W4 /Wall /WX /wd4668 /wd4820 /wd4711 /nologo /Zi %%a

