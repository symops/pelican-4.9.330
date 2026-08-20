#!/bin/bash

# Copyright (c) 2020 Western Digital Corporation or its affiliates.
#
# This code is CONFIDENTIAL and a TRADE SECRET of Western Digital
# Corporation or its affiliates ("WD").  This code is protected
# under copyright laws as an unpublished work of WD.  Notice is
# for informational purposes only and does not imply publication.
#
# The receipt or possession of this code does not convey any rights to
# reproduce or disclose its contents, or to manufacture, use, or sell
# anything that it may describe, in whole or in part, without the
# specific written consent of WD.  Any reproduction or distribution
# of this code without the express written consent of WD is strictly
# prohibited, is a violation of the copyright laws, and may subject you
# to criminal prosecution.

export KBUILD_BUILD_USER=kman
export KBUILD_BUILD_HOST=kmachine
export KCFLAGS="-DWD_CUSTOMIZE"
export BUILDNO=1
kernel_version=linux-4.9.330
netatop_version=netatop-1.0
exfat_linux_version=exfat-linux
exfat_linux_sub_version=5.8-2arter97
wifi_driver_version=RTL8822BE_WiFi_Linux_v5.8.7.8_WD_38251.20211221
bt_driver_version=RTL8822BS_Linux_BT_UART_v3.10_20200507_8822BS_BTCOEX_20200103-7979
#rstbtn_version=rstbtn
soc_chip=
log_name=build_kernel.log
BUILD_KERNEL_ROOT_PATH=
build_kernel_out_list=built_kernel.txt

#chip_vendor need to modify in build linux root folder mode
#ex:chip_vendor=realtek
chip_vendor=
#normal build , root_folder_build=NO
#build in linux folder , root_folder_build=YES
root_folder_build=NO
g_model_name=

PROJECT_LIST=(
    "Pelican" "realtek" "WDMyCloudHomeDuo" \
    "Monarch" "realtek" "WDMyCloudHome" \
    "Yodaplus" "realtek" "ibi"
)

function die()
{
    echo "$@"
    if [ -n "$BUILD_KERNEL_ROOT_PATH" ]; then
        echo "$@" >> ${BUILD_KERNEL_ROOT_PATH}/${log_name}
    fi
    exit 1
}

function LOG()
{
    echo "$@"
    if [ -n "$BUILD_KERNEL_ROOT_PATH" ]; then
        echo "$@" >> ${BUILD_KERNEL_ROOT_PATH}/${log_name}
    fi
}

function Help()
{
    echo "Usage:"
    if [ -e platform_chip_vendor.txt ]; then
        echo "    ./xbuild.sh [build/clean]"
        echo "ex: ./xbuild.sh build"
        echo "ex: ./xbuild.sh clean"
    else
        if [ "${root_folder_build}" = "NO" ]; then
            echo "    ./xbuild.sh [Build_Num/clean/untar/src] [platform]"
            echo "ex: ./xbuild.sh 1 Pelican"
            echo "ex: ./xbuild.sh clean Pelican"
            echo "ex: ./xbuild.sh untar Pelican"
            echo "ex: ./xbuild.sh src Pelican"
        else
            echo "    ./xbuild.sh [build/clean]"
            echo "ex: ./xbuild.sh build"
            echo "ex: ./xbuild.sh clean"
        fi
    fi
    exit 0
}

function Get_Chip_Vendor()
{
    local model_name=$1
    for index in ${!PROJECT_LIST[@]}; do
        if [ $(($index%3)) == 0 ] ; then
            project=${PROJECT_LIST[index]}
            index=$index+1
            if [ "${model_name}" = "${project}" ]; then
                g_model_name=${project}
                chip_vendor=${PROJECT_LIST[index]}
                index=$index+1
                product=${PROJECT_LIST[index]}
            fi
        fi
    done
    LOG "chip_vendor=$chip_vendor"
}

function Chk_Model_Name()
{
    local model_name=$1
    local chk_model_status=0

    for index in ${!PROJECT_LIST[@]}; do
        if [ $(($index%3)) == 0 ] ; then
            project=${PROJECT_LIST[index]}
            #echo "project=$project"
            index=$index+1
            if [ "${model_name}" = "${project}" ]; then
                chk_model_status=1
            fi
            index=$index+1
            product=${PROJECT_LIST[index]}
        fi
    done
    if [ $chk_model_status -eq 0 ]; then
        die "Not support $model_name mode."
    fi
}

function Untar_File()
{
    local file_name=$1

    rm -rf ${file_name}

    if [ ! -e ${file_name}.tar.xz ]; then
        die "Can not find ${file_name}.tar.xz"
    fi

    LOG "Decompress ${file_name}.tar.xz"
    tar -xxf ${file_name}.tar.xz || die "Can not decompress ${file_name}.tar.xz"
}

function do_patch()
{
    for patch_name in `ls ../$1 2>/dev/null`; do
        LOG "-->patch name=$patch_name"
        patch -p1 -i "../$1/${patch_name}" || die "patch $patch_name failed"
    done
}

function Patch_File()
{
    local patch_kver=$1
    local patch_path=$2
    local patch_path_generic="$2/../../rtk-generic"

    cd $patch_kver
    do_patch ${patch_path_generic}
    do_patch ${patch_path}
    cd ..
}

function Decompress_Kernel()
{
    local kver=${1}
    local model_name=${2}
    local platform_patch_path=${kver}_patch/platform/${model_name}/
    local build_name=${kver}.${model_name}

    LOG "Decompress Kernel ${kver}"
    LOG "model_name=$model_name"
    LOG "platform_patch_path=$platform_patch_path"

    #if jenkins build , always remove folder
    if [ "${ROLE}" = "jenkins" ] ; then
        rm -rf -d ${build_name}
    fi

    if [ ! -d ${build_name} ]; then
        if [ ! -d ${platform_patch_path} ]; then
            die "Can not find $patch folder"
        fi
        Untar_File ${kver}
        if [ "${chip_vendor}" = "realtek" ]; then
            echo "--> realtek SOC patch"
            soc_chip=1295
            local SOC_patch_path=${kver}_patch/SOC/${soc_chip}/
            Patch_File ${kver} ${SOC_patch_path}
        fi
        Patch_File ${kver} ${platform_patch_path}
        mv ${kver} ${build_name}
    fi

    if [ -e ${build_name}/jenkins_build_test ]; then
        rm -f ${build_name}/jenkins_build_test
    fi

    LOG "Decompress Kernel Success"
}

function Compress_Platform_Kernel_SRC
{
    local model_name=$1
    local platform_chip_vendor=platform_chip_vendor.txt
    local out_src_file=
    local platform_kernel_name=${kernel_version}.${model_name}

    date_time=`date +%Y%m%d`
    echo "date_time=$date_time"
    out_src_file=kernel-${kernel_version#*-}_${model_name}_src_${date_time}.tar.gz

    LOG "Compress kernel binary ${out_src_file}"

    Get_Chip_Vendor $model_name

    if [ "$chip_vendor" != "realtek" ]; then
        die "$chip vendor is not support."
    fi

    echo "$chip_vendor ${model_name}" > ${platform_kernel_name}/${platform_chip_vendor}
    cp xbuild.sh ${platform_kernel_name}

    if [ ! -d ../out/${model_name} ]; then
        mkdir -p ../out/${model_name}
    fi

    rm -rf ../out/${model_name}/${out_src_file}
    tar zcf ../out/${model_name}/${out_src_file} ${platform_kernel_name}

    LOG "output src file : ../out/${model_name}/${out_src_file}"
}

function Compress_Platform_Kernel_GPL_SRC
{
    local model_name=$1
    local platform_chip_vendor=platform_chip_vendor.txt
    local out_src_file=
    local platform_kernel_name=${kernel_version}.${model_name}

    out_src_file=${kernel_version}.tar.gz
    LOG "Compress kernel binary ${out_src_file}"

    Get_Chip_Vendor $model_name

    if [ "$chip_vendor" != "realtek" ]; then
        die "$chip vendor is not support."
    fi

    echo "$chip_vendor ${model_name}" > ${platform_kernel_name}/${platform_chip_vendor}
    cp xbuild.sh ${platform_kernel_name}

    if [ ! -d ../out/${model_name} ]; then
        mkdir -p ../out/${model_name}
    fi

    rm -rf ../out/${model_name}/${out_src_file}
    mv ${platform_kernel_name} ${kernel_version}
    tar zcf ../out/${model_name}/${out_src_file} ${kernel_version}
    rm -rf ${kernel_version}
    LOG "output src file : ../out/${model_name}/${out_src_file}"
}

function Decompress_module()
{
    current_path=`pwd`
    ex_module_name=$1
    LOG "Decompress ext_kernel_modules ${ex_module_name}"

    folder=ext_kernel_modules/${ex_module_name}
    cd ../$folder
    if [ "${ex_module_name}" = "exfat-linux" ]; then
        module_name=${ex_module_name}.${exfat_linux_sub_version}
    else
        module_name=${ex_module_name}
        if [ -d ${module_name} ]; then
            rm -rf ${module_name}
        fi
    fi
    tar -zxf ${module_name}.tar.gz || die "Can not untar ${module_name}.tar.gz"
    Patch_File ${module_name} patch
    cd $current_path
}

function build_external_module()
{
    module_path=$1
    cd ${BUILD_KERNEL_ROOT_PATH}/../ext_kernel_modules/${module_path}/
    ./xbuild.sh build || die "build external module ${module_path} failed"
    cd ${KERNELDIR}
}

function build_realtek()
{
    echo ""
    echo "--> Build Realtek Kernel <--"
    export BASEVERSION=

    export ARCH=arm64
    export CROSS_COMPILE=aarch64-linux-gnu-
    export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin

    make -j`nproc` Image || exit 1
    make dtbs DTC_FLAGS="-p 8192" || exit 1
    
    rm -rf _install
    make -j`nproc` modules || exit 1
    make modules_install INSTALL_MOD_PATH=_install
    export KERNELDIR=`pwd`

    if [ "${root_folder_build}" = "NO" ]; then
        # netatop
        export KERNELDIR=`pwd`
        build_external_module ${netatop_version} || exit 1
        build_external_module ${exfat_linux_version} || exit 1
        if [ "${platform}" = "Yodaplus" ]; then
            build_external_module wifi/${chip_vendor}/${wifi_driver_version} || exit 1
            build_external_module bluetooth/${chip_vendor}/${bt_driver_version} || exit 1
        fi
    fi
    cd arch/arm64/boot/

    if [ "${platform}" = "Pelican" ]; then
        kernel_image=emmc.uImage
        dtb_file=emmc.dtb
        cp dts/realtek/rtd129x/rtd-1296-pelican-1GB.dtb ${dtb_file}
		cp ${dtb_file} rescue.${dtb_file}
    elif [ "${platform}" = "Monarch" ]; then
        kernel_image=sata.uImage
        dtb_file=sata.dtb
        cp dts/realtek/rtd129x/rtd-1295-monarch-1GB.dtb ${dtb_file}
        cp ${dtb_file} rescue.${dtb_file}
    elif [ "${platform}" = "Yodaplus" ]; then
        kernel_image=sata.uImage
        dtb_file=sata.dtb
        cp dts/realtek/rtd129x/rtd-1295-yodaplus-1GB.dtb ${dtb_file}
        cp ${dtb_file} rescue.${dtb_file}
    fi
	rm -f ${kernel_image}
	cp Image ${kernel_image}
	dd if=/dev/zero of=${kernel_image} conv=notrunc oflag=append bs=1k count=512 > /dev/null
    cd -
}

function xbuild()
{
    if [ "${chip_vendor}" = "realtek" ]; then
        build_realtek
    else
        LOG "Not support ${chip_vendor} chip vendor"
    fi
}

function xinstall_external_module()
{
    local tmp_current_path=`pwd`
    local module_name=$1
    cd ${BUILD_KERNEL_ROOT_PATH}/../ext_kernel_modules/${module_name}/
    ./xbuild.sh install
    cd $tmp_current_path
}

function xinstall()
{
    local model_name=$1
    install_kernel=${BUILD_KERNEL_ROOT_PATH}/../out/${model_name}/${model_name}
    install_driver=${BUILD_KERNEL_ROOT_PATH}/../out/${model_name}/${model_name}/driver
    export KERNEL_DRIVER_INSTALL_PATH=$install_driver
    export WD_KERNEL_DRIVER_INSTALL_PATH=$install_rstbtn

    if  [ ! -d ${install_kernel} ]; then
        mkdir -p ${install_kernel}
    fi

    if  [ ! -d ${install_driver} ]; then
        mkdir -p ${install_driver}
    fi

    if [ "${model_name}" = "Pelican" ]; then
        cp -avf arch/${ARCH}/boot/emmc.uImage ${install_kernel}/ || die "Can not copy kernel image"
        cp -avf arch/${ARCH}/boot/${dtb_file} ${install_kernel}/ || die "Can not copy kernel dtb"
    elif [ "${model_name}" = "Monarch" -o "${model_name}" = "Yodaplus" ]; then
        cp -avf arch/${ARCH}/boot/sata.uImage ${install_kernel}/ || die "Can not copy kernel image"
        cp -avf arch/${ARCH}/boot/${dtb_file} ${install_kernel}/ || die "Can not copy kernel dtb"
    fi

    cp -avf \
      crypto/ansi_cprng.ko \
      drivers/platform/kamino/reset_button.ko \
      fs/nfs/flexfilelayout/nfs_layout_flexfiles.ko \
      ${install_driver}/

    # netatop
    xinstall_external_module ${netatop_version}

    xinstall_external_module ${exfat_linux_version}

    if [ "${chip_vendor}" = "realtek" ]; then
        rtk_paragon_driver_path="${BUILD_KERNEL_ROOT_PATH}/../ext_kernel_modules/Paragon"
        if [ -f ${rtk_paragon_driver_path}/${model_name}/jnl.ko ]; then
            cp -avf ${rtk_paragon_driver_path}/${model_name}/jnl.ko ${KERNEL_DRIVER_INSTALL_PATH}/
        fi
        if [ -f ${rtk_paragon_driver_path}/${model_name}/ufsd.ko ]; then
            cp -avf ${rtk_paragon_driver_path}/${model_name}/ufsd.ko ${KERNEL_DRIVER_INSTALL_PATH}/
        fi
        unset rtk_paragon_driver_path
    fi

    if [ "${platform}" = "Yodaplus" ]; then
        xinstall_external_module wifi/${chip_vendor}/${wifi_driver_version}
        xinstall_external_module bluetooth/${chip_vendor}/${bt_driver_version}
    fi

    ${CROSS_COMPILE}strip --strip-debug ${install_driver}/* || die "driver strip failed"
    LOG "--> Install dirver success."
}

function Build_Kernel()
{
    local model_name=$1
    local out_model_path=${BUILD_KERNEL_ROOT_PATH}/../out/$model_name
    local out_list_path=${BUILD_KERNEL_ROOT_PATH}/../out/${build_kernel_out_list}

    LOG "Build Kernel"
    LOG "model_name=$model_name"

    if [ "${root_folder_build}" = "NO" ]; then
        Chk_Model_Name $model_name

        if [ ! -d ../ext_kernel_modules/$netatop_version ]; then
            die "Can not find Kernel $netatop_version version."
        fi

        Get_Chip_Vendor $model_name

        if [ "$chip_vendor" != "realtek" ]; then
            die "$chip vendor is not support."
        fi

        rm -rf ${out_model_path}
        mkdir -p ${out_model_path}
        Decompress_Kernel $kernel_version ${model_name}
        Decompress_module ${netatop_version}
        sync
        sleep 1

        cd ${kernel_version}.$model_name
    fi
    PREV_CROSS_COMPILE=$CROSS_COMPILE
    PREV_PATH=$PATH

    xbuild $model_name
    if [ "${root_folder_build}" = "NO" ]; then
        xinstall $model_name
    fi
    export CROSS_COMPILE=$PREV_CROSS_COMPILE
    export PATH=$PREV_PATH

    if [ "${root_folder_build}" = "NO" ]; then
        echo ""
        cd ${out_model_path}/${model_name}
        compress_name=kernel-${kernel_version#*-}-${BUILDNO}_${product}.tar.gz
        LOG "Compress kernel binary ${compress_name}"
        tar zcf ../${compress_name} . || die "Can not compress file ${compress_name}"
        cd ..
        rm -rf ${model_name}

        #write out list
        if [ ! -e ${out_list_path} ]; then
            echo "${model_name} ${compress_name}" >> ${out_list_path}
        else
            check_build=`cat ${out_list_path} | grep ${model_name}`
            if [ -z "${check_build}" ]; then
                echo "${model_name} ${compress_name}" >> ${out_list_path}
            fi
        fi

        cd $BUILD_KERNEL_ROOT_PATH
    fi
}

function local_build()
{
    BUILD_KERNEL_ROOT_PATH=`pwd`
    BUILDNO=1
    LOG "local build"
    if [ "$1" = "clean" ]; then
        xclean clean
    elif [ "$1" = "build" ]; then
        rm -f ${BUILD_KERNEL_ROOT_PATH}/${log_name}
        Build_Kernel build
    else
        LOG "Input parameter incorrent!"
        Help
    fi
}

function clean_realtek()
{
    export ARCH=arm64
    export CROSS_COMPILE=aarch64-linux-gnu-
    make clean
}

function xclean()
{
    local model_name=$1
    local clean_kernel_path=${kernel_version}.${model_name}
    LOG "make clean"

    if [ "${root_folder_build}" = "NO" ]; then
        LOG "model_name=$model_name"
        Get_Chip_Vendor $model_name
        if [ "$chip_vendor" != "realtek" ]; then
            die "$chip vendor is not support."
        fi
        cd ${clean_kernel_path}
    fi

    if [ "${chip_vendor}" = "realtek" ]; then
        clean_realtek
    else
        LOG "Not support ${chip_vendor} chip vendor"
    fi
    exit 0
}

num=$#

if [ $num -eq 2 ]; then
    if [ "${root_folder_build}" = "NO" ]; then
        BUILD_KERNEL_ROOT_PATH=`pwd`
        if [ "$1" = "clean" ]; then
            platform=$2
            xclean $platform
        elif [ "$1" = "untar" ]; then
            platform=$2
            Get_Chip_Vendor $platform
            if [ -d ${kernel_version}.${platform} ]; then
              rm -rf ${kernel_version}.${platform}
            fi
            Decompress_Kernel ${kernel_version} ${platform}
            exit 0
        elif [ "$1" = "src" ]; then
            platform=$2
            Get_Chip_Vendor $platform
            if [ -d ${kernel_version}.${platform} ]; then
              rm -rf ${kernel_version}.${platform}
            fi
            Decompress_Kernel ${kernel_version} ${platform}
            Compress_Platform_Kernel_SRC $platform
            exit 0
        elif [ "$1" = "gpl" ]; then
            platform=$2
            Get_Chip_Vendor $platform
            if [ -d ${kernel_version}.${platform} ]; then
              rm -rf ${kernel_version}.${platform}
            fi
            Decompress_Kernel ${kernel_version} ${platform}
            Compress_Platform_Kernel_GPL_SRC $platform
            Decompress_module ${netatop_version}
            Decompress_module ${exfat_linux_version}
            exit 0
        fi
        BUILDNO=$1
        rm -f ${BUILD_KERNEL_ROOT_PATH}/${log_name}
        if [ "${2}" = "all" ]; then
            for index in ${!PROJECT_LIST[@]}; do
                if [ $(($index%3)) == 0 ] ; then
                    platform=${PROJECT_LIST[index]}
                    index=$index+1
                    index=$index+1
                    LOG "platform is $platform"
                    Build_Kernel $platform
                fi
            done
        else
            platform=$2
            Build_Kernel ${platform}
        fi
    else
        echo "Please check \"root_folder_build\" parameter setting"
    fi
elif [ $num -eq 1 ]; then
    if [ "${root_folder_build}" = "YES" ]; then
        local_build $1
    elif [ -e platform_chip_vendor.txt ]; then
        chip_vendor=`cat platform_chip_vendor.txt | awk '{print $1}'`
        platform=`cat platform_chip_vendor.txt | awk '{print $2}'`
        root_folder_build="YES"
        echo "chip_vendor=$chip_vendor"
        local_build $1
    else
        echo "Please check \"root_folder_build\" parameter setting"
    fi
else
    LOG "Input parameter incorrent!"
    Help
fi
