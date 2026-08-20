#!/bin/bash

#ln -sf /usr/share/zoneinfo/Europe/Minsk /etc/localtime
#apt install pigz sshpass dialog nano

date=$(date +"%d.%m.%Y.%H.%M.%S")

DIR="./WD/$date/"

#./xbuild.sh clean
./xbuild.sh build
pigz -11 ./arch/arm64/boot/emmc.uImage
mv ./arch/arm64/boot/emmc.uImage.gz ./arch/arm64/boot/emmc.uImage
mkdir -p $DIR
cp -r ./arch/arm64/boot/* $DIR
cp ./.config $DIR
cp ./drivers/usb/storage/usb-storage.ko $DIR 
cp ./drivers/usb/storage/uas.ko $DIR
cp ./drivers/phy/phy-rtk-sata.ko $DIR
