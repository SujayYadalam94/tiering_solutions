# Loaded by BASH_ENV for isolated script evaluation. Privileged commands are
# recorded rather than executed. Nested defrag is recorded, not launched.
sudo() {
    if [[ "$1" == tee ]]; then
        local value
        value=$(cat)
        printf 'write\t%s\t%s\n' "$2" "$value" >&9
    elif [[ "$1" == sysctl && "$2" == -w ]]; then
        local assignment=${3}
        local key=${assignment%%=*}
        printf 'write\t/proc/sys/%s\t%s\n' "${key//./\/}" "${assignment#*=}" >&9
    else
        printf 'command\t%s\n' "$*" >&9
    fi
}
ps() { :; }
bash() {
    if [[ "$1" == *defrag.sh ]]; then
        printf 'defrag\t%s\n' "$*" >&9
    else
        printf 'Unexpected nested bash: %s\n' "$*" >&2
        return 1
    fi
}
