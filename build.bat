@echo off

cl /c /W4 /Wall /WX /wd4668 /wd4820 /wd4711 /DEVIL_RUN_TESTS=1 /nologo /Zi /Od /MP *.c

link /nologo /DEBUG /out:r4rs.exe /PDB:r4rs.pdb *.obj
