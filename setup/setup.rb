require 'fileutils'

puts "kernel build begin"
if !Dir.exist?("build") then
    Dir.mkdir("build", 755)
end
# 编译链接
`
clang -target i386-apple-linux-elf -I lib/kernel/ -I kernel/ -fno-builtin -c -o build/init.o kernel/init.c
clang -target i386-apple-linux-elf -I lib/kernel/ -I kernel/ -fno-builtin -c -o build/interrupt.o kernel/interrupt.c
clang -target i386-apple-linux-elf -I lib/kernel/ -I kernel/ -fno-builtin -c -o build/main.o kernel/main.c
nasm -f elf -o build/print.o lib/kernel/print.s
nasm -f elf -o build/kernel.o kernel/kernel.s
alias i386-elf-ld=~/Dropbox/Developer/binutils/bin/i386-unknown-linux-gnu-ld
i386-elf-ld -Ttext 0xc0001500 -e main -o build/kernel.bin build/main.o build/init.o build/interrupt.o build/print.o build/kernel.o
`
# 写入到磁盘镜像中
bochsDir = "/Users/cache/Dropbox/Developer/bochs/"
FileUtils.cp("build/kernel.bin", bochsDir)
Dir.chdir(bochsDir)
`dd if=kernel.bin of=hd60m.img bs=512 count=200 seek=9 conv=notrunc`
FileUtils.rm("kernel.bin")
puts "kernel build complete"
