@echo off

cl /c /W4 /Wall /WX /wd4324 /wd4668 /wd4820 /wd4711 /DEVIL_RUN_TESTS=1 /nologo /Zi /Od /MP src/*.c tests/*.c *.c /Iinclude /Isrc /Itests 

link /nologo /DEBUG /out:r4rs.exe /PDB:r4rs.pdb *.obj
