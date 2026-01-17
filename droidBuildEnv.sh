# run with bash
 
# set -e

export HOME=.
export PATH="/data/data/com.termux/files/usr/bin:PATH"

clear
echo "This build enviroment is only meant to be runned on Android with root and termux! do not attempt to run this on a desktop Operating System!"

GREEN="\[$(tput setaf 2)\]"
RESET="\[$(tput sgr0)\]"

PS1="${GREEN}DarwinOnDroid${RESET}>"

