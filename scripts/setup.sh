#! /bin/bash
{
mbpu_has() {
  type "$1" > /dev/null 2>&1
}

nvm_default_install_dir() {
  [ -z "${XDG_CONFIG_HOME-}" ] && printf %s "${HOME}/.mbpu" || printf %s "${XDG_CONFIG_HOME}/.mbpu"
}

mbpu_install_dir() {
  if [ -n "$MBPU_DIR" ]; then
    printf %s "${MBPU_DIR}"
  else
    nvm_default_install_dir
  fi
}

mbpu_latest_version() {
  echo "master"
}

mbpu_driver_source(){
    local DOWNLOAD_METHOD
    DOWNLOAD_METHOD="$1"
    local MBPU_DRIVER_SOURCE_URL
    MBPU_DRIVER_SOURCE_URL="$NVM_SOURCE"
    if [ "_$DOWNLOAD_METHOD" = "_script-nvm-exec" ]; then
    MBPU_DRIVER_SOURCE_URL="https://raw.githubusercontent.com/the-medium/mbpu-driver/$(mbpu_latest_version)/mbpu-exec"
    elif [ "_$DOWNLOAD_METHOD" = "_script-nvm-bash-completion" ]; then
    MBPU_DRIVER_SOURCE_URL="https://raw.githubusercontent.com/the-medium/mbpu-driver/$(mbpu_latest_version)/bash_completion"
    elif [ -z "$MBPU_DRIVER_SOURCE_URL" ]; then
    if [ "_$DOWNLOAD_METHOD" = "_script" ]; then
        MBPU_DRIVER_SOURCE_URL="https://raw.githubusercontent.com/the-medium/mbpu-driver/$(mbpu_latest_version)/mbpu.sh"
    elif [ "_$DOWNLOAD_METHOD" = "_git" ] || [ -z "$DOWNLOAD_METHOD" ]; then
        MBPU_DRIVER_SOURCE_URL="https://github.com/the-medium/mbpu-driver"
    else
        echo >&2 "Unexpected value \"$DOWNLOAD_METHOD\" for \$DOWNLOAD_METHOD"
        return 1
    fi
    fi
    echo "$MBPU_DRIVER_SOURCE_URL"
}

# mbpu_driver_download() {
#   if nvm_has "curl"; then
#     curl --compressed -q "$@"
#   elif nvm_has "wget"; then
#     # Emulate curl with wget
#     ARGS=$(echo "$*" | command sed -e 's/--progress-bar /--progress=bar /' \
#                             -e 's/-L //' \
#                             -e 's/--compressed //' \
#                             -e 's/-I /--server-response /' \
#                             -e 's/-s /-q /' \
#                             -e 's/-o /-O /' \
#                             -e 's/-C - /-c /')
#     # shellcheck disable=SC2086
#     eval wget $ARGS
#   fi
# }

mbpu_setup_env(){
    local OS
    OS="$(grep -oP '(?<=^ID=).+' /etc/os-release | tr -d '"')"

    if [ "$OS" == "centos" ]; then
        [ -e "/lib/modules/$(uname -r)/build" ] || sudo yum install -y "kernel-devel-uname-r == $(uname -r)"
        if ! nvm_has "gcc"; then
            sudo yum install -y gcc
        fi
        if ! yum list installed "zlib-devel" >/dev/null 2>&1; then
            sudo yum install -y zlib-devel
        fi
        
    elif [ "$OS" == "ubuntu" ]; then
        [ -e "/lib/modules/$(uname -r)/build" ] || sudo apt install linux-headers-`uname -r`
        if ! nvm_has "gcc"; then
            sudo apt install gcc
        fi
    fi

    cd ~
    wget https://mirrors.edge.kernel.org/pub/software/utils/pciutils/pciutils-3.6.2.tar.gz --no-check-certificate
    tar -xzvf pciutils-3.6.2.tar.gz && rm pciutils-3.6.2.tar.gz -f
    cd pciutils-3.6.2/
    make
    sudo mv ~/pciutils-3.6.2 /opt

    cd ~
    git clone https://github.com/Xilinx/embeddedsw.git xilinx 
    cd xilinx/
    git checkout mcap --
    cd mcap/linux/
    make PCIUTILS_PATH=/opt/pciutils-3.6.2/
    sudo mv ~/xilinx /opt
}

mbpu_install_from_git(){
    local INSTALL_DIR
    INSTALL_DIR="$(mbpu_install_dir)"

    if [ -d "$INSTALL_DIR/.git" ]; then
    echo "=> mbpu driver is already installed in $INSTALL_DIR, trying to update using git"
    command printf '\r=> '
    command git --git-dir="$INSTALL_DIR"/.git --work-tree="$INSTALL_DIR" fetch origin tag "$(mbpu_latest_version)" --depth=1 2> /dev/null || {
      echo >&2 "Failed to update mbpu driver, run 'git fetch' in $INSTALL_DIR yourself."
      exit 1
    }
    else
        # Cloning to $INSTALL_DIR
        echo "=> Downloading mbpu driver from git to '$INSTALL_DIR'"
        command printf '\r=> '
        mkdir -p "${INSTALL_DIR}"
        if [ "$(ls -A "${INSTALL_DIR}")" ]; then
            command git init "${INSTALL_DIR}" || {
                echo >&2 'Failed to initialize mbpu driver repo. Please report this!'
                exit 2
            }
            command git --git-dir="${INSTALL_DIR}/.git" remote add origin "$(mbpu_driver_source)" 2> /dev/null \
                || command git --git-dir="${INSTALL_DIR}/.git" remote set-url origin "$(mbpu_driver_source)" || {
                echo >&2 'Failed to add remote "origin" (or set the URL). Please report this!'
                exit 2
            }
            command git --git-dir="${INSTALL_DIR}/.git" fetch origin tag "$(mbpu_latest_version)" --depth=1 || {
                echo >&2 'Failed to fetch origin with tags. Please report this!'
                exit 2
            }
        else
            command git -c advice.detachedHead=false clone "$(mbpu_driver_source)" -b "$(mbpu_latest_version)" --depth=1 "${INSTALL_DIR}" || {
                echo >&2 'Failed to clone mbpu driver repo. Please report this!'
                exit 2
            }
        fi
    fi

    command git -c advice.detachedHead=false --git-dir="$INSTALL_DIR"/.git --work-tree="$INSTALL_DIR" checkout -f --quiet "$(mbpu_latest_version)"

    if [ -n "$(command git --git-dir="$INSTALL_DIR"/.git --work-tree="$INSTALL_DIR" show-ref refs/heads/master)" ]; then
        if command git --git-dir="$INSTALL_DIR"/.git --work-tree="$INSTALL_DIR" branch --quiet 2>/dev/null; then
            command git --git-dir="$INSTALL_DIR"/.git --work-tree="$INSTALL_DIR" branch --quiet -D master >/dev/null 2>&1
        else
            echo >&2 "Your version of git is out of date. Please update it!"
            command git --git-dir="$INSTALL_DIR"/.git --work-tree="$INSTALL_DIR" branch -D master >/dev/null 2>&1
        fi
    fi

    echo "=> Compressing and cleaning up git repository"
    if ! command git --git-dir="$INSTALL_DIR"/.git --work-tree="$INSTALL_DIR" reflog expire --expire=now --all; then
        echo >&2 "Your version of git is out of date. Please update it!"
    fi
    if ! command git --git-dir="$INSTALL_DIR"/.git --work-tree="$INSTALL_DIR" gc --auto --aggressive --prune=now ; then
        echo >&2 "Your version of git is out of date. Please update it!"
    fi
    return
}

mbpu_install_as_script(){
    echo "not implemented"
}


mbpu_build_kernel_module(){
    local INSTALL_DIR
    INSTALL_DIR="$(mbpu_install_dir)"  

    if [ -d ${INSTALL_DIR} ]; then
        make -C "$INSTALL_DIR" clean
        make -C "$INSTALL_DIR"
    else
        echo >&2 'MBPU Source Directory is empty. Please report this!'
        exit 2
    fi    
}

mbpu_setup(){
    
    
    if mbpu_has git; then
      mbpu_setup_env $OS
      mbpu_install_from_git
    # elif mbpu_has mbpu_driver_download; then
    #   mbpu_install_as_script
    else
      # echo >&2 'You need git, curl, or wget to install mbpu-driver'
      echo >&2 'You need git to install mbpu-driver'
      exit 1
    fi

    mbpu_build_kernel_module
}

mbpu_setup
}