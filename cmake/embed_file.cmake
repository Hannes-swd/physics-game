# Bettet eine Datei als Byte-Liste ein: cmake -DIN=datei -DOUT=datei.inc -P embed_file.cmake
# Verwendung im Code: static const unsigned char data[] = {
#                     #include "datei.inc"
#                     };
file(READ "${IN}" hex HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
# Zeilenumbrüche, damit der Compiler keine riesige Zeile lesen muss
string(REPEAT "0x..," 64 line)
string(REGEX REPLACE "(${line})" "\\1\n" bytes "${bytes}")
file(WRITE "${OUT}" "${bytes}\n")
