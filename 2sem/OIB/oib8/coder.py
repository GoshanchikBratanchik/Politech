import struct


lang = int(input("Выберите язык: (0 - английский 1 - РУССКИЙ) "))
if lang == 1:
    s = input("Input message:(max_size 15 symbol)  ")
    message = ""
    for i in s:
        bs = format(ord(i), '016b')
        message += str(bs)
    len_msg = len(message)
    len_msg_b = bin(len_msg)[2:].zfill(8)
    all_msg = len_msg_b + "1" + message

else:
    s = input("Input message:(max_size 63 symbol) ")
    message = ""
    for i in s:
        bs = format(ord(i), '08b')
        message += str(bs)
    len_msg = len(message)
    len_msg_b = bin(len_msg)[2:].zfill(8)
    all_msg = len_msg_b + "0" + message

print("len message:", len_msg_b)
print("message :", message)
print("full message:", all_msg)


def modify_bmp_file(file_path, message):
    with open(file_path, 'rb+') as bmp_file:

        bmp_file.read(18)
        weight = struct.unpack('<I', bmp_file.read(4))[0]
        height = struct.unpack('<I', bmp_file.read(4))[0]
        bmp_file.read(28)

        if (len(message) + len(len_msg_b)) > 3 * weight * height:
            print("длинное сообщение")
            return

        l = 0
        for i in range(height):
            for j in range(weight):
                pixel_pos = bmp_file.tell()
                pixel_data = bmp_file.read(3)

                if l < len(all_msg):
                    new_pixel = bytearray()
                    for g in range(3):
                        if l < len(all_msg):
                            new_byte = (pixel_data[g] & 0xFE) | int(all_msg[l])
                            new_pixel.append(new_byte)
                            l += 1
                        else:
                            new_pixel.append(pixel_data[g])

                    bmp_file.seek(pixel_pos)
                    bmp_file.write(new_pixel)
                else:
                    break



modify_bmp_file('nothing.bmp', all_msg)