sudo pkill -f jupyter-notebook
sudo pkill python
if [[ -d /sys/module/memeater ]]; then
    sudo rmmod memeater
fi
