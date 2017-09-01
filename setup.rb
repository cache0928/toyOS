require 'fileutils'

puts "kernel build begin"
if !Dir.exist?("build") then
    Dir.mkdir("build", 755)
end
# 部署环境
BOCHS_DIR = "/Users/cache/Dropbox/Developer/bochs/"
SYS_DISK = "hd60m.img"
FS_DISK = "hd80m.img"
# 编译链接
CC = "clang"
ASM = "nasm"
LD = "~/Dropbox/Developer/binutils/bin/i386-unknown-linux-gnu-ld"
LIB = "-I lib/ -I lib/kernel/ -I lib/user/ -I kernel/ -I device/  -I thread/ -I userprog/ -I fs/"
CFLAGS = "-target i386-apple-linux-elf #{LIB} -fno-builtin -c"
ASMFLAGS = "-f elf"
LDFLAGS = "-Ttext 0xc0001500 -e main -o build/kernel.bin"
puts "begin compile & link"
`
#{ASM} -o build/mbr.bin -I boot/include/ boot/mbr.s
#{ASM} -o build/loader.bin -I boot/include/ boot/loader.s
#{CC} #{CFLAGS} -o build/timer.o device/timer.c
#{CC} #{CFLAGS} -o build/init.o kernel/init.c
#{CC} #{CFLAGS} -o build/interrupt.o kernel/interrupt.c
#{CC} #{CFLAGS} -o build/main.o kernel/main.c
#{CC} #{CFLAGS} -o build/debug.o kernel/debug.c
#{CC} #{CFLAGS} -o build/bitmap.o lib/kernel/bitmap.c
#{CC} #{CFLAGS} -o build/stdio-kernel.o lib/kernel/stdio-kernel.c
#{CC} #{CFLAGS} -o build/list.o lib/kernel/list.c
#{CC} #{CFLAGS} -o build/memory.o kernel/memory.c
#{CC} #{CFLAGS} -o build/thread.o thread/thread.c
#{CC} #{CFLAGS} -o build/string.o lib/string.c
#{CC} #{CFLAGS} -o build/sync.o thread/sync.c
#{CC} #{CFLAGS} -o build/console.o device/console.c
#{CC} #{CFLAGS} -o build/ioqueue.o device/ioqueue.c
#{CC} #{CFLAGS} -o build/ide.o device/ide.c
#{CC} #{CFLAGS} -o build/fs.o fs/fs.c
#{CC} #{CFLAGS} -o build/inode.o fs/inode.c
#{CC} #{CFLAGS} -o build/file.o fs/file.c
#{CC} #{CFLAGS} -o build/dir.o fs/dir.c
#{CC} #{CFLAGS} -o build/keyboard.o device/keyboard.c
#{CC} #{CFLAGS} -o build/tss.o userprog/tss.c
#{CC} #{CFLAGS} -o build/process.o userprog/process.c
#{CC} #{CFLAGS} -o build/syscall.o lib/user/syscall.c
#{CC} #{CFLAGS} -o build/fork.o userprog/fork.c
#{CC} #{CFLAGS} -o build/stdio.o lib/stdio.c
#{CC} #{CFLAGS} -o build/syscall-init.o userprog/syscall-init.c
#{ASM} #{ASMFLAGS} -o build/print.o lib/kernel/print.s
#{ASM} #{ASMFLAGS} -o build/kernel.o kernel/kernel.s
#{ASM} #{ASMFLAGS} -o build/switch.o thread/switch.s
`
OBJS = "build/main.o " + (`ls build/`).split.find_all { |file|
    file.end_with?(".o") && file != "main.o"
}.map{ |x| "build/" + x }.join(' ')
`
#{LD} #{LDFLAGS} #{OBJS}
`
puts "compile & link successfully"

# 写入到磁盘镜像中
FileUtils.cp("build/mbr.bin", BOCHS_DIR)
FileUtils.cp("build/loader.bin", BOCHS_DIR)
FileUtils.cp("build/kernel.bin", BOCHS_DIR)
`rm -rf build/*`
Dir.chdir(BOCHS_DIR)
puts "write mbr"
`
dd if=mbr.bin of=#{SYS_DISK} bs=512 count=1 conv=notrunc
`
puts "write mbr done"
puts "write bootloader"
`
dd if=loader.bin of=#{SYS_DISK} bs=512 count=4 seek=2 conv=notrunc
`
puts "write bootloader done"
puts "write kernel"
`
dd if=kernel.bin of=#{SYS_DISK} bs=512 count=200 seek=9 conv=notrunc
`
puts "write kernel done"
FileUtils.cp("kernel.bin", "/Users/cache/Desktop/kernel.bin")
FileUtils.rm("kernel.bin")
FileUtils.rm("mbr.bin")
FileUtils.rm("loader.bin")
puts "kernel build complete"
