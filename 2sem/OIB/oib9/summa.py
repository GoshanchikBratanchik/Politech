m = 255
path = 'test.txt'
with open(path, 'r+') as file:
    symbols = file.read()
    t = 0
    for i in symbols:
        t += ord(i)
c = t % (m + 1)
print(t, c)
