target: bootsect.bin kernel.bin

kernel.o: kernel.cpp
	gcc -ffreestanding -m32 -fno-pie -fno-stack-protector -c kernel.cpp -o kernel.o

kernel.bin: kernel.o kernel.ld
	ld -m elf_i386 -T kernel.ld --oformat binary -o kernel.bin kernel.o

bootsect.bin: bootsect.asm
	yasm -f bin -o bootsect.bin bootsect.asm

run: target
	qemu-system-i386 -drive file=bootsect.bin,format=raw,if=floppy,index=0 \
	                 -drive file=kernel.bin,format=raw,if=floppy,index=1 -boot a

run-v2: target
	qemu-system-i386 -fda bootsect.bin -fdb kernel.bin -boot a

clean:
	rm -f *.o *.bin

.PHONY: target clean make run
