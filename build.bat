@echo off

for %%a in (*.c) do cl /c /W4 /Wall /WX /wd4668 /wd4820 /wd4711 /nologo /Zi /Od %%a

link /nologo /DEBUG /out:r4rs.exe /PDB:r4rs.pdb *.obj
