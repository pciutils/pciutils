# bash completion for update-pciids                        -*- shell-script -*-

_comp_cmd_update_pciids() {
	COMPREPLY=()
	if [[ " ${COMP_WORDS[*]} " != *" -q "* ]]; then
		COMPREPLY=($(compgen -W "-q" -- "${COMP_WORDS[COMP_CWORD]}"))
	fi
} && complete -F _comp_cmd_update_pciids update-pciids
