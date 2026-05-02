import struct


def read_bmp_pixels(file_path):
    with open(file_path, 'rb') as bmp_file:

        bmp_file.read(18)
        weight = struct.unpack('<I', bmp_file.read(4))[0]
        height = struct.unpack('<I', bmp_file.read(4))[0]
        bmp_file.read(28)
        last_bit_st = ""
        for i in range(height):
            for j in range(weight):
                pixel = bmp_file.read(3)
                for sym in pixel:
                    binary = bin(sym)[2:]
                    last_bit_st += binary[-1]


        len_msg_bin = last_bit_st[:8]
        len_msg = int(len_msg_bin, base=2)

        with open("decode.txt", "w", encoding="UTF-8") as decode:
            decode_message = ""
            if last_bit_st[8] == "0":
                for bit in range(9, 9 + len_msg, 8):
                    byte_bin = last_bit_st[bit:bit + 8]

                    char_value = int(byte_bin, base=2)
                    symbol = chr(char_value)
                    decode.write(symbol)
            else:
                for bit in range(9, 9 + len_msg, 16):
                    byte_bin = last_bit_st[bit:bit + 16]

                    char_value = int(byte_bin, base=2)
                    symbol = chr(char_value)
                    decode.write(symbol)


if __name__ == '__main__':
    read_bmp_pixels('nothing.bmp')