#!/usr/bin/env bash
# SCRIPTDIR="${BASH_SOURCE%/*}"
SCRIPTDIR="/opt/devkitpro"

make_flag_list()
{
	while (( "$#" )); do
		echo -n "'$1'";
		if [ $# -gt 1 ]; then
			echo -n ",";
		fi
		shift
	done
}

if [ -z "$1" ]; then
	echo "No platform specified." 1>&2
	exit 1
fi

case "$1" in
"switch")
	PLAT_SYSTEM="horizon"
	PLAT_CPU_FAMILY="aarch64"
	PLAT_CPU="cortex-a57"
	PLAT_ENDIAN="little"
	source ${SCRIPTDIR}/switchvars.sh
	;;
"3ds")
	PLAT_SYSTEM="horizon"
	PLAT_CPU_FAMILY="arm"
	PLAT_CPU="arm11mpcore"
	PLAT_ENDIAN="little"
	source ${SCRIPTDIR}/3dsvars.sh
	;;
"nds")
	PLAT_SYSTEM="bare"
	PLAT_CPU_FAMILY="arm"
	PLAT_CPU="arm946e-s"
	PLAT_ENDIAN="little"
	source ${SCRIPTDIR}/ndsvars.sh
	;;
"ppc")
	PLAT_SYSTEM="bare"
	PLAT_CPU_FAMILY="ppc"
	PLAT_CPU="ppc750"
	PLAT_ENDIAN="big"
	source ${SCRIPTDIR}/ppcvars.sh
	;;
"wiiu")
	PLAT_SYSTEM="cafeos"
	PLAT_CPU_FAMILY="ppc"
	PLAT_CPU="ppc750"
	PLAT_ENDIAN="big"
	source ${SCRIPTDIR}/wiiuvars.sh
	;;	
*)
	echo "Unsupported platform." 1>&2
	exit 1
	;;
esac

echo "[binaries]"
echo "c = '` which ${TOOL_PREFIX}gcc `'"
echo "cpp = '` which ${TOOL_PREFIX}g++ `'"
echo "ar = '` which ${TOOL_PREFIX}gcc-ar `'"
echo "strip = '` which ${TOOL_PREFIX}strip `'"
echo "pkgconfig = '` which ${TOOL_PREFIX}pkg-config `'"
echo ""
echo "[built-in options]"
echo "c_args = [` make_flag_list $CPPFLAGS $CFLAGS `, '-fno-pic', '-fno-pie', '-Wl,-z,relro,-z,now']"
echo "c_link_args = [` make_flag_list $LDFLAGS $LIBS `]"
echo "cpp_args = [` make_flag_list $CPPFLAGS $CXXFLAGS `, '-fno-pic', '-fno-pie', '-Wl,-z,relro,-z,now']"
echo "cpp_link_args = [` make_flag_list $LDFLAGS $LIBS `]"
echo ""
echo "[host_machine]"
echo "system = '${PLAT_SYSTEM}'"
echo "cpu_family = '${PLAT_CPU_FAMILY}'"
echo "cpu = '${PLAT_CPU}'"
echo "endian = '${PLAT_ENDIAN}'"
echo ""
echo "[target_machine]"
echo "system = '${PLAT_SYSTEM}'"
echo "cpu_family = '${PLAT_CPU_FAMILY}'"
echo "cpu = '${PLAT_CPU}'"
echo "endian = '${PLAT_ENDIAN}'"
