#!python3
import zipfile

if __name__ == '__main__':
    with zipfile.ZipFile('a.zip', 'w') as z:
        z.write('pretty_printing.h')
        z.write('hlt.hpp')
        z.write('networking.hpp')
        z.write('MyBot.cpp')
