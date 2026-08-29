# bash completion for lspci                                -*- shell-script -*-

_comp_cmd_lspci__simple_field() {
	"$1" -Dnmm | while read -r -a fields; do
		printf "%s\n" "${fields[$2]//\"/}"
	done
}

_comp_cmd_lspci() {
	COMPREPLY=()
	local cur=$2 prev=$3

	# "=" preprocessing for -O k=v completions
	if [[ $prev == "=" ]]; then # -O foo=x
		cur="${COMP_WORDS[COMP_CWORD - 2]}=$cur"
		prev=${COMP_WORDS[COMP_CWORD - 3]}
	elif [[ -z $cur && ${COMP_LINE:$COMP_POINT-1:1} == "=" ]]; then # -O foo=
		cur="$prev="
		prev=${COMP_WORDS[COMP_CWORD - 2]}
	fi

	case $prev in
	-s)
		COMPREPLY=($(compgen -W '$(_comp_cmd_lspci__simple_field "$1" 0)' -- "$cur"))
		# https://tiswww.case.edu/php/chet/bash/FAQ, E13
		[[ $COMP_WORDBREAKS == *:* ]] && COMPREPLY=("${COMPREPLY[@]//:/\\:}")
		return 0
		;;
	-d)
		case $cur in
		*:*:*) # Class
			COMPREPLY=($(compgen -W '$(_comp_cmd_lspci__simple_field "$1" 1)' -- "${cur##*:}"))
			;;
		*:*) # Device ID
			compopt -o nospace
			COMPREPLY=($(compgen -S : -W '$(_comp_cmd_lspci__simple_field "$1" 2)' -- "${cur#*:}"))
			;;
		*) # Vendor ID
			compopt -o nospace
			COMPREPLY=($(compgen -S : -W '$(_comp_cmd_lspci__simple_field "$1" 3)' -- "$cur"))
			;;
		esac
		return 0
		;;
	-i)
		local gz=""
		if "$1" --help 2>&1 | command grep -qF pci.ids.gz &>/dev/null; then
			gz=.gz
		fi
		local IFS=$'\n'
		compopt -o filenames
		COMPREPLY=($(compgen -f -o plusdirs -X '!*.ids'$gz -- "$cur"))
		return 0
		;;
	-p)
		local IFS=$'\n'
		compopt -o filenames
		COMPREPLY=($(compgen -f -o plusdirs -X '!*.pcimap' -- "$cur"))
		return 0
		;;
	-A)
		COMPREPLY=($(compgen -W '$("$1" -A help | command sed -e "/^Known/d")' -- "$cur"))
		return 0
		;;
	-O)
		case $cur in
		devmem.path=* | ecam.acpimcfg=* | ecam.efisystab=* | net.cache_name=*)
			local IFS=$'\n'
			compopt -o filenames
			COMPREPLY=($(compgen -f -- "${cur#*=}"))
			;;
		ecam.x86bios=* | hwdb.disable=*)
			COMPREPLY=($(compgen -W '0 1' -- "${cur#*=}"))
			;;
		proc.path=* | sysfs.path=*)
			local IFS=$'\n'
			compopt -o filenames
			COMPREPLY=($(compgen -d -- "${cur#*=}"))
			;;
		*=*) ;;
		*)
			compopt -o nospace
			COMPREPLY=($(compgen -S = -W '$("$1" -O help | command sed -e "/^Known/d" -e "s/ .*//")' -- "$cur"))
			;;
		esac
		return 0
		;;
	-H)
		COMPREPLY=($(compgen -W '0 1' -- "$cur"))
		return 0
		;;
	-F | devmem.path | ecam.acpimcfg | ecam.efisystab | net.cache_name)
		local IFS=$'\n'
		compopt -o filenames
		COMPREPLY=($(compgen -f -- "$cur"))
		return 0
		;;
	esac

	if [[ $cur == -* ]]; then
		# Excluded on purpose: -m (obsolete), -xxx (dangerous)
		COMPREPLY=($(compgen -W '
		-mm
		-t
		-v
		-vv
		-vvv
		-k
		-xxxx
		-b
		-D
		-P
		-PP
		-n
		-nn
		-q
		-qq
		-Q
		-s
		-d
		-i
		-p
		-M
		-A
		-O
		-G
		-H
		-F
	    ' -- "$cur"))
		return 0
	fi
} && complete -F _comp_cmd_lspci lspci
