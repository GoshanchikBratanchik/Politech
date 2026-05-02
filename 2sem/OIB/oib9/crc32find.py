def bitss(file_path):

    with open(file_path, 'rb') as file:
        data = file.read()

    bits = []
    for byte in data:

        for i in range(7, -1, -1):
            bits.append((byte >> i) & 1)

    return bits


def crc(bits, polynomial):

    bits += [0] * (len(polynomial) - 1)

    poly_bits = [int(bit) for bit in polynomial]

    for i in range(len(bits) - len(poly_bits) + 1):
        if bits[i] == 1:
            for j in range(len(poly_bits)):
                bits[i + j] ^= poly_bits[j]

    return bits[-(len(poly_bits) - 1):]

def crcshift(bits2, polynomial):
    bits2 += [0] * (len(polynomial) - 1)
    poly_bits = [int(bit) for bit in polynomial[1:]]
    crc2 = [0] * (len(polynomial) - 1)

    for i in range(32):
        crc2[i] = bits2[i + 1]
    for n in range(len(bits2) - 33):
        for j in range(32):
            crc2[j] ^= poly_bits[j]
        if j == 31:
            for k in range(31):
                crc2[k] = crc2[k + 1]
                crc2[31] = bits2[32 + n]
    return crc2


def main():
    file_path = "test.txt"
    polynomial = "100000100110000010001110110110111"
    lenght = len(polynomial)

    bits = bitss(file_path)
    crcc = crc(bits.copy(), polynomial)
    сrc2 = crcshift(bits.copy(), polynomial)
    hexcrc = hex(int(''.join(map(str, crcc)), 2))[2:]
    hexcrc2 = []
    print("CRC in bin:", ''.join(str(bit) for bit in crcc))
    print("CRC in dec:", int(''.join(map(str, crcc)), 2))
    print("CRC in dec:", int(''.join(map(str, crc2)), 2))
    print()
    # print("CRC in hex:", hex(int(''.join(map(str, crcc)), 2)))
      


if __name__ == "__main__":
    main()