# 原版安装教程参考：https://wiki.x2go.org/doku.php/wiki:advanced:x2gokdrive:start?s[]=kdrive

# 此版本的x2gokdrive替换了原版的JPEG和PNG编码，使用x264编码器进行编码，具体安装步骤如下：
1. 下载源码：`git clone https://github.com/thenotoriousean/x2gokdrive.git`
2. 安装xorg-server：`apt-build source xorg-server`
3. 进入xorg目录进行操作：`cd /var/cache/apt-build/build/xorg-server-1.20.11`
4. 构建项目：`dpkg-buildpackage -uc -us` 
5. 进入xorg-server-1.20.11目录进行操作：`cd xorg-server-1.20.11` 
6. 将x2gokdrive复制到xorg-server/hw/kdrive/：`cp -r  /home/download/x2gokdrive /var/cache/apt-build/build/xorg-server-1.20.11/hw/kdrive/`，此处注意修改相关路径！
6. 创建目录xorg-server/debian/build/main/hw/kdrive/x2gokdrive： `mkdir -p debian/build/main/hw/kdrive/x2gokdrive`
7. 进入x2gokdrive目录：`cd debian/build/main/hw/kdrive/x2gokdrive`
7. 创建软链接：`ln -s /var/cache/apt-build/build/xorg-server-1.20.11/hw/kdrive/x2gokdrive/Makefile Makefile `
8. 构建x2goagent: `make x2go`
9. 将生成的x2gokdrive复制到/usr/bin目录下： `cp x2gokdrive /usr/bin/x2goagent`
