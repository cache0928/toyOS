require 'fileutils'

puts "kernel build begin"
if !Dir.exist?("build") then
    Dir.mkdir("build", 755)
end
# 编译链接
puts "begin compile & link"
`
nasm -o build/mbr.bin -I boot/include/ boot/mbr.s
nasm -o build/loader.bin -I boot/include/ boot/loader.s
clang -target i386-apple-linux-elf -I lib/kernel/ -c -o build/timer.o device/timer.c
clang -target i386-apple-linux-elf -I lib/kernel/ -I kernel/ -I device/ -fno-builtin -c -o build/init.o kernel/init.c
clang -target i386-apple-linux-elf -I lib/kernel/ -I kernel/ -fno-builtin -c -o build/interrupt.o kernel/interrupt.c
clang -target i386-apple-linux-elf -I lib/kernel/ -I kernel/ -fno-builtin -c -o build/main.o kernel/main.c
nasm -f elf -o build/print.o lib/kernel/print.s
nasm -f elf -o build/kernel.o kernel/kernel.s
alias i386-elf-ld=~/Dropbox/Developer/binutils/bin/i386-unknown-linux-gnu-ld
i386-elf-ld -Ttext 0xc0001500 -e main -o build/kernel.bin build/main.o build/init.o build/interrupt.o build/print.o build/kernel.o build/timer.o
`
puts "compile & link successfully"
# 写入到磁盘镜像中
bochsDir = "/Users/cache/Dropbox/Developer/bochs/"
FileUtils.cp("build/mbr.bin", bochsDir)
FileUtils.cp("build/loader.bin", bochsDir)
FileUtils.cp("build/kernel.bin", bochsDir)
Dir.chdir(bochsDir)
puts "write mbr"
`
dd if=mbr.bin of=hd60m.img bs=512 count=1 conv=notrunc
`
puts "write mbr done"
puts "write bootloader"
`
dd if=loader.bin of=hd60m.img bs=512 count=4 seek=2 conv=notrunc
`
puts "write bootloader done"
puts "write kernel"
`
dd if=kernel.bin of=hd60m.img bs=512 count=200 seek=9 conv=notrunc
`
puts "write kernel done"
FileUtils.rm("kernel.bin")
FileUtils.rm("mbr.bin")
FileUtils.rm("loader.bin")
puts "kernel build complete"
