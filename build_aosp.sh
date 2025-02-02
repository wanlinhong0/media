#!/bin/bash
# libVideoCodec的aosp构建脚本
# 版权所有 (c) 华为技术有限公司 2021-2021

cur_file_path=$(cd $(dirname "${0}");pwd)

error()
{
    echo -e "\033[1;31m${*}\033[0m"
}
info()
{
    echo -e "\033[1;36m${*}\033[0m"
}

link_dirs=(
    media
)

source_dirs="
    media
"

so_list=(
    vendor/lib/libVideoCodec.so
    vendor/lib64/libVideoCodec.so
)

setup_env()
{
    export TOP=${AN_AOSPDIR}
    export OUT_DIR=${AN_AOSPDIR}/out
    export ANDROID_BUILD_TOP=${AN_AOSPDIR}
    cd ${cur_file_path}/..
    root_dir=$(pwd)
    for link_dir in ${link_dirs[*]}
    do
        rm -rf ${AN_AOSPDIR}/${link_dir}
        [ ${?} != 0 ] && error "failed to clean link ${link_dir}" && retrun -1
        ln -vs ${root_dir}/${link_dir} ${AN_AOSPDIR}
        [ ${?} != 0 ] && error "failed to link ${link_dir} to ${AN_AOSPDIR}" && retrun -1
    done
    cd -
}

package()
{
    output_dir=${MODULE_OUTPUT_DIR}
    output_symbols_dir=${MODULE_SYMBOL_DIR}
    [ -z "${output_dir}" ] && output_dir=${cur_file_path}/output/aosp && rm -rf ${output_dir} && mkdir -p ${output_dir}
    [ -z "${output_symbols_dir}" ] && output_symbols_dir=${cur_file_path}/output/aosp/symbols && rm -rf ${output_symbols_dir} && mkdir -p ${output_symbols_dir}
    for so_name in ${so_list[@]}
    do
        source_path=${AN_AOSPDIR}/out/target/product/generic_arm64/${so_name}
        source_symbols_path=${AN_AOSPDIR}/out/target/product/generic_arm64/symbols/${so_name}
        target_path=${output_dir}/${so_name%/*}
        [ ! -d "${target_path}" ] && mkdir -p ${target_path}
        symbols_target_path=${output_symbols_dir}/${so_name%/*}
        [ ! -d "${symbols_target_path}" ] && mkdir -p ${symbols_target_path}
        cp -d ${source_path} ${target_path}
        [ ${?} != 0 ] && error "failed to copy ${source_path} to ${target_path}"
        [ -L ${source_path} ] && continue
        cp -d ${source_symbols_path} ${symbols_target_path}
        [ ${?} != 0 ] && error "failed to copy ${source_symbols_path} to ${symbols_target_path}"
    done
    if [ -z "${MODULE_OUTPUT_DIR}" ];then
        cd ${output_dir}
        tar zcvf ../libVideoCodec_aosp.tar.gz vendor
        cd -
    fi
    if [ -z "${MODULE_SYMBOL_DIR}" ];then
        cd ${output_symbols_dir}
        tar zcvf ../../libVideoCodecSymbols_aosp.tar.gz *
        cd -
    fi
}

clean()
{
    if [ -z "${MODULE_OUTPUT_DIR}" ];then
        output_dir=${cur_file_path}/output
        if [ -f "${output_dir}/libVideoCodec_aosp.tar.gz" ];then
            rm -rf ${output_dir}/libVideoCodec_aosp.tar.gz
        fi
        if [ -f "${output_dir}/libVideoCodecSymbols_aosp.tar.gz" ];then
            rm -rf ${output_dir}/libVideoCodecSymbols_aosp.tar.gz
        fi
        if [ -d "${output_dir}/aosp" ];then
            rm -rf ${output_dir}/aosp
        fi
        if [ -f "${output_dir}" ];then
            rm -rf ${output_dir}
        fi
    fi
}

inc()
{
    info "begin incremental compile"
    setup_env
    cd ${AN_AOSPDIR}
    source build/envsetup.sh
    lunch aosp_arm64-eng
    mmm ${source_dirs} showcommands -j
    [ ${?} != 0 ] && error "failed to incremental compile ${source_dirs}" && return -1
    cd -
    package
    info "incremental compile success"
}

build()
{
    info "begin build"
    clean
    [ ${?} != 0 ] && error "failed to clean" && return -1
    inc $@
    [ ${?} != 0 ] && error "failed to build" && return -1
    info "build success"
}

ACTION=$1; shift
case "$ACTION" in
    build) build "$@";;
    inc) inc "$@";;
    clean) clean "$@";;
    *) error "input command[$ACTION] not support.";;
esac
