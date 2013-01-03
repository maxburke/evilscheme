@echo off

for %%a in (*.c) do cl /c /W4 /Wall /WX /wd4668 /wd4820 /nologo %%a

