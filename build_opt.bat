@echo off

cl /Ox /fp:fast /DEVIL_RUN_TESTS=1 /c /W4 /Wall /WX /wd4324 /wd4668 /wd4820 /wd4711 /wd4710 /nologo /Zi /MP src/*.c tests/*.c *.c /Iinclude /Isrc /Itests /d2Zi+ 

link /nologo /DEBUG /out:r4rs.exe /PDB:r4rs.pdb *.obj

